"""Local preview: draw the 400x300 dashboard with PIL using ui.cpp's coordinate system and the firmware's own
u8g2 bitmap fonts (see u8g2font.py), so the output is pixel-identical to the device.
Usage: uvx --with pillow python mock.py [out.png] [--en]
--en draws the English UI (cfg.lang=1); fields pushed by the server (server names, Claude window names) are
left as-is, matching the real device."""
import sys
from PIL import Image, ImageOps
from u8g2font import fonts, Draw
EN = "--en" in sys.argv
def L(zh, en): return en if EN else zh
W, H = 400, 300
F = fonts()                       # the firmware's own u8g2 bitmap fonts: 14/16/18 px Noto (fonts_noto.c) + logisoso38 (time)
def f(sz): return F[sz]           # F_SM / F_TXT / F_NAME in ui.cpp
def fd(sz): return F[sz]          # 38 → F_TIME; 14 → the date is plain F_SM on the device
img = Image.new("1", (W, H), 1); d = Draw(img)
def txt(x, y, s, font, fill=0, anchor="ls"): d.text((x, y), s, font=font, fill=fill, anchor=anchor)
def tw(s, font): return d.textlength(s, font=font)
def meter(x, y, w, h, pct): d.rectangle([x, y, x+w-1, y+h-1], outline=0); fw=int((w-2)*pct/100); 
def meter(x, y, w, h, pct):
    d.rectangle([x, y, x+w-1, y+h-1], outline=0)
    fw = int((w-2)*pct/100)
    if fw > 0: d.rectangle([x+1, y+1, x+fw, y+h-2], fill=0)
def icon_temp(x, y): d.rounded_rectangle([x+3, y, x+7, y+11], 2, outline=0); d.ellipse([x+2, y+10, x+8, y+16], fill=0); d.line([x+5, y+4, x+5, y+12], fill=0)
def icon_drop(x, y): d.polygon([(x+5, y), (x+1, y+9), (x+9, y+9)], fill=0); d.ellipse([x+1, y+6, x+9, y+14], fill=0)
def icon_bat(x, y, bars, chg):
    d.rectangle([x, y, x+22, y+11], outline=0); d.rectangle([x+23, y+3, x+24, y+8], fill=0)
    for i in range(bars): d.rectangle([x+2+i*7, y+2, x+7+i*7, y+9], fill=0)
    if chg: d.polygon([(x+13, y-3), (x+8, y+7), (x+12, y+7), (x+10, y+15), (x+15, y+5), (x+11, y+5)], fill=1, outline=0)

def icon_wifi(x, y, bars=3):   # same as ui.cpp iconWifi: 3 arcs + a dot
    cx, cy = x+11, y+12
    for i, r in enumerate((4, 8, 12)):
        if i < bars: d.arc([cx-r, cy-r, cx+r, cy+r], 225, 315, fill=0, width=2)
    d.ellipse([cx-1, cy-2, cx+1, cy], fill=0)

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

# ---------- Top bar, h=50 ----------
TOP = 50
txt(4, 42, "16:28", fd(38))
txt(132, 22, L("周六", "Sat"), f(16)); txt(132, 42, "08-29", fd(14))
x = 184
icon_wx_mini(d, x, 6, 4); txt(x+26, 19, L("多云 26~32°C", "Cloudy 26~32°C"), f(14))          # top line: the forecast
icon_temp(x, 29); txt(x+14, 44, "28.9°C", f(16)); x += 14 + int(tw("28.9°C", f(16))) + 10   # bottom line: local temperature/humidity
icon_drop(x, 30); txt(x+14, 44, "53%", f(16))
bat = "85%"; txt(W-4, 35, bat, f(14), anchor="rs")            # far right: battery percentage (or voltage)
bx = W-4 - int(tw(bat, f(14))) - 6 - 24
icon_wifi(bx-6-22, 22); icon_bat(bx, 24, 3, True)
d.line([0, TOP, W, TOP], fill=0)

