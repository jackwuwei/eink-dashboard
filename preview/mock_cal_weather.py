"""Calendar-page / weather-page preview (coordinates match ui.cpp's drawCalendarPage / drawWeatherPage exactly)
Usage: uvx --with pillow --with lunar_python python mock_cal_weather.py [--en]"""
import re, sys, calendar
from PIL import Image, ImageOps
from u8g2font import fonts, Draw
from lunar_python import Solar
EN = "--en" in sys.argv
def L(zh, en): return en if EN else zh
SFX = "_en" if EN else ""
W, H, TOP = 400, 300, 50
F = fonts()                       # the firmware's own u8g2 bitmap fonts: 14/16/18 px Noto (fonts_noto.c) + logisoso38 (time)
def f(sz): return F[sz]           # F_SM / F_TXT / F_NAME in ui.cpp
def fd(sz): return F[sz]          # 38 → F_TIME; 14 → the date is plain F_SM on the device
def new():
    img = Image.new("1", (W, H), 1); d = Draw(img); return img, d
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
def status(d, left, hint=True):
    d.text((8, H-7), left, font=f(14), fill=0, anchor="ls")
    if hint: d.text((W-8, H-7), L("[2]下页 [3]上页", "[2]Next [3]Prev"), font=f(14), fill=0, anchor="rs")

# weather icons: parse the bitmaps straight out of weather_icons.h (45x45, 6 bytes per row, 1 = white)
ICONS = {}
for m in re.finditer(r"WX_(\w+)\[\] PROGMEM = \{([^}]*)\}", open("../weather_icons.h").read()):
    ICONS[m.group(1)] = [int(x, 16) for x in m.group(2).split(",")]
