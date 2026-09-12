"""Detail-page preview: Servers / Claude / Device (coordinates mirror ui.cpp)
Usage: uvx --with pillow python mock_pages.py [--en]"""
import sys, math
from PIL import Image, ImageDraw, ImageFont, ImageOps
EN = "--en" in sys.argv
def L(zh, en): return en if EN else zh
SFX = "_en" if EN else ""
W, H, TOP = 400, 300, 50
CJK = "NotoSansCJKsc-Regular.otf"; DIGIT = "/System/Library/Fonts/Supplemental/Verdana Bold.ttf"
def f(sz): return ImageFont.truetype(CJK, sz)
def fd(sz): return ImageFont.truetype(DIGIT, sz)
def new():
    img = Image.new("1", (W, H), 1); d = ImageDraw.Draw(img); return img, d
def icon_wx_mini(d, x, y, code):   # small top-bar weather icon, mirrors ui.cpp iconWxMini
    import math
    def cloud(x, y):
        for cx, cy, r in ((x+6, y+6, 4), (x+11, y+4, 5), (x+14, y+7, 3)): d.ellipse([cx-r, cy-r, cx+r, cy+r], outline=0, fill=1)
        d.rectangle([x+4, y+7, x+16, y+11], fill=1); d.line([x+4, y+11, x+16, y+11], fill=0)
    def sun(cx, cy, r):
        d.ellipse([cx-r, cy-r, cx+r, cy+r], outline=0)
        for i in range(8):
            a = i*math.pi/4; d.line([cx+(r+2)*math.cos(a), cy+(r+2)*math.sin(a), cx+(r+4)*math.cos(a), cy+(r+4)*math.sin(a)], fill=0)
    if code <= 3 or code in (37, 38): sun(x+11, y+9, 4); return
    if 26 <= code <= 31:
        for i in range(3): d.line([x+2+(i%2)*3, y+4+i*5, x+16+(i%2)*3, y+4+i*5], fill=0)
        return
    if 32 <= code <= 36:
        d.line([x+2, y+5, x+14, y+5], fill=0); d.line([x+2, y+9, x+19, y+9], fill=0); d.line([x+2, y+13, x+11, y+13], fill=0); return
    if code == 4: sun(x+15, y+5, 3); cloud(x, y+6); return
    cloud(x, y+1)
    if 10 <= code <= 19 and code not in (11, 12):
        for i in range(3): d.line([x+6+i*4, y+13, x+5+i*4, y+17], fill=0)
    elif code in (11, 12): d.line([x+11, y+11, x+8, y+15], fill=0); d.line([x+8, y+15, x+12, y+15], fill=0); d.line([x+12, y+15, x+9, y+19], fill=0)
    elif 20 <= code <= 25:
        for i in range(3): d.rectangle([x+5+i*4, y+14+(i%2)*2, x+6+i*4, y+15+(i%2)*2], fill=0)
def topbar(d):   # same as ui.cpp drawTopBar: time / weekday+date / temperature / humidity / WiFi / battery (charging)
    d.text((4, 42), "16:28", font=fd(38), fill=0, anchor="ls")
    d.text((132, 22), L("周六", "Sat"), font=f(16), fill=0, anchor="ls"); d.text((132, 42), "08-29", font=fd(14), fill=0, anchor="ls")
    x = 184
    icon_wx_mini(d, x, 6, 4); d.text((x+26, 19), L("多云 26~32°C", "Cloudy 26~32°C"), font=f(14), fill=0, anchor="ls")   # top line: the forecast
    d.rounded_rectangle([x+3, 29, x+7, 40], 2, outline=0); d.ellipse([x+2, 39, x+8, 45], fill=0); d.line([x+5, 33, x+5, 41], fill=0)
    d.text((x+14, 44), "28.9°C", font=f(16), fill=0, anchor="ls"); x += 14 + int(d.textlength("28.9°C", font=f(16))) + 10
    d.polygon([(x+5, 30), (x+1, 39), (x+9, 39)], fill=0); d.ellipse([x+1, 36, x+9, 44], fill=0)
    d.text((x+14, 44), "53%", font=f(16), fill=0, anchor="ls")
    bat = "85%"; d.text((W-4, 35), bat, font=f(14), fill=0, anchor="rs")   # far right: battery percentage
    bx = W-4 - int(d.textlength(bat, font=f(14))) - 6 - 24
    cx, cy = bx-6-22+11, 22+12
    for r in (4, 8, 12): d.arc([cx-r, cy-r, cx+r, cy+r], 225, 315, fill=0, width=2)
    d.ellipse([cx-1, cy-2, cx+1, cy], fill=0)
    by = 24
    d.rectangle([bx, by, bx+20, by+11], outline=0); d.rectangle([bx+21, by+3, bx+22, by+8], fill=0)
    for i in range(3): d.rectangle([bx+2+i*6, by+2, bx+6+i*6, by+9], fill=0)
    d.polygon([(bx+12, by-3), (bx+7, by+7), (bx+11, by+7), (bx+9, by+15), (bx+14, by+5), (bx+10, by+5)], fill=1, outline=0)
    d.line([0, TOP, W, TOP], fill=0)