# ---------- Servers, 4 rows x 34 ----------
servers = [("绿联 NAS", 6, 63, 51, "20.9天"), ("飞牛 NAS", 3, 46, 61, "34.1天"), ("树莓派", 6, 20, 47, "28.5天"), ("华硕路由器", 3, 54, 61, "0.4天")]
if EN:   # Server-pushed content (server names, Claude window names) is not translated by the firmware, so the English
         # screenshots use a separate set of English sample data; the dashboard name column is only 8..113 px and the
         # Claude label column 86..174 px, and these samples were measured against the font to fit
    servers = [("NAS", 6, 63, 51, "20.9天"), ("fnOS", 3, 46, 61, "34.1天"), ("Pi 5", 6, 20, 47, "28.5天"), ("Router", 3, 54, 61, "0.4天")]
# single header row (column names appear only once)
hy = TOP + 7
def icon_cpu(x, y):   # CPU chip 14x14: a square plus pins on all four sides
    d.rectangle([x+3, y+3, x+10, y+10], outline=0); d.rectangle([x+5, y+5, x+8, y+8], fill=0)
    for k in (4, 7, 9): d.line([x+k, y, x+k, y+2], fill=0); d.line([x+k, y+11, x+k, y+13], fill=0); d.line([x, y+k, x+2, y+k], fill=0); d.line([x+11, y+k, x+13, y+k], fill=0)
def icon_ram(x, y):   # RAM stick 18x12: bar + chip cells + contact fingers along the bottom
    d.rectangle([x, y+1, x+17, y+9], outline=0)
    for k in range(4): d.rectangle([x+2+k*4, y+3, x+4+k*4, y+6], fill=0)
    for k in range(6): d.line([x+2+k*3, y+10, x+2+k*3, y+12], fill=0)
def icon_clock(x, y): # clock 14x14
    d.ellipse([x, y, x+13, y+13], outline=0); d.line([x+7, y+3, x+7, y+7], fill=0); d.line([x+7, y+7, x+10, y+9], fill=0)
icon_cpu(130, hy); icon_ram(224, hy+1); icon_temp(315, hy-1); icon_clock(W-37, hy)

y = TOP + 20; ROW = 32
for i, (n, cpu, mem, t, up) in enumerate(servers):
    ry = y + i*ROW
    txt(8, ry+23, n, f(18))
    meter(113, ry+9, 48, 12, cpu); txt(165, ry+22, f"{cpu}%", f(14))
    meter(209, ry+9, 48, 12, mem); txt(261, ry+22, f"{mem}%", f(14))
    txt(305, ry+22, f"{t}°C", f(14))
    txt(W-8, ry+22, up.replace("天", "d") if EN else up, f(14), anchor="rs")
y += 4*ROW
d.line([0, y, W, y], fill=0); y += 5

# ---------- Claude, 3 rows x 24 ----------
limits = [("5 小时窗口", 36, "02:19"), ("本周全部", 5, "周三 05:59"), ("本周 · Fable", 9, "周三 05:59")]
if EN: limits = [("5h window", 36, "02:19"), ("Week · all", 5, "Wed 05:59"), ("Wk · Fable", 9, "Wed 05:59")]
for i, (l, pct, r) in enumerate(limits):
    yy = y + i*24
    if i == 0:
        import math
        cx, cy, S = 8 + 8, yy + 12, 16   # 16px starburst
        for k in range(12):
            ang = math.radians(k * 30 + 15); ro = S * (0.48 if k % 2 == 0 else 0.36); ri = S * 0.10
            d.line([(cx + ri*math.cos(ang), cy + ri*math.sin(ang)), (cx + ro*math.cos(ang), cy + ro*math.sin(ang))], fill=0, width=2)
        txt(30, yy+18, "Claude", f(14))
    txt(86, yy+18, l, f(14))
    meter(174, yy+7, 76, 12, pct)
    txt(256, yy+18, f"{pct}%", f(14))
    txt(W-8, yy+18, L(f"{r} 重置", f"resets {r}"), f(14), anchor="rs")
y += 3*24 + 1
d.line([0, y, W, y], fill=0)

# ---------- Status line ----------
txt(8, H-7, L("更新 16:25", "Upd 16:25"), f(14))   # inside the interactive window the key hints also appear on the right

args = [a for a in sys.argv[1:] if not a.startswith("--")]
out = args[0] if args else ("preview_en.png" if EN else "preview.png")
ImageOps.expand(img.convert("L").resize((W*2, H*2), Image.NEAREST), border=3, fill=0).save(out)   # add a black border so the images stay distinguishable side by side in the README
print("saved", out)
