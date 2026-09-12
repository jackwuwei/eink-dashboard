# Homelab 墨水屏看板 — 技术方案

状态：固件 v0.1.0 已写好并烧录（2026-08-26），待现场配网与联调；服务端接口已加到共用后端，待部署
目标硬件：本仓库 PCB V2.3x/V2.4x + 4.2 寸 HINK-E04A13-A0（Z96）
服务端：`server/`（submodule → dashboard-backend，与 Kindle 看板共用采集器，新增 JSON 接口）

---

## 1. 目标与约束

| 需求 | 方案要点 |
|---|---|
| 保留当前屏幕驱动 | 原样使用 `GxEPD2_420_Z96.{h,cpp}`（已从 flash 比对确认机器上是 Z96） |
| 时间、温湿度、电量逻辑与现有固件一致 | 直接移植 `Clock_8025T.ino` / `Get_bat_vcc.ino` 的逻辑，见 §5–§7 |
| 时间、温湿度每分钟更新 | 每分钟整分唤醒一次，只局刷顶栏（同现有时钟模式的整分对齐睡眠） |
| 看板数据由服务端 JSON 提供，本地绘制 | 每 N 分钟（默认 5）连 WiFi 拉 `/api/eink.json`，`rev` 变化才重画内容区 |
| 有变化才局刷 | 顶栏、内容区分别做区域局刷；文本没变的区域不刷；每小时整点一次全刷清残影 |
| 首次配网与现有一致 | 按住按键 3 复位进入配网；AP `ESP8266 E-Paper`；网页填 SSID/密码 |
| 上游 WiFi 需要 Captive Portal 登录 | 配置页可填门户账号/密码；设备检测到被门户拦截时自动提交登录后再拉数据，见 §8.3 |
| 按键功能 | 深睡期间只有 RST 能唤醒，按键功能设计为「组合键」和「唤醒后 10 s 交互窗口」两种形态，见 §8.5 |
| 手机连热点自动弹配置页（Captive Portal 形态一） | AP 模式下起 DNS 劫持 + 各系统探测 URL 302 跳转 |
| 支持 HTTPS | 服务端 URL 可为 `https://`；BearSSL + 会话复用 + 小缓冲，握手只在会话过期时发生，见 §8.6 |
| 省电 | 分钟级唤醒不开 WiFi；WiFi 快连；无变化不刷屏；细节与估算见 §10 |

不做：触摸、SD 卡、阅读器、OTA（首版先不做，后续可加）。

2026-08-29 追加：万年历页（设备端查表算农历/节气/节日）和天气页（服务端 weather.py 下发，默认 Open-Meteo + IP 定位），见 [README.zh-CN.md](README.zh-CN.md) 的「日历与天气页」。

支持 HTTPS：服务端接口和门户登录都可走 TLS（BearSSL），见 §8.6。

---

## 2. 硬件事实（来自 V2.43 原理图与现有固件）

| 项 | 内容 |
|---|---|
| MCU | ESP8266（4 MB flash），深睡靠 GPIO16→RST，已接好 |
| 屏幕 SPI | CS=15, DC=0, RST=2, BUSY=4（同现有固件 `GxEPD2_BW<...>(15,0,2,4)`） |
| I²C | SDA=GPIO13, SCL=GPIO14（与 SPI MOSI/CLK 复用，现有固件每次用前 `Wire.begin(13,14)`） |
| 温湿度 | SHT30 @0x44，由 **GPIO12 通过 Q7 供电**，读前拉高、读后拉低并设 INPUT |
| 时钟芯片 | BL8025T（兼容拆机 RX8025T），库 `BL8025_RTC_V2.0` |
| 电池电压 | A0，分压后经 Q4/Q5 开关，**开关也是 GPIO12**；系数 `adc/1024 × 5.607` |
| 充电 | TC4056，`CHRG` 只接 LED，`STDBY` 悬空，**无 GPIO 可读充电状态** |
| 按键（4.2 寸板 revB-230621，由 Gerber 铜层连通性反推） | SW1 = EN/CH_PD → GND（复位，唯一能从深睡唤醒的键）；SW2 = GPIO0 经串阻 → GND；SW3 = GPIO3/RX 经串阻 → GND；USB-C 旁的小侧键 = 与 SW2 并联在 GPIO0 线上（第 4 个键，电气上等同 SW2）；左侧边为电源拨动开关（BAT → LDO）。GPIO16 已与 RST 短接（深睡唤醒） |
| 电池 | 660 mAh 锂电池，TC4056 充电 |
| 功耗 | 深睡 0.03–0.04 mA；WiFi 工作 70–120 mA；WiFi 关闭运行 17–25 mA |
| 屏幕 | 400×300，局刷 ~0.2 s，全刷 ~1.7 s，支持 `setPartialWindow` 区域局刷 |

---

## 3. 总体架构

