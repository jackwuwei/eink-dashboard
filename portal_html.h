#pragma once
#include <pgmspace.h>
// Provisioning page. UI strings live in the JS I{} table ([Chinese, English]); the language selector sets both
// the page language and the device's screen language (cfg.lang).
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="zh"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>墨水屏看板 配置</title>
<style>body{font-family:-apple-system,Helvetica,Arial,sans-serif;margin:0;background:#f4f4f4;color:#222}main{max-width:520px;margin:0 auto;padding:12px}
h1{font-size:20px;margin:8px 0 12px}section{background:#fff;border-radius:10px;padding:12px 14px;margin-bottom:12px;box-shadow:0 1px 2px #0002}
h2{font-size:15px;margin:0 0 8px;color:#444}label{display:block;font-size:13px;color:#555;margin:8px 0 3px}
input,select{width:100%;box-sizing:border-box;padding:8px;border:1px solid #ccc;border-radius:6px;font-size:15px}
.row{display:flex;gap:8px}.row>*{flex:1}button{padding:10px 14px;border:0;border-radius:8px;background:#2f6fed;color:#fff;font-size:15px;margin-top:8px}
button.sec{background:#888}button.warn{background:#c33}#msg{white-space:pre-wrap;font-size:13px;background:#eef;padding:8px;border-radius:6px;min-height:20px;margin-top:8px}
.chk{display:flex;align-items:center;gap:6px;margin-top:8px}.chk input{width:auto}small{color:#777}details summary{cursor:pointer;color:#2f6fed}
.lang{display:flex;align-items:center;gap:8px;margin:8px 0 12px}.lang select{width:auto}.lang span{font-size:13px;color:#555}</style></head><body><main>
<h1 data-i="h1">墨水屏 Homelab 看板</h1>
<div class="lang"><span data-i="langLbl">语言 / Language</span><select id="lang" onchange="setlg(this.value)"><option value="0">中文</option><option value="1">English</option></select></div>
<section><h2>WiFi</h2>
<div class="row"><select id="ssidsel" onchange="pick(this)"></select><button class="sec" onclick="scan()" data-i="rescan">重新扫描</button></div>
<label data-i="ssid">WiFi 名称</label><input id="ssid"><label><span data-i="pass">WiFi 密码</span> <small data-i="passHint">开放网络（无 🔒）留空；网页认证的账号填下面</small></label><input id="pass" type="password">
<details><summary data-i="cpSum">此 WiFi 需要网页认证（Captive Portal）</summary>
<div class="chk"><input type="checkbox" id="pen"><label for="pen" style="margin:0" data-i="penLbl">启用门户自动登录</label></div>
<label><span data-i="purl">登录 URL</span> <small data-i="purlHint">留空自动识别（Aruba 等常见门户）；可填完整地址或以 / 开头的路径</small></label><input id="purl" data-p="purlPh">
<div class="row"><div><label data-i="user">用户名</label><input id="puser"></div><div><label data-i="pw">密码</label><input id="ppass" type="password"></div></div>
<label><span data-i="pfields">表单字段名</span> <small data-i="pfieldsHint">用户名,密码（Aruba 门户自动识别时按 user,password）</small></label><input id="pfields" value="username,password">
<label><span data-i="pextra">附加参数</span> <small>k=v&amp;k2=v2</small></label><input id="pextra">
<label><span data-i="pok">成功判定字符串</span> <small data-i="opt">可空</small></label><input id="pok"></details></section>
<section><h2 data-i="srvSec">Dashboard 服务端</h2>
<label data-i="url">服务端 URL</label><input id="url" data-p="urlPh" oninput="tlsvis()">
<div id="tls" style="display:none"><label data-i="tlsLbl">HTTPS 证书验证</label><select id="tlsmode" onchange="tlsvis()"><option value="0" data-i="tls0">不验证（局域网自签/仅加密）</option><option value="1" data-i="tls1">证书指纹 SHA-1（自签长期证书推荐）</option><option value="2" data-i="tls2">内置根 CA（Let's Encrypt）</option></select>
<div id="fpbox"><label data-i="fp">指纹 SHA-1</label><input id="fp" data-p="fpPh"></div></div>
<button class="sec" onclick="test()" data-i="testBtn">测试连接</button></section>
<section><h2 data-i="dispSec">刷新与显示</h2>
<div class="row"><div><label data-i="poll">轮询间隔（分钟）</label><input id="poll" type="number" min="1" max="60" value="5"></div><div><label data-i="npoll">夜间轮询（分钟）</label><input id="npoll" type="number" min="1" max="60" value="30"></div></div>
<div class="row"><div><label data-i="nstart">夜间开始（时）</label><input id="nstart" type="number" min="0" max="23" value="0"></div><div><label data-i="nend">夜间结束（时）</label><input id="nend" type="number" min="0" max="23" value="6"></div></div>
<div class="chk"><input type="checkbox" id="ntop" checked><label for="ntop" style="margin:0" data-i="ntopLbl">夜间仍每分钟刷新时间/温湿度</label></div>
<div class="row"><div><label data-i="batLbl">电量显示</label><select id="bat"><option value="1" data-i="batPct">百分比</option><option value="0" data-i="batV">电压</option></select></div><div><label data-i="rotLbl">屏幕方向</label><select id="rot"><option value="0" data-i="rot0">正常</option><option value="2" data-i="rot2">旋转 180°</option></select></div></div></section>
<section><button onclick="save()" data-i="saveBtn">保存并连接</button> <button class="warn" onclick="doreset()" data-i="resetBtn">恢复出厂</button>
<div id="msg" data-i="loading">加载中…</div><small id="dev"></small></section></main>
<script>
const I={
title:['墨水屏看板 配置','E-Paper Dashboard Setup'],h1:['墨水屏 Homelab 看板','E-Paper Homelab Dashboard'],langLbl:['语言 / Language','语言 / Language'],
rescan:['重新扫描','Rescan'],ssid:['WiFi 名称','WiFi name'],pass:['WiFi 密码','WiFi password'],
passHint:['开放网络（无 🔒）留空；网页认证的账号填下面','Leave empty for open networks (no 🔒); portal logins go below'],
cpSum:['此 WiFi 需要网页认证（Captive Portal）','This WiFi needs a captive-portal login'],penLbl:['启用门户自动登录','Log in to the portal automatically'],
purl:['登录 URL','Login URL'],purlHint:['留空自动识别（Aruba 等常见门户）；可填完整地址或以 / 开头的路径','Leave empty to auto-detect (Aruba and friends); full URL or a /path'],
purlPh:['留空 / /auth/index.html/u / http://portal.example.com/login','empty / /auth/index.html/u / http://portal.example.com/login'],
user:['用户名','Username'],pw:['密码','Password'],pfields:['表单字段名','Form field names'],
pfieldsHint:['用户名,密码（Aruba 门户自动识别时按 user,password）','user,password (Aruba auto-detect uses user,password)'],
pextra:['附加参数','Extra parameters'],pok:['成功判定字符串','Success match string'],opt:['可空','optional'],
srvSec:['Dashboard 服务端','Dashboard server'],url:['服务端 URL','Server URL'],urlPh:['http://192.168.1.100:8090 或 https://…','http://192.168.1.100:8090 or https://...'],
tlsLbl:['HTTPS 证书验证','HTTPS certificate check'],tls0:['不验证（局域网自签/仅加密）','No check (LAN self-signed / encryption only)'],
tls1:['证书指纹 SHA-1（自签长期证书推荐）','SHA-1 fingerprint (best for long-lived self-signed)'],tls2:["内置根 CA（Let's Encrypt）","Built-in root CA (Let's Encrypt)"],
fp:['指纹 SHA-1','SHA-1 fingerprint'],fpPh:['AA:BB:CC:…（40 位十六进制）','AA:BB:CC:... (40 hex digits)'],testBtn:['测试连接','Test connection'],
dispSec:['刷新与显示','Refresh & display'],poll:['轮询间隔（分钟）','Poll interval (min)'],npoll:['夜间轮询（分钟）','Night poll (min)'],
nstart:['夜间开始（时）','Night start (h)'],nend:['夜间结束（时）','Night end (h)'],ntopLbl:['夜间仍每分钟刷新时间/温湿度','Still refresh clock & T/RH every minute at night'],
batLbl:['电量显示','Battery readout'],batPct:['百分比','Percent'],batV:['电压','Voltage'],rotLbl:['屏幕方向','Screen rotation'],rot0:['正常','Normal'],rot2:['旋转 180°','Rotate 180°'],
saveBtn:['保存并连接','Save & connect'],resetBtn:['恢复出厂','Factory reset'],loading:['加载中…','Loading...'],
scanning:['扫描中…','Scanning...'],scanFail:['扫描失败','Scan failed'],pickWifi:['— 选择 WiFi —','- pick a WiFi -'],openNet:['开放网络，无需密码','Open network, no password'],
needSsid:['WiFi 名称不能为空','WiFi name is required'],badUrl:['服务端 URL 不合法，需形如 http://192.168.1.2:8090','Bad server URL, expected e.g. http://192.168.1.2:8090'],
badFp:['指纹需为 40 位十六进制','Fingerprint must be 40 hex digits'],
cfgLoaded:['已配置，可修改后保存','Configured - edit and save'],cfgFirst:['首次配置：填写 WiFi 和服务端地址','First run: fill in WiFi and the server URL'],cfgFail:['读取配置失败','Could not read the settings'],
testing:['测试中（含连接 WiFi，最多 30 秒）…','Testing (connects to WiFi, up to 30 s)...'],testErr:['测试请求失败：','Test request failed: '],
resHead:['结果','Result'],took:['耗时','took'],
hPortal:['被门户拦截：请启用门户自动登录并填写账号','Blocked by a captive portal: enable portal auto-login and fill in the account'],
hLogin:['门户登录失败：核对账号密码、登录 URL / 字段名 / 附加参数（详见串口日志）','Portal login failed: check the account, login URL, field names and extra parameters (see the serial log)'],
hCert:['证书校验失败：检查指纹/CA 或改为不验证','Certificate check failed: fix the fingerprint/CA or turn the check off'],
saving:['保存中…','Saving...'],saveFail:['保存失败','Save failed'],saved:['已保存，正在连接 WiFi…','Saved, connecting to WiFi...'],
wifiOk:['WiFi 已连接 IP ','WiFi connected, IP '],rebooting:['\n设备 5 秒后重启进入看板模式','\nRebooting into dashboard mode in 5 s'],
wifiFail:['WiFi 连接失败，请检查名称/密码后重试','WiFi connection failed - check the name/password'],waitTimeout:['等待超时，请查看屏幕提示','Timed out - check the screen'],
confReset:['清除全部设置？','Erase all settings?'],resetDone:['已恢复出厂，设备将重启','Factory reset done, rebooting'],
devInfo:['固件 %1  电池 %2V  温度 %3°C 湿度 %4%  时钟芯片 %5','FW %1  battery %2V  %3°C %4% RH  RTC chip %5'],rtcOk:['正常','ok'],rtcNo:['未检测到','not found']};
let LG=0,NETS=null;
const $=i=>document.getElementById(i);const t=k=>(I[k]||['',''])[LG];const msg=s=>$('msg').textContent=s;
function paint(){document.title=t('title');document.documentElement.lang=LG?'en':'zh';
document.querySelectorAll('[data-i]').forEach(e=>{if(e.id!='msg'||!e.dataset.done)e.textContent=t(e.dataset.i)});
document.querySelectorAll('[data-p]').forEach(e=>e.placeholder=t(e.dataset.p));render()}
function setlg(v){LG=+v;$('lang').value=v;paint()}
function pick(s){$('ssid').value=s.value;const o=s.selectedOptions[0];if(s.value&&o&&o.dataset.e=='0'){$('pass').value='';$('pass').placeholder=t('openNet')}else $('pass').placeholder=''}
async function post(u,o){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});return r.json()}
function tlsvis(){const h=$('url').value.trim().startsWith('https://');$('tls').style.display=h?'':'none';$('fpbox').style.display=$('tlsmode').value=='1'?'':'none'}
function form(){return{ssid:$('ssid').value.trim(),pass:$('pass').value,url:$('url').value.trim(),tlsMode:+$('tlsmode').value,fp:$('fp').value,
poll:+$('poll').value,npoll:+$('npoll').value,nstart:+$('nstart').value,nend:+$('nend').value,ntop:$('ntop').checked?1:0,bat:+$('bat').value,rot:+$('rot').value,lang:LG,
pen:$('pen').checked?1:0,purl:$('purl').value.trim(),puser:$('puser').value,ppass:$('ppass').value,pfields:$('pfields').value,pextra:$('pextra').value,pok:$('pok').value}}
function valid(f){if(!f.ssid)return t('needSsid');try{const u=new URL(f.url);if(!/^https?:$/.test(u.protocol))throw 0;if(!u.hostname)throw 0;if(u.port&&(+u.port<1||+u.port>65535))throw 0}catch(e){return t('badUrl')}
if(f.url.startsWith('https://')&&f.tlsMode==1&&!/^[0-9a-fA-F]{40}$/.test(f.fp.replace(/[:\s]/g,'')))return t('badFp');return''}
function render(){const s=$('ssidsel'),v=s.value;
if(!NETS){s.innerHTML=`<option value="">${t(NETS===null?'scanning':'scanFail')}</option>`;return}
s.innerHTML=`<option value="">${t('pickWifi')}</option>`+NETS.map(n=>`<option value="${n.s}" data-e="${n.e?1:0}">${n.s} (${n.r}dBm${n.e?' 🔒':''})</option>`).join('');s.value=v}
async function scan(){NETS=null;render();try{NETS=await(await fetch('/api/scan')).json()}catch(e){NETS=0}render()}
function say(s){$('msg').dataset.done=1;msg(s)}
async function load(){try{const c=await(await fetch('/api/config')).json();setlg(c.lang||0);
for(const k of['ssid','url','poll','npoll','nstart','nend','purl','puser','pfields','pextra','pok','fp'])if(c[k]!==undefined)$(k).value=c[k];
$('tlsmode').value=c.tlsMode||0;$('bat').value=c.bat;$('rot').value=c.rot;$('ntop').checked=!!c.ntop;$('pen').checked=!!c.pen;tlsvis();
$('dev').textContent=t('devInfo').replace('%1',c.fw).replace('%2',c.vbat).replace('%3',c.t).replace('%4',c.h).replace('%5',c.rtc?t('rtcOk'):t('rtcNo'));
say(c.configured?t('cfgLoaded'):t('cfgFirst'))}catch(e){say(t('cfgFail'))}}
async function test(){const f=form();const v=valid(f);if(v)return say(v);say(t('testing'));try{const r=await post('/api/test',f);
say(`${t('resHead')}: ${r.res}\nHTTP ${r.code}  ${t('took')} ${r.ms} ms${r.rev?'  rev '+r.rev:''}${r.ip?'  IP '+r.ip:''}\n${r.err||''}${r.res=='portal'?'\n'+t('hPortal'):''}${r.res=='portal-login-fail'?'\n'+t('hLogin'):''}${r.res=='tls-cert'?'\n'+t('hCert'):''}`)}catch(e){say(t('testErr')+e)}}
async function save(){const f=form();const v=valid(f);if(v)return say(v);say(t('saving'));const r=await post('/api/save',f);if(!r.ok)return say(t('saveFail'));say(t('saved'));
for(let i=0;i<40;i++){await new Promise(r=>setTimeout(r,1000));try{const s=await(await fetch('/api/status')).json();if(s.sta=='connected'){say(t('wifiOk')+s.ip+t('rebooting'));return}if(s.sta=='failed'){say(t('wifiFail'));return}}catch(e){}}say(t('waitTimeout'))}
function doreset(){if(confirm(t('confReset')))post('/api/reset',{}).then(()=>say(t('resetDone')))}
paint();load();scan();
</script></body></html>)HTML";
