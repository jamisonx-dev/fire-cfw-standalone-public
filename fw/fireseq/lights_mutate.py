#!/usr/bin/env python3
"""Mutation check for lights.c: each deliberate break below must turn the lights tests red.
    python3 lights_mutate.py [path/to/orc1_fire.py]      (run on the Pi: needs gcc)
"Red" = lights_test fails, or (when the ORC's orc1_fire.py is given) `lights_test dump | lights_vs_orc.py` fails.
Without that path the effect maths is only checked by lights_test, so some mutations below are expected to show
NOT CAUGHT; give the path (on the Pi: ~/orc/orc1/orc1_fire.py) for a full run.
Works on copies in a temporary folder next to this script (removed afterwards); the real files are never touched.
A mutation whose anchor text is not found exactly once is NOT counted as caught: it is reported and fails the run.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = ("lights.c", "lights.h", "lights_tab.h", "lights_test.c", "lights_vs_orc.py")
ORC = sys.argv[1] if len(sys.argv) > 1 else None

MUTS = [  # (name, old, new) in lights.c
    # review 4.10: the five that used to survive
    ("whole look: no peak dot", "else if (pk > rows && fb == pk - 1) hsv(", "else if (0) hsv("),
    ("host went quiet: the last level is never let go", "if (host_age > 0.5f) host_tgt[c]", "if (0) host_tgt[c]"),
    ("HOST_PEAK_HOLD 0 (the dot falls at once)", "#define HOST_PEAK_HOLD 0.4f", "#define HOST_PEAK_HOLD 0.0f"),
    ("TRACKS: the peak dot does not fall behind the bar", "hit_peak[c] = fmaxf_(hit_bar[c], hit_peak[c] - dt * 0.45f);", "hit_peak[c] = hit_bar[c];"),
    ("HOST_UP_TAU = the falling one (slow rise)", "#define HOST_UP_TAU 0.06f", "#define HOST_UP_TAU 0.16f"),
    # more of the bars
    ("host went quiet: let go at once", "if (host_age > 0.5f) host_tgt[c]", "if (host_age > 0.0f) host_tgt[c]"),
    ("HOST_PEAK_HOLD doubled", "#define HOST_PEAK_HOLD 0.4f", "#define HOST_PEAK_HOLD 0.8f"),
    ("HOST_UP_TAU halved (rise too fast)", "#define HOST_UP_TAU 0.06f", "#define HOST_UP_TAU 0.03f"),
    ("HOST_DOWN_TAU = the rising one (fast fall)", "#define HOST_DOWN_TAU 0.16f", "#define HOST_DOWN_TAU 0.06f"),
    ("HOST_PEAK_FALL 0 (the dot never falls)", "#define HOST_PEAK_FALL 0.3f", "#define HOST_PEAK_FALL 0.0f"),
    ("HOST_PEAK_FALL doubled", "#define HOST_PEAK_FALL 0.3f", "#define HOST_PEAK_FALL 0.6f"),
    ("TRACKS: the peak dot falls slower", "hit_peak[c] - dt * 0.45f", "hit_peak[c] - dt * 0.3f"),
    ("TRACKS: the bar falls at half speed", "hit_bar[c] - dt * 1.6f", "hit_bar[c] - dt * 0.8f"),
    ("TRACKS: a soft hit lowers an accent", "if (h > hit_bar[c]) hit_bar[c] = h;", "hit_bar[c] = h;"),
    ("TRACKS: tracks 16-23 not folded onto columns 0-7", "int c = track & 15;", "int c = track > 15 ? 0 : track;"),
    ("TRACKS: track 24 accepted", "if (track < 0 || track > 23 || lv < 1) return;", "if (track < 0 || track > 24 || lv < 1) return;"),
    ("TRACKS: a normal hit is as tall as an accent", "lv == 2 ? 0.7f", "lv == 2 ? 1.0f"),
    ("snappy: falls twice as fast", "host_bar[c] - dt * 1.2f", "host_bar[c] - dt * 2.4f"),
    ("snappy: a higher level does not raise the bar", "if (h > host_bar[c]) host_bar[c] = h;", "(void)0;"),
    ("smooth: a higher level jumps the bar (the old ratchet)", "if (snappy()) {                             /* snappy: the bar", "if (1) {                             /* snappy: the bar"),
    ("hysteresis: turn-on at the half", "(float)(cur + 1) - 0.35f", "(float)(cur + 1) - 0.5f"),
    ("hysteresis: turn-off at the half", "(float)cur - 0.65f", "(float)cur - 0.5f"),
    ("snappy/smooth: no peak dot", "} else if (peak > 0.05f && (int)(peak * (float)LT_ROWS - 0.001f) == fb) {", "} else if (0) {"),
    ("heat: no peak dot", "        else if (peak > 0.05f && (int)(peak * (float)LT_ROWS - 0.001f) == fb) hsv(", "        else if (0) hsv("),
    ("partly covered top pad as bright as a full one", "0.2f + 0.7f * cover", "0.9f"),
    ("heat: hot tip instead of hot base", "float w = heat_flip ?", "float w = !heat_flip ?"),
    ("heat: HEAT_DEPTH 1 pad", "#define HEAT_DEPTH 3.2f", "#define HEAT_DEPTH 1.0f"),
    ("bar look out of range not clamped high", "st > 4 ? 4 : st", "st > 4 ? 0 : st"),
    # the shows and their maths (most need the orc1_fire.py comparison)
    ("fsin: x^9 term dropped", "x2 * 2.7557319e-6f", "x2 * 0.0f"),
    ("fsin: one-part 2 pi (large arguments drift)", "    x -= (float)k * 0.0019353072f;\n", "\n"),
    ("hsv: sector 1 colours swapped", "case 1: r = q; g = v; b = p;", "case 1: r = v; g = q; b = p;"),
    ("pmod: a negative remainder not wrapped", "    if (r < 0) r += m;\n", "\n"),
    ("PLASMA: no square root", "fsqrt(fc * fc + fr * fr + 0.5f)", "(fc * fc + fr * fr + 0.5f)"),
    ("RIPPLE: no square root", "dist = fsqrt(dx * dx + dy * dy);", "dist = (dx * dx + dy * dy);"),
    ("SPARKLE: x^8 is x^4", "static float pow8(float x) { x *= x; x *= x; return x * x; }", "static float pow8(float x) { x *= x; return x * x; }"),
    ("COMET: snake rows not reversed", "((row & 1) ? (LT_COLS - 1 - col) : col)", "col"),
    ("SCANNER: no bounce", "float x = p > (float)(LT_COLS - 1) ? 2.0f * (float)(LT_COLS - 1) - p : p;", "float x = p;"),
    ("speed factor: octave step 3x", "for (; oct > 0; oct--) f *= 2.0f;", "for (; oct > 0; oct--) f *= 3.0f;"),
    ("text: scrolls the wrong way", "int m = (p + col - LT_COLS + n * 4) % n;", "int m = (p - col + LT_COLS + n * 4) % n;"),
    ("text: lower case not raised", "if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);", "(void)0;"),
    ("text: not cut at 32 characters", "for (int i = 0; i < n && k < LT_TEXT_MAX; i++) {", "for (int i = 0; i < n && k < LT_TEXT_MAX + 4; i++) {"),
    ("level: not squared", "float y = x * x * 7.0f;", "float y = x * 7.0f;"),
    ("level: smooth threshold moves every frame", "float th = ((float)B[(unsigned)(row * 4 + col) & 15u] + 0.5f) / 16.0f;",
     "float th = ((float)B[(unsigned)(row * 4 + col + (int)frame) & 15u] + 0.5f) / 16.0f;"),
    ("level: smooth = plain rounding", "if (frac > th) L++;", "if (frac >= 0.5f) L++;"),
]


def run(d):
    b = subprocess.run(["gcc", "-O1", "-Wall", "-Wextra", "-I.", "lights_test.c", "lights.c", "-lm", "-o", "lights_test"],
                       cwd=d, capture_output=True, text=True)
    if b.returncode:
        return "build", b.stderr.strip().splitlines()[:3]
    t = subprocess.run(["./lights_test"], cwd=d, capture_output=True, text=True)
    why = []
    if t.returncode:
        why.append(f"lights_test: {t.stdout.count('FAIL')} check(s) fail" + (", count drift" if "EXPECTED differs" in t.stdout else ""))
    if ORC:
        dump = subprocess.run(["./lights_test", "dump"], cwd=d, capture_output=True, text=True)
        v = subprocess.run([sys.executable, "-B", "lights_vs_orc.py", ORC], cwd=d, input=dump.stdout, capture_output=True, text=True)
        if v.returncode:
            why.append("vs ORC: " + (v.stdout.strip().splitlines() or ["?"])[-1])
    return ("red" if why else "green"), why


def main():
    if ORC and not os.path.isfile(ORC):
        sys.exit(f"no such file: {ORC}")
    d = tempfile.mkdtemp(prefix="lights_mut_", dir=HERE)
    try:
        for f in FILES:
            shutil.copy(os.path.join(HERE, f), d)
        p = os.path.join(d, "lights.c")
        orig = open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
        state, why = run(d)                                 # the unmutated copy must be green first
        if state != "green":
            print("the UNMUTATED code is not green:", state, why)
            return 1
        red = problems = 0
        for name, old, new in MUTS:
            n = orig.count(old)
            if n != 1:
                print(f"NOT APPLIED   {name}: anchor found {n}x")
                problems += 1
                continue
            mutated = orig.replace(old, new)
            open(p, "w", encoding="utf-8", newline="").write(mutated)
            print(f"APPLIED       {name}")
            state, why = run(d)
            if state == "red":
                red += 1
                print(f"   RED        {'; '.join(why)}")
            elif state == "build":
                problems += 1
                print(f"   BUILD FAILED (not a test result): {' / '.join(why)}")
            else:
                problems += 1
                print("   GREEN (NOT CAUGHT)")
        open(p, "w", encoding="utf-8", newline="").write(orig)
        print(f"{red}/{len(MUTS)} mutations red" + ("" if ORC else " (without the ORC comparison)")
              + ("" if not problems else f", {problems} problem(s)"))
        return 1 if problems else 0
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