```
┌────────── dashboard-backend (NAS Docker) ────────┐
│ collectors.py ──► 状态快照 ──► /dashboard.png (Kindle, 不变) │
│                            └─► /api/eink.json  (新增)       │
└──────────────────────────────┬────────────────────┘
                               │ HTTP, 每 N 分钟一次, ~1 KB
┌──────────────────────────────▼────────────────────┐
│ ESP8266 eink-dashboard.ino                       │
│  每分钟醒: RTC芯片 → 时间 | SHT30 → 温湿度 | A0 → 电压 │
│            顶栏区域局刷（内容有变才刷）               │
│  每N分钟:  +WiFi → JSON → rev变化 → 内容区局刷        │
│  每小时:   全刷一次                                   │
│  深睡到下一个整分                                     │
└────────────────────────────────────────────────────┘
```

设计原则：**设备端自己能拿到的信息（时间/温湿度/电量）不依赖网络**；网络只负责 homelab 状态；服务端挂了看板仍显示时间温湿度，内容区标"离线 + 最后更新时间"。

---

## 4. 屏幕布局（400×300，横向，`setRotation` 与现有一致）

```
 y=0   ┌──────────────────────────────────────────────────────┐
       │ 14:32  周三 08-26     🌡 23.5°C  💧 58%    ⚡▮▮▮ 3.9V │  顶栏 h=40
 y=40  ├──────────────────────────────────────────────────────┤
       │ 绿联 NAS   ●在线  CPU 12%  MEM 45%  41°C   up 12d     │
       │ 飞牛 NAS   ●在线  CPU  8%  MEM 61%  38°C   up  3d     │  服务器卡片
       │ 树莓派     ○离线  ── 整行反白 ──                       │  每行 h=34
       │ 路由器     ●在线  CPU  5%  MEM 30%  52°C   up 30d     │
 y=180 ├──────────────────────────────────────────────────────┤
       │ Claude  5h ▮▮▮▮▮▯▯▯▯▯ 48%  重置 16:00                │  Claude 用量
       │         周 ▮▮▯▯▯▯▯▯▯▯ 21%  重置 周一                  │  h=60
 y=240 ├──────────────────────────────────────────────────────┤
       │ 机柜 27.1°C 风扇 1200rpm   ·  更新 14:30  ·  ⚠ 1 离线  │  底栏 h=60
 y=300 └──────────────────────────────────────────────────────┘
```

- 顶栏（本地数据）与其余区域（远端数据）物理上分开，便于分别局刷。
- 内容区的行数/卡片由 JSON 决定，设备端按模板渲染；服务器最多 4 行，多余截断并在底栏提示。
- 字体：U8g2 `u8g2_font_wqy14_t_gb2312`（中文，~170 KB PROGMEM）+ `logisoso` 数字字体做时间；不再从 LittleFS 读大字库（那是现有固件慢的根源）。
- 显示的百分比条、图标用 GFX 画线/矩形，不放位图资源。
- 中英双语：固件自己的文案两套都在 `i18n.h` 的 `I18N_STRINGS` 表里，按 `cfg.lang` 取（配网页切换）。字模的 32–126 段本来就在，英文不额外占字库。
  服务端下发的字符串（服务器名、Claude 窗口名、天气提示语、城市名）不翻译；天气现象例外——按心知天气码在本地取英文名，预报列另有一套短名。

---

## 5. 时间：与现有时钟模式一致

移植自 `Clock_8025T.ino` / `Other.ino::read_RTC_time()`，保留以下行为：

1. **数据存放**：时分秒放 `ESP.rtcUserMemory`（深睡不丢）；年月日星期放 EEPROM。
2. **时钟芯片自动识别**（`ClockChipCheck`）：写一组测试时间，按 BL 顺序（type 0）和 RX 偏移（type 1）各读一次，匹配者存 EEPROM `type_8025T`。
3. **合法性校验**（`ClockReadCheck`）：年 2020–2099、月日时分秒范围合法才认为芯片可用；否则标记 `rtc_error`，退化为软件累加。
4. **首次开机 / RTC memory 无效**：连 WiFi → NTP（6 次重试、轮换 5 个国内 NTP）→ 写 RTC memory → `rtc.write()` 写芯片 → 回读校验。
   - 改进：用 NTP 的 epoch 直接算年月日星期（`localtime`），**不再请求 `api.xygeng.cn/day`**。
5. **每次唤醒**：芯片可用则 `rtc.read()` 覆盖 RTC memory；不可用则 `minute += 1` 软件累加并处理进位。
6. **定期校准**：每天 03:00 的那次 JSON 拉取顺带 NTP 一次并回写芯片（合并进同一次 WiFi 连接，不额外耗电）；芯片不可用时改为每 `clockJZJG`（默认 60）次唤醒校准一次，与现有一致。
7. **校准失败补偿**（`ManualCompensation8025T`）：保留，累计误差 ≥1 s 时修正芯片秒寄存器。
8. **整分对齐睡眠**（`DisplaySetup.ino:567-577`）：`sleep = 60000 − 秒×1000 − 本次运行耗时 − 上次运行耗时`，使下次醒来落在整分；深睡前 `display.powerOff()`（不 hibernate，保住局刷基准）。

---

## 6. 温湿度：与现有一致

