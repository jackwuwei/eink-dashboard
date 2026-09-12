"""Merge every CJK/symbol character used in the sources into charset.txt (bdfconv -m format).
Usage: python3 gen_charset.py"""
import re, glob
chars = set()
for p in ["../ui.cpp", "../i18n.h", "../i18n.cpp", "../lunar.cpp", "../eink-dashboard.ino", "../portal.cpp", "../net.cpp",
          "../server/app/weather.py", "../server/config.yaml"]:
    try: txt = open(p, encoding="utf-8").read()
    except OSError: continue
    if p.endswith(".yaml"): txt = "\n".join(l for l in txt.splitlines() if not l.strip().startswith("#"))
    chars |= {c for c in txt if ord(c) > 127}
# every weather-phenomenon string Seniverse can return
chars |= set("晴多云阴阵雨雷阵雨伴有冰雹小雨中雨大雨暴雨大暴雨特大暴雨冻雨雨夹雪阵雪小雪中雪大雪暴雪浮尘扬沙沙尘暴强沙尘暴雾霾风大风飓风热带风暴龙卷风冷热未知晴间多云局部东南西北级晴夜"
             "○●")
old = set()
for tok in open("charset.txt").read().strip().split(","):
    a, _, b = tok.partition("-"); a, b = int(a), int(b or a)
    old |= set(range(a, b + 1))
new = old | {ord(c) for c in chars}
codes = sorted(new); out = []; i = 0
while i < len(codes):
    j = i
    while j + 1 < len(codes) and codes[j + 1] == codes[j] + 1: j += 1
    out.append(str(codes[i]) if i == j else f"{codes[i]}-{codes[j]}"); i = j + 1
open("charset.txt", "w").write(",".join(out))
print(f"charset: {len(old)} -> {len(new)} chars (+{len(new - old)})")
