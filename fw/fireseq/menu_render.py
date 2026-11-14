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
# v0.35 About rows: the widest each can be (crash.c crash_about_value; crash_test checks the 13-character cap, the short
# cause <= 10 and stage <= 9). The version is main.c's FW_VERSION, read from the source.
FW_VERSION = re.search(r'#define FW_VERSION "([^"]*)"', SRC).group(1)
WIDEST_ABOUT = {"ABOUT_VERSION": FW_VERSION, "ABOUT_BUILD": "ffffffff", "ABOUT_CAUSE": "UsageFault",
                "ABOUT_STAGE": "sequencer", "ABOUT_TOTAL": "4294967295", "ABOUT_UPTIME": "71582 min",
                "ABOUT_PC": "ffffffff", "ABOUT_LR": "ffffffff", "ABOUT_MISREADS": "4294967295"}
pages = {}
for name, page, kind, _v, _n, names in items:
    val = WIDEST.get(kind, "?")
    if kind == "MK_ENUM":
        val = max(arrays.get(names.strip(), ["?"]), key=len)
    if kind == "MK_ABOUT":
        val = WIDEST_ABOUT.get(_n.strip(), "?" * 13)
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
# v0.31 UNIVERSAL screens, drawn by the real uni.c + screens.c: the play screen, an empty page, every edit page
# (each pad type on both knob pages, base and SHIFT, every knob type). The header text is main.c's (render()).
u = ['#include <stdio.h>', '#include <string.h>', '#include "draw.h"', '#include "screens.h"', '#include "uni.h"',
     'static int bad;',
     'static void save(const char *n) { char p[512]; snprintf(p, sizeof p, "%s/uni_%s.raw", OUT, n); FILE *f = fopen(p, "wb");'
     ' for (int y = 0; y < FB_H; y++) for (int x = 0; x < FB_W; x++) fputc((fb[(y >> 3) * FB_W + x] >> (y & 7) & 1) ? 255 : 0, f); fclose(f); }',
     'static void hd(const char *ctx, const char *st) { scr_head_t h = {"PAT 3", 120, 50, 0, 1, 1, ctx, st}; draw_clear(); scr_header(&h); }',
     'static void lines4(char L[4][20]) { const char *lp[4] = {L[0], L[1], L[2], L[3]}; for (int i = 0; i < 4; i++) if (strlen(L[i]) > 19) { printf("too long: %s\\n", L[i]); bad = 1; } scr_lines(lp); }',
     'int main(void) {',
     '  { char lab[4][20]; const char *lp[4]; int val[4] = {100, 64, 0, 127}, used[4] = {1, 1, 0, 1};',
     '    uni_knob_t k[4] = {{UK_CC, 0, 74, 64}, {UK_PB, 15, 0, 64}, {UK_NONE, 0, 0, 0}, {UK_CC, 15, 127, 0}};',
     '    for (int i = 0; i < 4; i++) { uni_knob_row(k[i], 4 + i, lab[i]); lp[i] = lab[i]; if (strlen(lab[i]) > 13) bad = 1; }',
     '    hd("Univ", "P3/8 drums"); scr_uni_knobs(lp, val, used, 1); save("play");',
     '    hd("Univ", "P8/8 SHIFT longname"); scr_uni_knobs(lp, val, used, -1); save("play_shift"); }',
     '  { static const char *const E[4] = {"(empty page)", "set up on the ORC", "or ALT + tap a pad", "GRID < > = page"};',
     '    hd("Univ", "P1/8 "); scr_lines(E); save("empty"); }',
     '  char L[4][20], n[32];',
     '  for (int t = 0; t < UT_N; t++) for (int pg = 0; pg < 2; pg++) for (int ly = 0; ly < 2; ly++) {',
     '    uni_pad_t p = {(uint8_t)t, 15, (uint8_t)(t == UT_NOTE || t == UT_LATCH ? 61 : 127), 127, 127, 13};',
     '    uni_pad_lines(p, pg, ly, L); hd("Edit", ly ? "pad 4-16 SHIFT" : "pad 4-16"); lines4(L);',
     '    snprintf(n, sizeof n, "pad_t%d_p%d_%s", t, pg + 1, ly ? "shift" : "base"); save(n); }',
     '  for (int t = 0; t < UK_N; t++) { uni_knob_t k = {(uint8_t)t, 15, 127, 127}; uni_knob_lines(k, L); hd("Edit", "knob 4D"); lines4(L);',
     '    snprintf(n, sizeof n, "knob_t%d", t); save(n); }',
     # v0.31d the DAW page: the fader rows with the host's names (8 characters at most), first and last view of 9 rows
     '  { scr_fader_t f[4] = {{1, 1, 0, 100}, {1, 2, 0, 0}, {1, 3, 0, 64}, {1, 4, 0, 127}};',
     '    const char *nm[4] = {"Kick", "SNAREBUS", "", "Vox Lead"};',
     '    hd("DAW", "F1-4/9"); scr_fader_rows(f, nm); save("daw_top");',
     '    scr_fader_t g[4] = {{1, 6, 0, 90}, {1, 7, 0, 127}, {1, 8, 0, 33}, {1, 9, 0, 101}};',
     '    const char *nm2[4] = {"WWWWWWWWWWWW", "Pad 2", "Room L", "MASTER"};',
     '    hd("DAW", "F6-9/9"); scr_fader_rows(g, nm2); save("daw_bottom");',
     '    for (int y = 22; y < 62; y++) for (int x = 48; x < 60; x++) if (fb[(y >> 3) * FB_W + x] >> (y & 7) & 1) { printf("DAW name runs into the bar\\n"); bad = 1; y = 99; break; } }',
     '  return bad; }']
open(os.path.join(d, "u.c"), "w").write("\n".join(u).replace("OUT", '"' + out + '"'))
subprocess.run(["gcc", "-O1", "-I", HERE, os.path.join(d, "u.c"), os.path.join(HERE, "screens.c"), os.path.join(HERE, "draw.c"),
                os.path.join(HERE, "uni.c"), "-o", os.path.join(d, "u")], check=True)
if subprocess.run([os.path.join(d, "u")]).returncode:
    bad.append("a UNIVERSAL screen line is too long")
from PIL import Image
for fn in sorted(os.listdir(out)):
    if fn.endswith(".raw"):
        im = Image.frombytes("L", (128, 64), open(os.path.join(out, fn), "rb").read()).resize((512, 256), Image.NEAREST)
        im.save(os.path.join(out, fn[:-4] + ".png"))
        os.remove(os.path.join(out, fn))
print(f"{sum(len(r) for r in pages.values())} items on {len(pages)} pages rendered to {out}")
print("\n".join(bad) if bad else "every row fits")
sys.exit(1 if bad else 0)
