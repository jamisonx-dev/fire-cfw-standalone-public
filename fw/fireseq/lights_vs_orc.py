"""Compare lights.c's effects with the ORC's real Python (orc1_fire.py), pad by pad.

The Python methods are pulled out of the source file by ast and run with a stub that records what
_send_pads gets, so what is compared is the code that actually drives the Fire today, not a copy.
  ./lights_test dump | python3 lights_vs_orc.py path/to/orc1_fire.py
Python gives 0-127 (truncated), C gives 0-255 (rounded): equal when |c*127/255 - py| <= 1.6.
Exits 1 on any difference AND unless exactly 4928 pads were compared (empty or short input fails).
"""
import ast, math, sys

src = open(sys.argv[1], encoding="utf-8").read()
tree = ast.parse(src)
fh = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "FireHandler")
want = {"_render_pl_plasma", "_render_pl_sparkle", "_render_pl_sweep", "_render_pl_flame", "_render_pl_beat",
        "_render_pl_sparse", "_render_pl_rain", "_render_pl_ripple", "_render_pl_comet", "_render_pl_scanner",
        "_render_pl_wave", "_render_pl_vu"}
order = ["plasma", "sparkle", "sweep", "flame", "beat", "sparse", "rain", "ripple", "comet", "scanner", "wave", "vu"]
funcs = [n for n in fh.body if isinstance(n, ast.FunctionDef) and n.name in want]
hsv = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "_hsv")
padi = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "pad_index")
ns = {"N_ROWS": 4, "N_COLS": 16, "math": math}
exec(compile(ast.fix_missing_locations(ast.Module([hsv, padi], [])), "orc", "exec"), ns)


class Stub:
    def _send_pads(self, cells):
        self.cells = cells


for f in funcs:
    exec(compile(ast.fix_missing_locations(ast.Module([ast.ClassDef("T", [], [], [f], [])], [])), "orc", "exec"), ns)
    setattr(Stub, f.name, ns["T"].__dict__[f.name])

ref = {}  # (mode, t) -> {(row, col): (r, g, b)}
rows = [l.split() for l in sys.stdin if l.strip() and l[0].isdigit()]
bad = worst = n = 0
for m, t, r, c, R, G, B in rows:
    m, r, c = int(m), int(r), int(c)
    key = (m, t)
    if key not in ref:
        s = Stub()
        getattr(s, "_render_pl_" + order[m])(float(t), math)
        ref[key] = {(i // 16, i % 16): (x, y, z) for i, x, y, z in s.cells}
    py = ref[key][(r, c)]
    got = (int(R), int(G), int(B))
    d = max(abs(got[i] * 127 / 255 - py[i]) for i in range(3))
    worst = max(worst, d)
    n += 1
    if d > 1.6:
        bad += 1
        if bad <= 12:
            print(f"DIFF mode {order[m]} t={t} pad({r},{c}): C {got} -> {[round(x*127/255,1) for x in got]} vs Python {py}")
print(f"{n} pads compared, {bad} differ by more than 1.6 (worst {worst:.2f} of 127)")
# lights_test dump prints 11 effects (12 minus the retired 11) x 7 times x 64 pads. Fewer means the dump broke
# (empty input, a crash part-way, an effect missing), and comparing nothing must not pass.
EXPECTED = 11 * 7 * 64
if n != EXPECTED:
    print(f"FAIL: expected {EXPECTED} pads, got {n} (is the input really `./lights_test dump`?)")
sys.exit(1 if bad or n != EXPECTED else 0)