移植 `Get_bat_vcc.ino::get_dht30_data()`：
- 每次唤醒读一次：GPIO12 拉高供电 → `Wire.begin(13,14)` → `sht3xd.begin(0x44)` → `readTempAndHumidity(REPEATABILITY_LOW, CLOCK_STRETCH, 50)` → GPIO12 拉低设 INPUT。
- 读失败保留上次值并在图标旁画"?"；连续 3 次失败视为无传感器，顶栏隐藏温湿度。
- 显示一位小数；**只有当四舍五入到 0.1 后的值与上次不同才触发顶栏局刷**（上次值存 RTC memory）。
- 随 JSON 请求上报 `?t=23.5&h=58`，服务端可转发到 HA（可选）。

---

## 7. 电池电量与充电状态

**电压**：同现有 `getBatVolNew()`（GPIO12 开测量开关，A0 采样 30 次取平均，系数 5.607）；百分比用现有的四次多项式 `getBatVolBfb()`；三格电量图标阈值 3.7/3.5 V 与现有一致；显示电压或百分比可配置（`batDisplayType`）。

**低压保护**：同现有——≤3.1 V 立即永久深睡；≤3.3 V（`BatLow`）显示"电量过低"并永久深睡，插电后按 RST 恢复。

**充电状态（新增，硬件无信号，只能推断）**：
- 每分钟采样一次电压，最近 5 个样本存 RTC memory。
- 判定为"充电中"：`V ≥ 4.15`（恒压段）**或** 5 分钟内电压上升 ≥ 0.04 V；
  判定为"放电"：5 分钟内电压下降或 `V < 4.10` 且不再上升。带迟滞，避免图标闪烁。
- 充电中在电量图标旁画闪电（复用现有 `Bitmap_dlsd` 图标）。
- 局限：刚插电的第 1–2 分钟不会立刻显示；充满后 TC4056 停充，电压稳定在 ~4.2 V 时仍显示闪电（可接受，也可改为"已充满"）。
- 如果以后想要精确检测：把 TC4056 `STDBY`/`CHRG` 飞线到 GPIO3(RX)（V2.4x 上 RX 空闲），代码留编译开关 `CHARGE_DETECT_PIN`。

---

## 8. 网络

### 8.1 首次配网（与现有流程一致 + Captive Portal）

进入方式不变：**按住按键 3 再按 RST**；或 EEPROM 里没有服务端地址 / WiFi 从未连上过时自动进入。

```
AP: SSID "ESP8266 E-Paper"  密码 333333333  IP 192.168.3.3   (与现有相同)
屏幕显示: 热点名 / 密码 / "连接后自动弹出配置页, 或浏览器打开 192.168.3.3"
```

Captive Portal 实现（新增）：
- `DNSServer` 监听 53 端口，`*` 全部解析到 192.168.3.3。
- WebServer 对各系统的联网探测路径返回 302 → `http://192.168.3.3/`：
  - Android：`/generate_204`、`/gen_204`
  - iOS/macOS：`/hotspot-detect.html`、`/library/test/success.html`
  - Windows：`/ncsi.txt`、`/connecttest.txt`、`/redirect`、`/fwlink`
  - 其他未知路径：`onNotFound` 也 302 到首页
- 首页 `Host` 不是 192.168.3.3 时同样 302，保证所有系统的弹窗都落在配置页。
- 配置页（单文件 HTML 内嵌 PROGMEM，不依赖 bootstrap/jquery，保证弱网也能秒开）：
  1. WiFi：扫描列表下拉 + 手动输入 SSID + 密码
  2. **Dashboard 服务端 URL**（必填，默认预填 `http://192.168.1.20:8090`）
     - 接受 `http://host[:port][/path]` 和 `https://host[:port][/path]`，host 可为 IP 或域名；设备只在其后拼 `/api/eink.json`
     - 前端校验：scheme 必须是 http/https、host 非空、端口 1–65535；不合法不允许提交
     - 选 `https://` 时展开"证书验证"单选：不验证 / 证书指纹（填 SHA-1，`AA:BB:…` 格式，自动去冒号和空格）/ 内置根 CA（Let's Encrypt）；默认"不验证"，页面给一句提示说明各自适用场景
     - **"测试连接"按钮**：设备用当前填写的 URL 和验证方式实际请求一次 `/api/eink.json`，页面显示结果：HTTP 状态码、耗时、`rev`、是否为门户拦截、TLS 错误原因（证书不符 / 握手内存不足 / 超时）；测试通过后再点保存。测试期间保持 AP+STA 共存，不断热点
     - 保存时 URL、验证方式、指纹一起写 EEPROM；正常模式下每次轮询直接使用，不再重新解析
  3. 轮询间隔（分钟，默认 5）、夜间模式时段（默认 00:00–06:00 → 30 min）
  4. 电量显示方式（电压/百分比）
  5. 提交 → 屏幕显示"正在连接 xxx"，成功后显示 IP 并 5 s 后进入正常模式；失败保留热点让用户重填（同现有 `ap_state` 状态机）
