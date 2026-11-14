"""Mutation check for PAINT mode's rules (paint.c): break each rule in a temp copy; paint_test must turn red for every one.
    python3 -B paint_mutate.py .        (on the Pi: needs gcc)"""
import os, shutil, subprocess, sys, tempfile
src = sys.argv[1] if len(sys.argv) > 1 else "."
d = tempfile.mkdtemp(dir=os.path.expanduser("~"))
for f in ("paint.c", "paint.h", "paint_test.c"):
    shutil.copy(os.path.join(src, f), d)
M = [  # (old, new): one rule each
    ("return (cv[pad >> 1] >> ((pad & 1) * 4)) & 0x0F;", "return (cv[pad >> 1] >> ((pad & 1) * 4)) & 0x1F;"),     # reads the neighbour's bit
    ("    if (pad < 0 || pad >= PAINT_PADS) return 0;\n    return (cv", "    return (cv"),                     # off the grid read
    ("    if (pad < 0 || pad >= PAINT_PADS) return;\n    int sh", "    int sh"),                              # off the grid written
    ("((c & 0x0F) << sh)", "(c << sh)"),                                                                      # spills into the next pad
    ("(cv[pad >> 1] & ~(0x0F << sh))", "(cv[pad >> 1])"),                                                     # old colour not cleared
    ("int c = paint_get(cv, pad) == cur ? 0 : cur;", "int c = cur;"),                                         # no wipe on a re-tap
    ("int block = (pad / 16 / 2) * 8 + (pad % 16) / 2;", "int block = (pad / 16) * 8 + (pad % 16) / 2;"),       # blocks 1 row tall
    ("return (block + 1) % PAINT_COLS;", "return block % PAINT_COLS;"),                                       # eraser top left
    ("        if (palette && c == 0) v = PAINT_ERASER_RGB;\n", ""),                                            # the eraser invisible
    ("        if (palette && c != cur) v = (uint8_t)(v / 2);\n", ""),                                         # the pick not shown
    ("int c = palette ? paint_swatch(pad) : paint_get(cv, pad);", "int c = paint_get(cv, pad);"),            # no palette view
    ("        if (b->row < 0) { b->row", "        { b->row"),                                                 # a second button steals
    ("    if (b->row != row) return PB_NONE;", ""),                                                           # any release counts
    ("int r = b->done ? PB_NONE :", "int r ="),                                                               # a save's release loads too
    ("now - b->t >= PAINT_HOLD_MS ? PB_SAVE : PB_LOAD", "PB_LOAD"),                                           # a long hold without ticks loads
    ("    if (b->row < 0 || b->done || now - b->t < PAINT_HOLD_MS) return PB_NONE;", "    if (b->row < 0 || now - b->t < PAINT_HOLD_MS) return PB_NONE;"),   # saves every tick
    ("now - b->t < PAINT_HOLD_MS) return PB_NONE;", "now - b->t < PAINT_HOLD_MS / 2) return PB_NONE;"),     # half a second
    ("    if (row < 0 || row >= PAINT_SLOTS) return PB_NONE;", ""),                                           # slot 5
]
red = 0
p = os.path.join(d, "paint.c")
orig = open(p, encoding="utf-8").read().replace("\r\n", "\n")
for a, b in M:
    if a not in orig:
        print("NOT APPLIED:", a[:60]); continue
    open(p, "w", encoding="utf-8").write(orig.replace(a, b, 1))
    exe = os.path.join(d, "pt")
    bld = subprocess.run(["gcc", "-O2", "-Wall", "-I", d, "-fsanitize=bounds", "-fsanitize-undefined-trap-on-error",
                          os.path.join(d, "paint_test.c"), p, "-o", exe], capture_output=True, text=True)
    if bld.returncode:
        print("NO BUILD", a[:60]); continue           # (never counted red)
    ok = subprocess.run([exe], capture_output=True).returncode != 0
    red += ok
    print("red  " if ok else "GREEN", a[:70].replace("\n", " "))
open(p, "w", encoding="utf-8").write(orig)
print(f"{red}/{len(M)} mutations red")
shutil.rmtree(d)
sys.exit(0 if red == len(M) else 1)
