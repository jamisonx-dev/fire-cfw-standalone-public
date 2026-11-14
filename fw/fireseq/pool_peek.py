"""Read-only: how full the Fire's step pool is, from its RAM (09 peek), by the same rule the firmware refuses a step.
    python3 -B pool_peek.py <the flashed .elf>"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/orc/orc1"))
try:
    import orc1_midisafe; orc1_midisafe.install()
except Exception:
    pass
import mido

HDR = [0x7D, 0x4F, 0x52, 0x43]
POOL, PATS, TRACKS = 20480, 32, 32
NBLK = PATS * TRACKS
nm = subprocess.run(["arm-none-eabi-nm", sys.argv[1]], capture_output=True, text=True).stdout
sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
o = mido.open_output(next(n for n in mido.get_output_names() if ("FLAME seq" in n or "ORC FW seq" in n)))
i = mido.open_input(next(n for n in mido.get_input_names() if ("FLAME seq" in n or "ORC FW seq" in n)))


def peek(a):
    for _ in i.iter_pending():
        pass
    o.send(mido.Message("sysex", data=HDR + [0x09] + [(a >> (7 * k)) & 0x7F for k in range(5)]))
    end = time.time() + 1
    while time.time() < end:
        for m in i.iter_pending():
            if m.type != "sysex":                   # (the Fire's own notes / clock while it plays)
                continue
            d = list(m.data)
            if d[:5] == HDR + [0x09]:
                w = d[10:15]
                return sum((w[k] & 0x7F) << (7 * k) for k in range(5)) & 0xFFFFFFFF
        time.sleep(0.002)
    raise SystemExit("no 09 reply")


def u16s(base, n):
    out = []
    for k in range(0, n, 2):
        w = peek(base + 2 * k)
        out += [w & 0xFFFF, w >> 16]
    return out[:n]


off = u16s(sym["off"], NBLK + 2)               # (off is 4-aligned: checked by the reads)
saved = u16s(sym["steps_saved"], PATS)
live = [off[(p + 1) * TRACKS] - off[p * TRACKS] for p in range(PATS)]
used = off[NBLK]
counted = sum(max(l, s) for l, s in zip(live, saved))
print(f"pool bytes used (live): {used} of {POOL}")
print(f"counted by the refuse rule (max of live / saved per pattern): {counted} -> room {max(0, POOL - counted)} B"
      f" = {max(0, POOL - counted) // 3} drum steps")
busy = [(p + 1, live[p], saved[p]) for p in range(PATS) if live[p] or saved[p]]
print("patterns (number, live B, saved B):", busy)
print("offsets monotone:", all(off[k] <= off[k + 1] for k in range(NBLK)), " last block end == used:", off[NBLK] == used)