- 配网模式超时 10 分钟无操作深睡（同现有 `overtime`）。
- 仍保留 `/status` 简单 JSON 供调试（电压、温湿度、上次 rev、错误计数）。

### 8.2 日常连接（快连）

- `WiFi.persistent(false)`，凭据在 EEPROM 自己管理，避免每次写 flash。
- RTC memory 缓存 BSSID + 信道 + 上次成功的 IP/网关/掩码；唤醒后 `WiFi.config(ip,gw,mask)` + `WiFi.begin(ssid,pwd,channel,bssid)`，跳过扫描和 DHCP，通常 <1 s 连上。
- 快连失败 → 回退为普通 `WiFi.begin(ssid,pwd)` 并刷新缓存；普通连接也失败 → 记错误计数，本轮跳过网络，屏幕底栏标"WiFi ✕"。
- 连续 3 轮失败后把轮询间隔拉长到 15 分钟，成功后恢复。
- 请求完成立即 `WiFi.mode(WIFI_OFF); WiFi.forceSleepBegin()`，画图和刷屏阶段在低电流下进行。

### 8.3 上游 WiFi 的 Captive Portal 登录（形态二）

场景：设备要连的 WiFi 本身有门户认证（公司/宿舍/酒店网络），DHCP 能拿到 IP，但 HTTP 会被 302 到登录页，需要提交用户名/密码后 MAC 才放行。

**配置**（配网页新增"门户登录"折叠区，默认关闭）：

| 字段 | 说明 | 默认 |
|---|---|---|
| 启用 | 开关 | 关 |
| 登录 URL | 门户的表单提交地址：完整 URL，或以 `/` 开头的路径（拼到跳转地址的主机上）；留空则按跳转地址识别门户类型（Aruba 预设），识别不出就直接 POST 跳转地址 | 空 |
| 用户名 / 密码 | 门户账号 | |
| 字段名 | 表单里用户名/密码的字段名，逗号分隔（Aruba 预设自动改为 `user,password`） | `username,password` |
| 附加参数 | 固定要带的 `k=v&k2=v2`（有些门户要 `action=login`） | 空 |
| 成功判定 | 响应里包含的字符串；留空则以探测通过为准 | 空 |

**运行逻辑**（正常情况下不多发任何请求，只有主请求失败才探测）：

```
GET /api/eink.json
 ├─ 200 且 Content-Type 是 application/json → 正常
 ├─ 302/303 或 200 但正文不是 JSON（HTML）→ 被门户拦截，Location 即门户地址
 ├─ 其它失败（超时 / 证书错 / 拒绝）且启用了门户登录 → 门户通常只劫持 80/443，非标端口和 HTTPS 看不到 302：
 │     纯 HTTP GET connect.rom.miui.com/generate_204（备用 captive.apple.com/hotspot-detect.html）
 │       204 / Success → 不是门户问题，按原错误上报
 │       3xx → 门户，取 Location
 │       200 别的页面 → 门户，从正文找 <meta refresh url=…> / location.href / <form action>（Aruba 就是 200 + meta refresh）
 └─ 被拦截
       ├─ 门户登录未启用 → 底栏标"门户认证" 并跳过本轮
       └─ 已启用 → 沿跳转链走到真正的门户页（只 GET、不自动跟随、最多 4 跳，到 https 或命中门户路径为止）
                  Aruba 实测链路：探测点 200 meta refresh → 同主机 ?cmd=redirect&arubalp=… → 302
                    https://<门户>/upload/custom/<profile>/login.html?cmd=login&mac=…&ip=…&essid=…&url=…
                  登录地址：配置的完整 URL > 配置的路径（拼到门户主机）> 门户类型预设 > 跳转地址本身
                  Aruba 预设（路径 /cgi-bin/login、/auth/index.html、/upload/…）：
                    POST <门户主机>/auth/index.html/u，user / password / cmd=authenticate&Login=Log+In
                    成功回 200 "External Welcome Page"，失败 302 回登录页且 Location 带 errmsg=Authentication%20failed
                  → POST application/x-www-form-urlencoded，不跟 302
                  → 再探测一次确认已放行（204）→ 再 GET /api/eink.json；仍被拦截则计失败
 连续 3 次登录失败 → 底栏标"门户登录失败"，间隔拉长到 15 min，避免账号被锁
```

- 门户页动辄 20 KB，探测/登录响应只读前 4 KB 到 JSON 缓冲里做字符串判定，不用 `getString()` 占堆。
- 门户 TLS 用 `setInsecure()`，接收缓冲 6 KB（企业门户不支持 MFLN 小分片，证书链一整条记录进来）；登录时实测剩余堆 ~19 KB。
- 必须 `ssl=all` 编译：`ssl=basic` 没有 ECDHE，门户直接 handshake_failure。
- cont 栈只有 4 KB：`HTTPClient`（~300 B）放堆上、`Dash` 临时对象（~1.7 KB）放静态区，否则同步 DNS + lwIP 发包路径把栈踩爆（表现为 Soft WDT / 随机异常，`end:` 标记被改写）。
- 门户走 HTTPS 时按 §8.6 的 TLS 方案处理（门户证书千奇百怪，默认 `setInsecure()`，可选指纹）。
- 门户会话通常按 MAC 放行数小时到数天，实际登录频率很低；`WiFi.persistent(false)` 不影响 MAC。
- 不支持：需要 JavaScript 执行、验证码、企业 802.1X（后者是另一套 `WPA2-Enterprise`，如需要另议）。
- 安全：门户账号密码存 EEPROM 明文（与 WiFi 密码同等级），配网页只在 AP 模式可访问。