def meter(d, x, y, w, h, pct):
    d.rectangle([x, y, x+w-1, y+h-1], outline=0); fw = int((w-2)*pct/100)
    if fw > 0: d.rectangle([x+1, y+1, x+fw, y+h-2], fill=0)
def status(d, txt=None):
    txt = txt or L("更新 16:25  [2]下页 [3]上页", "Upd 16:25  [2]Next [3]Prev")
    d.text((8, H-7), txt, font=f(14), fill=0, anchor="ls")
def upfmt(up):   # same as model.cpp: "20天22小时" -> "20.9天" / "20.9d"
    import re as _re
    m = _re.match(r"(?:(\d+)天)?(?:(\d+)小时)?", up)
    if not up or not (m.group(1) or m.group(2)): return up
    days = int(m.group(1) or 0) + int(m.group(2) or 0) / 24
    return L(f"{days:.1f}天", f"{days:.1f}d")
def title(d, t):
    d.text((8, TOP+24), t, font=f(16), fill=0, anchor="ls"); d.line([0, TOP+30, W, TOP+30], fill=0)
servers = [("绿联 NAS", True, "4ms", 6, 63, 51, "20天22小时", [("在线设备","29")]), ("飞牛 NAS", True, "4ms", 10, 46, 63, "34天3小时", []),
           ("树莓派", False, "ping 不通", None, None, None, "", []), ("华硕路由器", True, "4ms", 3, 54, 61, "10天10小时", [("在线设备","29")])]
env = [("机柜温度", "27.1°C"), ("机柜湿度", "58%"), ("机柜风扇", "1200rpm")]
if EN:   # server-pushed content (server names, rack entries, ...) is not translated by the firmware, so the English
         # screenshots use a separate set of English sample data
    servers = [("NAS", True, "4ms", 6, 63, 51, "20天22小时", []), ("fnOS", True, "4ms", 10, 46, 63, "34天3小时", []),
               ("Pi 5", False, "no ping", None, None, None, "", []), ("Router", True, "4ms", 3, 54, 61, "10天10小时", [])]
    env = [("Rack temp", "27.1°C"), ("Rack RH", "58%"), ("Rack fan", "1200rpm")]
# ---- Page 1: Servers ----
img, d = new(); topbar(d); title(d, L("服务器详情", "Servers"))
y = TOP + 36
for n, ok, det, cpu, mem, t, up, extra in servers:
    d.text((8, y+16), n, font=f(16), fill=0, anchor="ls")
    d.text((W-8, y+16), (L("在线 ", "Online ") if ok else L("离线 ", "Offline ")) + det, font=f(14), fill=0, anchor="rs")
    if ok:
        d.text((20, y+34), "CPU", font=f(14), fill=0, anchor="ls"); meter(d, 50, y+23, 44, 12, cpu); d.text((98, y+34), f"{cpu}%", font=f(14), fill=0, anchor="ls")
        d.text((136, y+34), L("内存", "RAM"), font=f(14), fill=0, anchor="ls"); meter(d, 168, y+23, 44, 12, mem); d.text((216, y+34), f"{mem}%", font=f(14), fill=0, anchor="ls")
        d.text((256, y+34), f"{t}°C", font=f(14), fill=0, anchor="ls")
        d.text((W-8, y+34), upfmt(up), font=f(14), fill=0, anchor="rs")   # extras (such as the online-device count) are omitted, same as ui.cpp
    else:
        d.text((20, y+34), "—", font=f(14), fill=0, anchor="ls")
    y += 42
