"""Bench proof of v0.33b's entry-misread tolerance on a REAL Fire running a CRASH_TEST build (host 7E 08 hands TIM4
0xFFFFFFFF as its entry address, what the 2026-11-11 crashes saw). Each fake must be COUNTED in the TIM4 note (kind 4,
seen 0xFFFFFFFF) and the Fire must keep running: same uptime line, no crash record, no restart.
    python3 -B entry_bench.py        (in the bench tree, on the Pi)"""
import sys, time
import mido
import host_vu as H

HDR = [0x7D, 0x4F, 0x52, 0x43]
o = mido.open_output(next(n for n in mido.get_output_names() if ("FLAME seq" in n or "ORC FW seq" in n)))
i = mido.open_input(next(n for n in mido.get_input_names() if ("FLAME seq" in n or "ORC FW seq" in n)))
fails = 0


def check(c, what):
    global fails
    print(("ok    " if c else "FAIL  ") + what, flush=True)
    fails += not c


def ask(data, want, timeout=1.0):
    for _ in i.iter_pending():
        pass
    o.send(mido.Message("sysex", data=HDR + data))
    end = time.time() + timeout
    while time.time() < end:
        for m in i.iter_pending():
            if m.type == "sysex" and list(m.data[:5]) == HDR + [want]:
                return list(m.data)
        time.sleep(0.002)
    return None


def note():
    return H.decode_crash(ask([0x06], 0x06) or []) or {}


c = note()
check(bool(c) and not c.get("valid"), f"start: the Fire answers 06 and has no crash on record (valid={c.get('valid')})")
n0 = c.get("t4_count", 0)
t0 = c.get("t4_ms", 0)
for k in range(5):
    o.send(mido.Message("sysex", data=HDR + [0x7E, 0x08]))
    time.sleep(0.25)
    c = note()
    check(bool(c) and c.get("t4_count") == n0 + k + 1 and c.get("t4_kind") == 4 and c.get("t4_seen") == 0xFFFFFFFF
          and (c.get("t4_want", 0) >> 16) == 0x2000 and not c.get("valid"),
          f"fake {k + 1}: counted ({c.get('t4_count')}), kind {c.get('t4_kind')}, handed 0x{c.get('t4_seen', 0):08x}, "
          f"TIM4's own SP 0x{c.get('t4_want', 0):08x}, no crash record")
ms1 = c.get("t4_ms", 0)
time.sleep(1.0)
o.send(mido.Message("sysex", data=HDR + [0x7E, 0x08])); time.sleep(0.25)
c = note()
check(c.get("t4_ms", 0) - ms1 > 900, f"the uptime clock ran on between fakes ({ms1} -> {c.get('t4_ms')} ms): no restart")
h = ask([0x00], 0x00)
check(h is not None, "the Fire still answers hello")
print(H.describe_t4_note(c))
print("ALL OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
