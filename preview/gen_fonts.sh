#!/bin/zsh
# Regenerate fonts_noto.c (needs brew install otf2bdf and u8g2's bdfconv)
set -e
cd "$(dirname "$0")/.."
BDFCONV=${BDFCONV:-bdfconv}
{ echo '// Generated from Noto Sans CJK SC (OFL) with otf2bdf + u8g2 bdfconv; charset in preview/charset.txt. Regenerate: preview/gen_fonts.sh'; echo '#include <u8g2_fonts.h>'; } > fonts_noto.c
CITY=$(python3 -c "print(','.join(str(ord(c)) for c in sorted(set(open('preview/charset_city.txt').read()) - {'\n'})))")
for sz in 14 16 18; do
  otf2bdf -p $sz -r 72 -o /tmp/noto$sz.bdf preview/NotoSansCJKsc-Regular.otf
  M="$(cat preview/charset.txt)"; [ $sz = 16 ] && M="$M,$CITY"     # city-name glyphs only go into 16px (weather page)
  $BDFCONV -f 1 -m "$M" -n u8g2_font_noto${sz}_hd /tmp/noto$sz.bdf -o /tmp/noto$sz.c
  LC_ALL=C sed "/^#include/d" /tmp/noto$sz.c >> fonts_noto.c
done
