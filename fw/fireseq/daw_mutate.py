"""Mutation check for the v0.31d DAW page's pure code (ctl.c fader sets + 1A-1E, uni.c PERFORM cycle, screens.c rows):
undo / break each rule in a temp copy; its host test (ctl_test / uni_test / screens_test) must turn red.
    python3 daw_mutate.py ."""
import os, shutil, subprocess, sys, tempfile
src = sys.argv[1]
d = tempfile.mkdtemp()
for f in os.listdir(src):
    if f.endswith((".c", ".h")):
        shutil.copy(os.path.join(src, f), d)
TESTS = {
    "ctl.c": ["gcc", "-O1", "-Wall", "-I.", "ctl_test.c", "ctl.c", "-lm", "-o", "t"],
    "uni.c": ["gcc", "-O1", "-Wall", "-I.", "-DUNI_TEST", "uni_test.c", "uni.c", "-o", "t"],
    "screens.c": ["gcc", "-O1", "-Wall", "-I.", "screens_test.c", "screens.c", "draw.c", "-o", "t"],
}
M = [
 ("ctl.c", "    return edge > 0 ? 0 : max_top;", "    return edge > 0 ? max_top : 0;"),                                # v0.31e: ends swapped
 ("ctl.c", "    return edge > 0 ? 0 : max_top;", "    return 0;"),                                                    # v0.31e: always the top
 ("ctl.c", "    int rows = n + (add_row && n < cap ? 1 : 0);", "    int rows = n + (n < cap ? 1 : 0);"),             # DAW gets a + row
 ("ctl.c", "    int rows = n + (add_row && n < cap ? 1 : 0);", "    int rows = n + (add_row ? 1 : 0);"),              # + row even at 32
 ("ctl.c", "    if (d && t == was) return d < 0 ? -1 : 1;", "    if (t == was) return d < 0 ? -1 : 1;"),           # d = 0 is a bank
 ("ctl.c", "    if (d && t == was) return d < 0 ? -1 : 1;", "    if (d && t == was) return d < 0 ? 1 : -1;"),      # directions swapped
 ("ctl.c", "    if (d && t == was) return d < 0 ? -1 : 1;", "    if (d && t != was + d) return d < 0 ? -1 : 1;"),  # partial move = bank
 ("ctl.c", "    if (t > max_top) t = max_top;\n    if (t < 0) t = 0;", "    if (t < 0) t = 0;\n    if (t > max_top) t = max_top + 1;"),
 ("ctl.c", "    if (val[f] == v) return 0;", "    if (0) return 0;"),                                               # resend unchanged
 ("ctl.c", "    v = v < 0 ? 0 : v > 127 ? 127 : v;\n    if (val[f] == v) return 0;", "    if (val[f] == v) return 0;"),   # no clamp
 ("ctl.c", "case 0x1A: if (n == 2 && p[1] <= CTL_DAW_ROWS)", "case 0x1A: if (n == 2 && p[1] <= 127)"),
 ("ctl.c", "case 0x1B: if (n >= 7 && p[1] < CTL_DAW_ROWS)", "case 0x1B: if (n >= 6 && p[1] < CTL_DAW_ROWS)"),
 ("ctl.c", "case 0x1B: if (n >= 7 && p[1] < CTL_DAW_ROWS)", "case 0x1B: if (n >= 7 && p[1] <= CTL_DAW_ROWS)"),
 ("ctl.c", "h.len = (unsigned)((p[2] & 0x7F) << 7 | (p[3] & 0x7F)); }", "h.len = (unsigned)(p[2] << 7 | p[3]); }"),
 ("ctl.c", "case 0x1C: if (n == 4 && p[1] < CTL_DAW_ROWS)", "case 0x1C: if (n >= 4 && p[1] < CTL_DAW_ROWS)"),
 ("ctl.c", "    for (int r = n; r < CTL_DAW_ROWS; r++) {", "    for (int r = n + 1; r < CTL_DAW_ROWS; r++) {"),
 ("ctl.c", "        d->val[r] = 0; d->fine[r] = 0; d->rgb[r][0]", "        d->fine[r] = 0; d->rgb[r][0]"),
 ("ctl.c", "    if (k > CTL_DAW_NAME) k = CTL_DAW_NAME;", ""),
 ("ctl.c", "(char)(ch >= 0x20 && ch <= 0x7E ? ch : '?')", "(char)(ch >= 0x20 ? ch : '?')"),
 ("ctl.c", "    for (unsigned i = k; i <= CTL_DAW_NAME; i++) d->name[row][i] = 0;", "    d->name[row][k] = 0;"),
 ("ctl.c", "    ctl_daw_level(d, row, (data[3] & 0x7F) << 7 | (data[4] & 0x7F));", "    ctl_daw_level(d, row, data[3] << 7);"),   # v0.31f low bits lost
 ("ctl.c", "    fine = fine < 0 ? 0 : fine > 16383 ? 16383 : fine;", ""),                                          # v0.31f no clamp
 ("ctl.c", "    d->val[row] = (uint8_t)(fine >> 7);", "    d->val[row] = (uint8_t)(fine >> 6);"),                       # v0.31f pads wrong
 ("ctl.c", "    return (level * 16383 + 63) / 127;", "    return level << 7;"),                                         # v0.31f tap 127 != top
 ("ctl.c", "    return dt_ms >= 120 ? 1 : dt_ms >= 60 ? 8 : dt_ms >= 30 ? 32 : 128;", "    return 2;"),               # v0.31f coarse knob
 ("ctl.c", "out[8] = (uint8_t)(fine & 0x7F);", "out[8] = 0;"),                                                     # v0.31f 1D loses low bits
 ("ctl.c", "out[7] = (uint8_t)(count < 1 ? 1 : count > 127 ? 127 : count);", "out[7] = 1;"),                      # v0.31f count dropped
 ("ctl.c", "    if (row < 0 || row >= CTL_DAW_ROWS || len < 5) return;", "    if (row < 0 || row > CTL_DAW_ROWS || len < 5) return;"),
 ("ctl.c", "(uint8_t)(mx ? (c[i] * 7 + mx / 2) / mx : 6)", "(uint8_t)(mx ? (c[i] * 7) / mx : 6)"),
 ("ctl.c", "(uint8_t)(mx ? (c[i] * 7 + mx / 2) / mx : 6)", "(uint8_t)(mx ? (c[i] * 7 + mx / 2) / mx : 0)"),
 ("ctl.c", "(uint8_t)(mx ? (c[i] * 7 + mx / 2) / mx : 6)", "(uint8_t)(c[i] * 7 / 127)"),
 ("ctl.c", "out[5] = 0x1D;", "out[5] = 0x1C;"),
 ("ctl.c", "out[6] = (uint8_t)(dir > 0 ? 1 : 0);", "out[6] = (uint8_t)(dir < 0 ? 1 : 0);"),
 ("uni.c", "    if (mode == m_fader) return has_daw ? m_daw : before;", "    if (mode == m_fader) return m_daw;"),   # v0.38: the rule moved to FADERS
 ("uni.c", "    if (mode == m_fader) return has_daw ? m_daw : before;", "    if (mode == m_fader) return before;"),
 ("uni.c", "    if (mode == m_daw) return before;\n", ""),
 ("screens.c", "draw_text_n(0, y, name[r], SCR_FADER_NAME, 1);", "draw_text(0, y, name[r], 1);"),
 ("screens.c", "            else if (name) { o = put_str(b, \"row \"); put_int(o, f[r].num); draw_text(0, y, b, 1); }\n", ""),
 ("screens.c", "void scr_faders(const scr_fader_t f[4]) { scr_fader_rows(f, 0); }", "void scr_faders(const scr_fader_t f[4]) { static const char *const e[4] = {\"\", \"\", \"\", \"\"}; scr_fader_rows(f, e); }"),
]
red = 0
for fn, a, b in M:
    p = os.path.join(d, fn); s = open(p, encoding="utf-8", newline="").read()
    sn = s.replace("\r\n", "\n")
    if a not in sn:
        print("NOT APPLIED:", a[:60]); continue
    open(p, "w", encoding="utf-8", newline="").write(sn.replace(a, b, 1))
    ok = True
    if subprocess.run(TESTS[fn], cwd=d, capture_output=True).returncode == 0:
        r = subprocess.run(["./t"], cwd=d, capture_output=True, text=True)
        ok = r.returncode != 0
    red += ok
    print("red  " if ok else "GREEN", fn, a[:60].replace("\n", " "))
    open(p, "w", encoding="utf-8", newline="").write(s)
print(f"{red}/{len(M)} mutations red")
shutil.rmtree(d)
sys.exit(0 if red == len(M) else 1)
