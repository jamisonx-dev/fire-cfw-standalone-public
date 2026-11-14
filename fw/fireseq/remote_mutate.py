"""Mutation check for remote_test.c: break each rule of remote.c in a temp copy; every one must turn the test red.
    python3 remote_mutate.py .        (on the Pi: needs gcc)"""
import os, shutil, subprocess, sys, tempfile
src = sys.argv[1] if len(sys.argv) > 1 else "."
d = tempfile.mkdtemp(dir=os.path.expanduser("~"))
for f in ("remote.c", "remote.h", "remote_test.c"):
    shutil.copy(os.path.join(src, f), d)
M = [  # (old, new)
    ("    if (!r->armed) return RIN_NOT_ARMED;\n", "\n"),                                              # armed gate
    ("            if (q_free(r) < held_count(r) + 2) return RIN_FULL;", "            if (!q_free(r)) return RIN_FULL;"),   # the release reserve
    ("        if (rin_held(r, i) == down) return RIN_OK;", "        if (0) return RIN_OK;"),               # no double press
    ("    release_all(r);\n    r->armed = 0;\n    return 1;", "    r->armed = 0;\n    return 1;"),      # timeout releases
    ("        else { if (r->armed) release_all(r); r->armed = 0; }", "        else { r->armed = 0; }"),  # disarm releases
    ("    if (!r->armed || now - r->last_ms < RIN_QUIET_MS) return 0;", "    if (!r->armed || (int32_t)now - (int32_t)r->last_ms < 0) return 0;"),  # quiet time
    ("        if (d[0]) { r->armed = 1; r->last_ms = now; }", "        if (d[0]) { r->armed = 1; }"),   # keepalive
    ("    memset(r->turn, 0, sizeof r->turn);\n", "\n"),                                               # turns dropped
    ("    if (v > TURN_MAX || v < -TURN_MAX) return RIN_FULL;", ""),                                    # wind-up cap
    ("int rin_valid_input(int idx) { return idx >= 0 && idx < RIN_INPUTS && !(idx >= 69 && idx <= 71); }",
     "int rin_valid_input(int idx) { return idx >= 0 && idx < RIN_INPUTS; }"),                         # 69-71
    ("    int v = r->turn[d[0]] + (d[1] ? d[2] : -d[2]);", "    int v = r->turn[d[0]] + d[2];"),         # direction
]
red = 0
p = os.path.join(d, "remote.c")
orig = open(p, encoding="utf-8", newline="").read()
for a, b in M:
    if a not in orig:
        print("NOT APPLIED:", a[:60]); continue
    open(p, "w", encoding="utf-8", newline="").write(orig.replace(a, b, 1))
    exe = os.path.join(d, "rt")
    c = subprocess.run(["gcc", "-O1", "-w", "-I" + d, "-o", exe, os.path.join(d, "remote_test.c"), p], capture_output=True, text=True)
    r = subprocess.run([exe], capture_output=True, text=True) if c.returncode == 0 else None
    ok = r is None or r.returncode != 0
    red += ok
    print("red  " if ok else "GREEN", a.strip()[:70].replace("\n", " "))
open(p, "w", encoding="utf-8", newline="").write(orig)
print(f"{red}/{len(M)} mutations red")
shutil.rmtree(d)
sys.exit(0 if red == len(M) else 1)