### 8.4 网络失败分级显示（底栏）

`WiFi ✕` → `门户认证` → `服务端离线` → `数据过期 HH:MM`，四种状态互斥，从左到右优先级递减。

### 8.5 按键功能

硬件事实：4.2 寸板 4 个按键中只有 3 个独立信号——SW1（EN 复位）、SW2（GPIO0）、SW3（GPIO3/RX）；USB-C 旁的侧键与 SW2 并联在 GPIO0 上，固件无法区分，只是换个位置按「按键 2」（外壳装好后顶边按键可能不好按，侧键就是给这个用的）。深睡期间只有 SW1 能唤醒；SW2/SW3 在深睡时按下无效。因此按键功能分两种形态。

**形态 A：组合键（按住 + SW1 复位）** —— 开机瞬间在 `setup()` 读取 GPIO3 电平。
注意 **GPIO0（SW2）按住时复位会让 ROM 进入串口下载模式**，固件根本不会启动，所以不存在"SW2 + 复位"组合；恢复出厂改为配网页里的按钮。

| 组合 | 功能 |
|---|---|
| 单独 SW1 | 立即刷新：强制拉一次 JSON、NTP 校时、全刷整屏；随后 3 秒内按 SW2/SW3 进入交互窗口 |
| 按住 SW3 + SW1 | 进入配网模式（与现有一致） |
| 配网页"恢复出厂" | 清 EEPROM/RTC memory，重新配网 |

**形态 B：交互窗口** —— 由"SW1 复位后 3 s 内按 SW2/SW3"进入（唤醒后先停留 3 s 检测按键，这 3 s 用 20 mA 的 WiFi 关闭状态等待，约 0.017 mAh，仅在手动按 RST 时发生，不影响定时唤醒）。窗口期设备不睡，每次按键把窗口延长到 10 s，无操作后深睡并恢复定时节奏。

| 按键 | 短按 | 长按（≥1 s） |
|---|---|---|
| 按键 2 | 翻页：看板 → 服务器详情页（每台一整块：磁盘、负载、网络流量等 JSON 扩展字段）→ Claude 详情页（按模型周限额）→ 设备信息页（电压曲线、WiFi RSSI/IP、上次同步、错误计数、固件版本）→ 回看板 | 切换电量显示方式（电压/百分比），存 EEPROM |
| 按键 3 | 立即拉一次 JSON 并重画当前页 | 切换夜间模式开/关（顶栏画月亮图标），存 EEPROM |
| 按键 1 | 退出窗口，回看板并深睡 | （硬件复位，等同"立即刷新"） |

- 翻到非看板页后，下一次定时唤醒自动回到看板页（详情页不常驻，避免用户忘了切回去）。
- 服务器详情页需要 JSON 额外字段（`disk`、`load`、`net`），服务端一并下发，设备只在详情页渲染，看板页忽略；整包仍控制在 2 KB 内。
- 按键去抖沿用现有 `key_lb_max=3` 次滤波。
- 如果后续想要"任意键唤醒"，硬件改法：按键 2/3 各经一只二极管并到 RST，代码无需改动（开机时读电平即可区分是哪个键）。

### 8.6 HTTPS 支持

ESP8266 的 TLS 用 core 自带的 BearSSL，两个硬约束：**堆内存**（TLS 连接峰值 ~20 KB）和**握手时间**（RSA-2048 完整握手 1.5–3 s @80 MHz，直接影响每次轮询的耗电）。设计如下：

**内存预算（ESP8266 可用堆约 45–50 KB）**

| 项 | 占用 | 说明 |
|---|---|---|
| GxEPD2 页缓冲 | 7.5 KB | 用 `HEIGHT/2` 分页（两页画完一屏），而不是 15 KB 整屏缓冲 |
| WiFi 协议栈 | ~10 KB | 连接期间 |
| BearSSL 收/发缓冲 | 1 KB + 0.5 KB | `setBufferSizes(1024, 512)`；服务端启用 MFLN（TLS 最大分片协商）时可谈到 512，否则收缓冲需满足服务端记录大小，1 KB 对我们 <2 KB 的 JSON 足够（分多记录接收） |
| BearSSL 内部状态 | ~6–7 KB | 握手期间峰值 |
| ArduinoJson | 3 KB | TLS 断开后再解析（文档缓冲复用 HTTP 正文缓冲） |
| 合计峰值 | ~28 KB | 留 15 KB 以上余量；`ESP.getFreeHeap()` 会记到设备信息页 |