CODE2ICON = {0:"qt",1:"qt_ws",2:"qt",3:"qt_ws",4:"dy",5:"dy",6:"dy_ws",7:"dy",8:"dy_ws",9:"yt",10:"zheny",11:"lzy",12:"lzybbb",13:"xy",14:"zhongy",15:"dayu",16:"by",17:"dby",18:"tdby",19:"dongy",20:"yjx",21:"zhenx",22:"xx",23:"zhongx",24:"dx",25:"bx",26:"fc",27:"ys",28:"scb",29:"scb",30:"w",31:"m",32:"f",33:"f",34:"jf",35:"rdfb",36:"ljf",37:"dongy",38:"rdfb",99:"wz"}
def icon(d, x, y, code):
    data = ICONS[CODE2ICON.get(code, "wz")]
    for r in range(45):
        for c in range(45):
            if not (data[r*6 + c//8] >> (7 - c % 8)) & 1: d.point((x+c, y+r), fill=0)

# ---- Calendar page ----
def cell_text(y, m, dd):
    l = Solar.fromYmd(y, m, dd).getLunar()
    fest = l.getFestivals() or Solar.fromYmd(y, m, dd).getFestivals()
    if fest: return fest[0]
    if l.getJieQi(): return l.getJieQi()
    return l.getMonthInChinese() + "月" if l.getDay() == 1 else l.getDayInChinese()
def calendar_page(year, month, today):
    img, d = new(); topbar(d)
    COLW, X0 = 57, 0
    for i, n in enumerate(["Mon","Tue","Wed","Thu","Fri","Sat","Sun"] if EN else list("一二三四五六日")):
        d.text((X0 + COLW*i + COLW//2 + 1, TOP+16), n, font=f(16), fill=0, anchor="ms")
    d.line([0, TOP+21, W, TOP+21], fill=0)
    first_wd = (calendar.weekday(year, month, 1))       # 0 = Monday
    ndays = calendar.monthrange(year, month)[1]
    ROW, Y0 = 32, TOP+23
    for day in range(1, ndays+1):
        idx = first_wd + day - 1; r, c = divmod(idx, 7)
        cx, ry = X0 + COLW*c + COLW//2 + 1, Y0 + ROW*r
        if day == today:
            d.ellipse([cx-11, ry-1, cx+11, ry+21], fill=0)
            d.text((cx, ry+15), str(day), font=f(16), fill=1, anchor="ms")
        else:
            d.text((cx, ry+15), str(day), font=f(16), fill=0, anchor="ms")
        if not EN: d.text((cx, ry+30), cell_text(year, month, day), font=f(14), fill=0, anchor="ms")   # the lunar line is only shown in the Chinese UI
    l = Solar.fromYmd(year, month, today).getLunar()
    MON = ["January","February","March","April","May","June","July","August","September","October","November","December"]
    status(d, f"{MON[month-1]} {year}" if EN else f"{year}年{month}月   农历{l.getMonthInChinese()}月{l.getDayInChinese()}")
    return img

# ---- Weather page ----
WX = dict(city="深圳", t=32, c=9, txt="阴", lo=26, hi=32, h=70, wd="西南风", ws="5级", sr="06:05", ss="18:44", tip="天气炎热，注意防晒补水", up="15:30",
          d=[dict(d="08/30", w="日", c=10, t="阵雨", lo=25, hi=30), dict(d="08/31", w="一", c=11, t="雷阵雨", lo=25, hi=29),
             dict(d="09/01", w="二", c=11, t="雷阵雨", lo=25, hi=30), dict(d="09/02", w="三", c=13, t="小雨", lo=25, hi=30)])
if EN:   # the city name and the tip come from the server and are not translated by the firmware, so the English
         # screenshots use a separate set of English sample data
    WX = dict(WX, city="Shenzhen", tip="Hot today - sunscreen and plenty of water")
WX_EN = ("Sunny Clear Fair Fair Cloudy P.Cloudy P.Cloudy M.Cloudy M.Cloudy Overcast Shower T-storm Hail Lt~Rain Rain Hvy~Rain "
         "Storm Storm+ Storm++ Ice~Rain Sleet Snow~Sh. Lt~Snow Snow Hvy~Snow Blizzard Dust Sand Duststorm Sandstorm Fog Haze "
         "Windy Gale Hurricane Trop.Storm Tornado Cold Hot").split()
WX_EN_SHORT = ("Sunny Clear Fair Fair Cloudy P.Cldy P.Cldy M.Cldy M.Cldy Ovcast Shwr T-stm Hail Rain- Rain Rain+ "
               "Storm Storm+ Deluge IceRn Sleet SnwShr Snow- Snow Snow+ Blizrd Dust Sand DstStm SndStm Fog Haze "
               "Windy Gale Hurr. TropSt Tornado Cold Hot").split()
def wx(code, zh, short=False):   # matches WX_EN / WX_EN_SHORT in i18n.cpp
    if not EN or code > 38: return zh
    return (WX_EN_SHORT if short else WX_EN)[code].replace("~", " ")
def weather_page(w):
    img, d = new(); topbar(d)
    d.text((8, TOP+22), w["city"], font=f(16), fill=0, anchor="ls")
    d.text((6, TOP+68), str(w["t"]), font=fd(38), fill=0, anchor="ls")
    tw = d.textlength(str(w["t"]), font=fd(38))
    d.text((6+tw+4, TOP+68), "°C", font=f(18), fill=0, anchor="ls")
    icon(d, 8, TOP+76, w["c"]); d.text((60, TOP+106), wx(w["c"], w["txt"]), font=f(16), fill=0, anchor="ls")
    d.text((8, TOP+134), f'{w["lo"]}°C / {w["hi"]}°C', font=f(14), fill=0, anchor="ls")
    wind = L(f'{w["wd"]} {w["ws"]}', 'SW ' + w["ws"].replace("级", "") + ' Bft')
    d.text((8, TOP+154), L(f'湿度 {w["h"]}%   ', f'Humidity {w["h"]}%   ') + wind, font=f(14), fill=0, anchor="ls")
    d.text((8, TOP+174), L(f'日出 {w["sr"]}  |  日落 {w["ss"]}', f'Sunrise {w["sr"]}  |  Sunset {w["ss"]}'), font=f(14), fill=0, anchor="ls")
    for i, day in enumerate(w["d"][:4]):
        cx = 225 + 50*i
        WD_EN = ["Sun","Mon","Tue","Wed","Thu","Fri","Sat"]   # the sample date 8/29 is a Saturday
        head = WD_EN[(6 + i + 1) % 7] if EN else ("明天" if i == 0 else "周" + day["w"])
        d.text((cx, TOP+26), head, font=f(14), fill=0, anchor="ms")
        d.text((cx, TOP+44), day["d"], font=f(14), fill=0, anchor="ms")
        icon(d, cx-22, TOP+50, day["c"])
        d.text((cx, TOP+112), wx(day["c"], day["t"], True), font=f(14), fill=0, anchor="ms")
        d.text((cx, TOP+130), f'{day["lo"]}/{day["hi"]}°', font=f(14), fill=0, anchor="ms")
    d.line([20, TOP+186, W-20, TOP+186], fill=0)
    d.text((W//2, TOP+212), w["tip"], font=f(16), fill=0, anchor="ms")
    status(d, L(f'天气   更新 {w["up"]}', f'Weather   upd {w["up"]}'))
    return img

if __name__ == "__main__":
    a = ImageOps.expand(calendar_page(2026, 8, 29).convert("L").resize((W*2, H*2), Image.NEAREST), border=3, fill=0); a.save(f"page_calendar{SFX}.png")
    b = ImageOps.expand(weather_page(WX).convert("L").resize((W*2, H*2), Image.NEAREST), border=3, fill=0); b.save(f"page_weather{SFX}.png")
    sheet = Image.new("L", (a.width, a.height*2+20), 128); sheet.paste(a, (0, 0)); sheet.paste(b, (0, a.height+20)); sheet.save(f"pages_cal_weather{SFX}.png")
    print(f"saved pages_cal_weather{SFX}.png")
