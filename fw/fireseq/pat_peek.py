"""Read-only: the Fire's current pattern + the lit steps of the given patterns (track, step, level byte), from RAM.
    python3 -B pat_peek.py <elf> <pattern 1-32> [...]"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/orc/orc1"))
try:
    import orc1_midisafe; orc1_midisafe.install()
except Exception:
    pass
import mido

HDR = [0x7D, 0x4F, 0x52, 0x43]
nm = subprocess.run(["arm-none-eabi-nm", sys.argv[1]], capture_output=True, text=True).stdout
S = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
o = mido.open_output(next(n for n in mido.get_output_names() if ("FLAME seq" in n or "ORC FW seq" in n)))
i = mido.open_input(next(n for n in mido.get_input_names() if ("FLAME seq" in n or "ORC FW seq" in n)))


def peek(a):
    for _ in i.iter_pending():
        pass
    o.send(mido.Message("sysex", data=HDR + [0x09] + [(a >> (7 * k)) & 0x7F for k in range(5)]))
    end = time.time() + 1
    while time.time() < end:
        for m in i.iter_pending():
            if m.type == "sysex" and list(m.data[:5]) == HDR + [0x09]:
                d = list(m.data)
                return sum((d[10 + k] & 0x7F) << (7 * k) for k in range(5)) & 0xFFFFFFFF
        time.sleep(0.002)
    raise SystemExit("no 09 reply")


def u16(base, k):
    w = peek(base + 2 * (k & ~1))
    return (w >> 16) if k & 1 else (w & 0xFFFF)


print("current pattern:", ((peek(S["G"]) >> 16) & 0xFF) + 1)
for p1 in map(int, sys.argv[2:]):
    p = p1 - 1
    for t in range(32):
        words = [peek(S["map"] + 16 * (p * 32 + t) + 4 * w) for w in range(4)]   # map[p][t][4] uint32
        steps = [w * 32 + b for w in range(4) for b in range(32) if words[w] >> b & 1]
        if steps:
            base = u16(S["off"], p * 32 + t)
            size = 3 if t < 24 else 6
            levels = []
            for j, s in enumerate(steps):
                a = S["pool"] + base + j * size
                levels.append((peek(a & ~3) >> (8 * (a & 3))) & 0xFF)
            print(f"pattern {p1} T{t + 1}: steps {[s + 1 for s in steps]} first bytes {[hex(v) for v in levels]}")