关键顺序：**读传感器 → 连 WiFi → TLS 取 JSON 到 2 KB 静态缓冲 → 断开并销毁 client → 关 WiFi → 解析 → 画图**。TLS 和绘图不同时存在，避免碎片化导致握手失败。

**握手时间与省电**
- **会话复用**：用 `BearSSL::Session` 保存握手结果，序列化后存 RTC memory（`br_ssl_session_parameters` 约 100 B）；深睡后下一次连接 `setSession()` 恢复，握手从 ~2 s 缩到 ~0.3 s，且不做 RSA 验签。服务端需允许 session ID 复用（Caddy/nginx 默认开；Python uvicorn 用 `ssl.SSLContext` 默认也开），过期（服务端通常 5–24 h）后自动完整握手一次并刷新缓存。
- 握手期间 `system_update_cpu_freq(160)`，握手完切回 80 MHz——功耗几乎不变但时间减半。
- 服务端优先用 **ECDSA (P-256) 证书**：ESP8266 验 ECDSA 比 RSA-2048 快 3–4 倍。Let's Encrypt 可直接申请 EC 证书；自签也可以。
- 功耗影响：完整握手一次约 +0.05 mAh，会话复用后每次 +0.01 mAh；按 5 min 轮询、会话 24 h 有效算，每天约 +3 mAh（§11 的 17 mAh/天 → 20 mAh/天，660 mAh 仍约 4 周）。

**证书验证方式**（配网页可选，默认第 2 种）

| 方式 | 安全性 | 维护 | 适用 |
|---|---|---|---|
| `setInsecure()` 不验证 | 仅加密不防中间人 | 零 | 局域网自签、门户登录 |
| **证书指纹**（SHA-1 fingerprint，配网页填） | 防中间人 | 证书续期后要改指纹（LE 每 90 天） | 自签长期证书（推荐给 NAS 自建） |
| **根 CA**（内置 ISRG Root X1，`BearSSL::X509List` PROGMEM ~1.5 KB） | 完整验证 | 零；但需要正确的系统时间 | 公网域名 + Let's Encrypt |

根 CA 方式需要时间——正好有 RTC 芯片，每次唤醒 `settimeofday()` 把芯片时间灌给系统再握手；首次开机 RTC 无效时先用 `setInsecure()` 拉一次 NTP，校准后再切回验证模式。验证失败时底栏标"证书错误"，不降级为不验证（防止静默被劫持），用户可在配网页改成指纹或不验证。

**服务端侧配合**（后端只提供 HTTP，要 HTTPS 自己在前面加一层反代）
- 反代到 `dashboard-backend:8080`，公网域名用自动签发的 EC 证书，内网用自签 + 指纹；开 `session tickets off, session cache on`（ESP8266 BearSSL 只支持 session ID 复用，不支持 ticket）；如可能开 MFLN。
- Kindle 那条链路不受影响，继续走 8090 HTTP。
- 2026-09-08：实际部署里这层反代（原来的 Caddy）已撤掉，设备走 HTTP 8090。

**门户登录的 HTTPS**：门户证书来源不可控（常是自签或企业 CA），默认 `setInsecure()`；登录只在被拦截时发生一次，不做会话缓存。

**HTTP 回退**：URL 写 `http://` 就不加载 BearSSL，行为和 §8.2 一致；两条路径共用同一套请求/解析代码，只是 `WiFiClient` 换 `BearSSL::WiFiClientSecure`。

---

## 9. 服务端接口（在 dashboard-backend 新增）

`GET /api/eink.json?vbat=3.87&t=23.5&h=58&rev=<上次rev>`

```json
{
  "rev": "a1b2c3d4",              // 内容哈希；与请求 rev 相同 → 服务端直接返回 304
  "next_poll": 300,               // 秒；服务端可依据告警/夜间下发 120 / 1800
  "ts": "2026-08-26T14:30:12+08:00",
  "alert": 1,                     // 离线/告警数量, 0 表示全部正常
  "servers": [
    {"n": "绿联 NAS", "ok": 1, "cpu": 12, "mem": 45, "temp": 41, "up": "12d",
     "d": {"disk": "3.2T/8T", "load": "0.4 0.5 0.6", "net": "12M↓ 3M↑"}},   // d: 详情页用, 看板页忽略
    {"n": "树莓派",   "ok": 0}
  ],
  "claude": {
    "h5":   {"pct": 48, "reset": "16:00"},
    "week": {"pct": 21, "reset": "周一"}
  },
  "env": [{"i": "temp", "v": "27.1°C"}, {"i": "fan", "v": "1200rpm"}]
}
```

- 字段名刻意短，看板字段 <1 KB，含详情 `d` 字段 <2 KB；ArduinoJson 静态缓冲 3 KB。
- `rev` 只覆盖显示相关字段（不含 `ts`），否则每次都"有变化"。
- 服务端逻辑：复用现有 `_refresh_loop` 的快照，`render.py` 不动；新增 `eink.py` 负责裁剪字段、算 rev、决定 `next_poll`（夜间时段、`alert>0` 时缩短）。
- 设备上报的 `vbat/t/h` 写入快照，可在 Kindle 看板角落显示或推到 HA（可选，默认只记日志）。