d.line([0, y-4, W, y-4], fill=0)
d.text((8, y+14), "   ".join(f"{k} {v}" for k, v in env), font=f(14), fill=0, anchor="ls")
status(d); ImageOps.expand(img.convert("L").resize((W*2, H*2), Image.NEAREST), border=3, fill=0).save(f"page1_servers{SFX}.png")
# ---- Page 2: Claude ----
img, d = new(); topbar(d); title(d, L("Claude 用量", "Claude Usage"))
limits = [("5 小时窗口", 36, "02:19"), ("本周全部", 5, "周三 05:59"), ("本周 · Fable", 9, "周三 05:59")]
if EN: limits = [("5-hour window", 36, "02:19"), ("Week, all models", 5, "Wed 05:59"), ("Week · Fable", 9, "Wed 05:59")]
limits = [(l, p, L(f"{r} 重置", f"resets {r}")) for l, p, r in limits]
y = TOP + 44
for l, pct, r in limits:
    d.text((8, y+16), l, font=f(16), fill=0, anchor="ls"); d.text((W-8, y+16), r, font=f(14), fill=0, anchor="rs")
    meter(d, 8, y+24, 330, 16, pct); d.text((W-8, y+38), f"{pct}%", font=f(16), fill=0, anchor="rs")
    y += 56
status(d); ImageOps.expand(img.convert("L").resize((W*2, H*2), Image.NEAREST), border=3, fill=0).save(f"page2_claude{SFX}.png")
# ---- Page 3: Device ----
img, d = new(); topbar(d); title(d, L("设备信息", "Device"))
rows = [("固件", "hd-0.1.0    唤醒 1234 次    空闲堆 32040"), ("电池", "4.19 V (100%)  未充电"), ("WiFi", "home-wifi  192.168.1.123  -36 dBm"),
        ("服务端", "http://192.168.1.100:8090"), ("TLS", "不验证    会话缓存 无"), ("时钟", "芯片正常    NTP 已校准(27日)    轮询 5 分钟"),
        ("失败计数", "WiFi 0  门户 0  网络 0  温湿度 0"), ("夜间", "00:00–06:00 每 30 分钟    顶栏 刷")]
if EN:
    rows = [("Firmware", "hd-0.1.0    wake 1234   heap 32040"), ("Battery", "4.19 V (100%)  not charging"), ("WiFi", "home-wifi  192.168.1.123  -36 dBm"),
            ("Server", "http://192.168.1.100:8090"), ("TLS", "insecure   session cache no"), ("Clock", "RTC chip   NTP synced(day 27)   poll 5 min"),
            ("Failures", "WiFi 0  portal 0  net 0  T/RH 0"), ("Night", "00:00-06:00 every 30 min   topbar on")]
y = TOP + 40
for k, v in rows:
    d.text((8, y+14), k, font=f(14), fill=0, anchor="ls"); d.text((76, y+14), v, font=f(14), fill=0, anchor="ls"); y += 22
status(d); ImageOps.expand(img.convert("L").resize((W*2, H*2), Image.NEAREST), border=3, fill=0).save(f"page3_device{SFX}.png")
# stitch into one image
sheet = Image.new("L", (W*2+6, (H*2+6)*3+40), 128)
for i, p in enumerate([f"page1_servers{SFX}.png", f"page2_claude{SFX}.png", f"page3_device{SFX}.png"]):
    sheet.paste(Image.open(p), (0, i*(H*2+26)))
sheet.save(f"pages{SFX}.png"); print(f"saved pages{SFX}.png")
