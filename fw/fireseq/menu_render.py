#!/usr/bin/env python3
"""Render every BROWSER menu page the way the Fire's OLED shows it, from the MENU table in main.c, with each row's
WIDEST value (so a page that fits here fits on the Fire). Also checks the text widths. Run on the Pi (gcc + Pillow):

    python3 menu_render.py [outdir]      -> menu_<page>.png (4x scale) + a width report; exit 1 if anything is too wide

Rows: the screen shows 6 at a time; with more, a scroll bar takes the right edge (rows end at x=121, else 125).
Font 6 px a character; a name starts at x=2; a value is right-aligned and must leave 2 px after the name."""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, "main.c"), encoding="utf-8").read().replace("\r\n", "\n")
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "menu_png")
os.makedirs(out, exist_ok=True)

arrays = {m.group(1): re.findall(r'"([^"]*)"', m.group(2))
          for m in re.finditer(r"static const char \*const (NM_\w+)\[[^\]]*\] = \{([^}]*)\}", SRC)}
titles = re.findall(r'"([^"]*)"', re.search(r"MENU_TITLE\[MENU_PAGES\] = \{(.*?)\};", SRC, re.S).group(1))
body = re.search(r"static const mitem_t MENU\[\] = \{(.*?)\n\};", SRC, re.S).group(1)
items = re.findall(r'\{"([^"]*)",\s*(\d+),\s*(MK_\w+),\s*([^,]+),\s*([^,]+),\s*([^,]+),', body)

WIDEST = {"MK_SUB": ">", "MK_TEMPO": "300 bpm", "MK_SWING": "75%", "MK_CLOCK": "off", "MK_KBANK": "4",
          "MK_KSEL": "4D", "MK_KFN": "CC 127", "MK_KCH": "ch 16", "MK_KMODE": "absolute", "MK_INFO": "",
          "MK_ACT": "", "MK_SPEED": "x4.00", "MK_EFFECT": "Scanner", "MK_ROOM": "6826"}
pages = {}
for name, page, kind, _v, _n, names in items:
    val = WIDEST.get(kind, "?")
    if kind == "MK_ENUM":
        val = max(arrays.get(names.strip(), ["?"]), key=len)
    pages.setdefault(int(page), []).append((name, val, kind))

bad = [f"title '{t}' is {len(t)} characters (max 10: room before 'BRW=back')" for t in titles if len(t) > 10]
for p, rows in sorted(pages.items()):
    right = 121 if len(rows) > 6 else 125
    for name, val, kind in rows:
        need = 2 + 6 * len(name) + 2 + 6 * len(val)
        if need > right - 2:
            bad.append(f"page {p} '{name}' + '{val}': {need} px > {right - 2}")

c = ['#include <stdio.h>', '#include "draw.h"', '#include "screens.h"', "int main(void) {"]
for p, rows in sorted(pages.items()):
    c.append("  { scr_menu_row_t r[] = {" + ", ".join('{"%s", "%s"}' % (n, v) for n, v, _ in rows) + "};")
    for top in range(0, max(1, len(rows) - 5), 6):          # one image per screenful
        sel = min(top + 2, len(rows) - 1) if top else 0
        c.append(f'    draw_clear(); scr_menu("{titles[p] if p < len(titles) else "?"}", "BRW=back", r, {len(rows)}, {sel}, 0, 0);')
        c.append(f'    {{ FILE *f = fopen("{out}/menu_{p:02d}_{top // 6}.raw", "wb"); for (int y = 0; y < FB_H; y++) '
                 'for (int x = 0; x < FB_W; x++) fputc((fb[(y >> 3) * FB_W + x] >> (y & 7) & 1) ? 255 : 0, f); fclose(f); }')
    c.append("  }")
c.append("  return 0; }")
d = tempfile.mkdtemp()
open(os.path.join(d, "r.c"), "w").write("\n".join(c))
subprocess.run(["gcc", "-O1", "-I", HERE, os.path.join(d, "r.c"), os.path.join(HERE, "screens.c"), os.path.join(HERE, "draw.c"),
                "-o", os.path.join(d, "r")], check=True)
subprocess.run([os.path.join(d, "r")], check=True)
from PIL import Image
for fn in sorted(os.listdir(out)):
    if fn.endswith(".raw"):
        im = Image.frombytes("L", (128, 64), open(os.path.join(out, fn), "rb").read()).resize((512, 256), Image.NEAREST)
        im.save(os.path.join(out, fn[:-4] + ".png"))
        os.remove(os.path.join(out, fn))
print(f"{sum(len(r) for r in pages.values())} items on {len(pages)} pages rendered to {out}")
print("\n".join(bad) if bad else "every row fits")
sys.exit(1 if bad else 0)