---

## 10. 唤醒时序与刷新策略

### 10.1 每分钟一次的基本循环

```
RST/定时唤醒
 ├─ 读 RTC memory（时间、上次显示值、计数器、WiFi缓存）
 ├─ 读时钟芯片 → 时间           (~5 ms)
 ├─ 读 SHT30 → 温湿度           (~20 ms)
 ├─ 读 A0 → 电压 → 充电推断     (~5 ms)
 ├─ 判断本轮是否要联网: minute % poll_min == 0  或  强制(首次/重试/校准)
 │    是 → WiFi快连 → GET json → 关WiFi     (~1–1.5 s)
 │         200 且 rev 变化 → 标记"内容区需重画", 保存 JSON 摘要到 RTC memory
 │         304 / 失败        → 内容区不动 (失败时底栏标"离线")
 ├─ 决定刷新:
 │    minute == 0 (整点)         → 全刷整屏 (清残影, 顺便重画一切)
 │    顶栏任一显示值变化          → setPartialWindow(0,0,400,40)  局刷
 │    内容区需重画               → setPartialWindow(0,40,400,260) 局刷
 │    都没变                     → 不碰屏幕
 ├─ display.powerOff()
 └─ 计算到下一整分的睡眠时间 → deepSleep
```

"顶栏任一显示值变化"指：分钟数（必然变）、温度/湿度（0.1 精度）、电量格数或百分比、充电图标。因为分钟必变，**顶栏每分钟都会局刷一次（0.2 s）**，这是需求要求的；温湿度不变时只是少画几个字，不影响刷屏次数。

### 10.2 局刷/全刷控制

- Z96 局刷残影随次数累积，现有固件用 `clockQSJG` 计数控制全刷；本方案固定为**每小时整点全刷一次**（60 次局刷一次全刷），并在 JSON `alert` 从 0 变为非 0 时立刻全刷一次以突出告警。
- 局刷用 `display.init(0, 0, 10, 0)`（不做初始全刷），全刷用 `display.init(0, 1, 10, 0)`，与现有 `display_clock()` 相同。
- 局刷用 GxEPD2 分页模式 `firstPage/nextPage`，页缓冲 15 KB，堆余量足够（无 SD、无 HTTPS）。

### 10.3 夜间

服务端 `next_poll` 在夜间时段下发 1800 s；设备端分钟级唤醒照常（时间要走），但 `minute % 30` 才联网。也可在配置页关闭"夜间刷新顶栏"，关闭后夜间每分钟只更新 RTC memory 不刷屏，进一步省电。

---

## 11. 功耗估算（660 mAh 电池）

| 事件 | 频率 | 单次电量 | 每日电量 |
|---|---|---|---|
| 分钟唤醒（不联网，读传感器 + 顶栏局刷 0.2 s） | 1440/天 | ~0.5 s × 20 mA ≈ 0.003 mAh | ~4 mAh |
| 联网轮询（快连 + JSON + 关闭，HTTP） | 288/天（5 min） | ~1.5 s × 90 mA ≈ 0.04 mAh | ~11 mAh |
| HTTPS 额外开销（会话复用，每天 1 次完整握手） | 288/天 | +0.01 mAh（复用）/ +0.05 mAh（完整） | ~+3 mAh |
| 内容区局刷（rev 变化时才有） | ≤288/天 | 0.2 s × 30 mA ≈ 0.002 mAh | ≤0.5 mAh |
| 整点全刷 | 24/天 | 1.7 s × 30 mA ≈ 0.014 mAh | ~0.3 mAh |
| 深睡 | 全天 | 0.035 mA | ~0.8 mAh |
| **合计** | | | **HTTP ≈ 17 mAh/天、HTTPS ≈ 20 mAh/天 → 660 mAh 约 4–5 周** |

夜间 30 min 轮询可再省 ~2 mAh/天。联网是大头，轮询间隔是最有效的旋钮。

---

## 12. RTC memory 布局（512 B，深睡保持）

| 偏移 | 内容 |
|---|---|
| 0 | magic + 版本（无效则视为首次开机） |
| 4 | 时、分、秒（软件时钟备份） |
| 8 | 上次运行耗时、上次睡眠误差（整分对齐用） |
| 16 | 唤醒计数、局刷计数、WiFi 失败计数、校准状态 |
| 32 | WiFi 缓存：BSSID[6]、channel、IP/GW/Mask |
| 48 | 上次显示的顶栏值：温度×10、湿度×10、电量格数、充电标志 |
| 56 | 电压历史 5 个样本（充电推断） |
| 72 | 上次 rev（8 字节） |
| 88 | TLS 会话参数（`br_ssl_session_parameters`，~100 B）+ 有效期戳 |
| 200 | 上次 JSON 摘要（已渲染内容的紧凑表示，用于离线时重画及全刷时重画内容区，≤ 300 B） |

EEPROM（掉电保持）：WiFi SSID/密码、服务端 URL（http/https）、TLS 验证方式（不验证/指纹/根 CA）与指纹、轮询间隔、夜间时段、夜间开关、电量显示方式、时钟芯片类型、时钟补偿累计值、门户登录配置（启用/URL/账号/密码/字段名/附加参数/成功判定）。

---

## 13. 代码结构

```
eink-dashboard/
├── DESIGN.md                      本文档
├── eink-dashboard.ino          setup(): 一次唤醒的全部流程; 无 loop 常驻
├── config.h                       引脚、编译开关(屏型号/按键3引脚/充电检测脚)、默认值
├── GxEPD2_420_Z96.h / .cpp        原样来自 源码&SD例程/4.2寸z96和z98驱动.zip
├── rtc_mem.h/.cpp                 RTC memory 结构体读写 + CRC
├── settings.h/.cpp                EEPROM 配置结构体（ESP_EEPROM）
├── clock.cpp                      移植 Clock_8025T.ino: 芯片识别/读写/NTP 校准/补偿/整分睡眠
├── sensors.cpp                    移植 Get_bat_vcc.ino: SHT30、电压、百分比、充电推断
├── net.cpp                        快连、JSON 拉取、304 处理、门户拦截检测与登录
├── tls.cpp / ca_certs.h           BearSSL 客户端封装：会话缓存到 RTC memory、指纹/根 CA 验证、缓冲大小
├── i18n.h/.cpp                    中英文案表（PROGMEM + 轮转缓冲）、星期/月份/天气码/风向英文名
├── keys.cpp                       开机组合键检测、交互窗口、去抖
├── pages.cpp                      详情页/设备信息页绘制
├── portal.cpp / portal_html.h     配网 AP + DNSServer + Captive Portal 页面
├── ui.cpp                         布局绘制：顶栏、服务器行、Claude 条、底栏；区域局刷调度
└── README.md / README.zh-CN.md   烧录、配网、服务端配置说明（英文 / 中文）
```

依赖库：GxEPD2、Adafruit_GFX、U8g2_for_Adafruit_GFX（原版即可，字库用 PROGMEM 的 wqy）、ArduinoJson 6、ESP_EEPROM、ClosedCube_SHT31D、BL8025_RTC（仓库 zip）、DNSServer/ESP8266WebServer/ESP8266WiFi（core 自带）、NTPClient 或 core 的 `configTime`。

服务端：`server/app/eink.py` + `main.py` 加一个路由；`config.yaml` 加 `eink:` 段（轮询间隔、夜间时段、告警时间隔）。

构建/烧录：本机装 `arduino-cli` + esp8266 core 3.x；`esptool` 已可用（uvx）。**烧录前先把当前 4 MB flash 完整读出保存**，随时可刷回现有天气固件（文件系统区 0x200000 不动）。

---

## 14. 异常处理

| 情况 | 行为 |
|---|---|
| 时钟芯片读数非法 | 标记 rtc_error，改软件累加，下次联网 NTP 后重写芯片并重新校验 |
| SHT30 连续 3 次失败 | 顶栏隐藏温湿度，每小时重试 |
| WiFi 快连失败 | 回退普通连接；再失败则本轮跳过，底栏标"WiFi ✕"，3 轮失败后间隔拉长到 15 min |
| HTTP 超时/非 200/JSON 解析失败 | 内容区保持上次，底栏标"离线 最后更新 HH:MM"；`alert` 视为 1 |
| TLS 握手失败（内存不足/证书不符） | 证书不符 → 底栏"证书错误"，不降级；内存不足 → 清会话缓存重试一次，仍失败按离线处理并记录 `getFreeHeap` 到设备信息页 |
| 电压 ≤ 3.3 V | 显示"电量过低"后永久深睡（同现有） |
| RTC memory CRC 错 | 视为首次开机：全刷、NTP、重建缓存 |
| 配网 10 分钟无操作 | 深睡；下次 RST 若仍无配置则再次进入配网 |

---

## 15. 待确认 / 未决

1. ~~供电方式~~ 已确认：660 mAh 电池，按深睡方案。
2. **布局取舍**：§4 的行高/区域划分按"4 台服务器 + Claude 两条 + 底栏"设计；如果服务器数量或 Claude 卡片优先级不同请指出。
3. **充电显示的推断规则**（§7）是否可接受；若要精确检测需飞一根线。
4. **夜间是否刷新顶栏**：默认刷（时间要走），可在配置页关。
5. ~~按键 3 引脚~~ 按 V2.43 原理图为 GPIO3/RX，留 `KEY3_PIN` 编译开关。
6. ~~按键数量~~ 已从 4.2 寸板 Gerber 反推：SW1=EN、SW2=GPIO0、SW3=GPIO3、侧键=GPIO0（与 SW2 并联），电源拨动开关切 BAT。没有第 4 个独立信号，交互窗口按 3 键设计；侧键当「按键 2」用。
7. **门户登录**：请提供一次实际门户登录页的表单（浏览器 F12 看 POST 的 URL 和字段名），以便默认值贴合实际；也确认是否为 HTTPS。
