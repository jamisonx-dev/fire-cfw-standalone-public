"""Live check of v0.33 ORC mode (host mode) + the 22-level LEDs ON THE REAL FIRE (run on the Pi, nobody touching it).
Edits nothing on the Fire: it switches ORC mode on and off, paints pads and the screen the way the ORC does, presses
and turns through the remote input (host 20) and reads back what the Fire sent and what is in its memory (09 peek of
led_level / fb, addresses from the flashed ELF).
RUN IT WHILE THE BRAIN IS NOT IN ORC MODE WITH THE FIRE (a brain that knows ORC mode answers the Fire's 21 01 by
painting and by keeping it alive, which this script's quiet-timeout check must not have): before the brain side is
deployed, or with `--brain-knows` to skip that one check.
    python3 -B hostm_live_check.py <the flashed .elf> [--brain-knows]"""
import os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fire_press                                   # (installs the ORC's mido leak fix)
import mido

HDR = [0x7D, 0x4F, 0x52, 0x43]
HERE = os.path.dirname(os.path.abspath(__file__))
fails = 0


def check(c, what):
    global fails
    print(("ok    " if c else "FAIL  ") + what, flush=True)
    fails += not c


def sym(elf, name):
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout
    return next(int(l.split()[0], 16) for l in out.splitlines() if l.split()[-1] == name)


def collect(fire, secs):
    """Every ORC SysEx the Fire sends in the next `secs` seconds, as lists (cmd first)."""
    got, end = [], time.time() + secs
    with fire.lock:
        while time.time() < end:
            for m in fire.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR:
                    got.append(list(m.data[4:]))
            time.sleep(0.002)
    return got


def ask(fire, data, want, timeout=1.0):
    """Send one ORC frame; -> (the answer's bytes after the command, everything else that arrived first)."""
    other = []
    with fire.lock:
        for _ in fire.i.iter_pending():
            pass
        fire.o.send(mido.Message("sysex", data=HDR + data))
        end = time.time() + timeout
        while time.time() < end:
            for m in fire.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR:
                    if m.data[4] == want:
                        return list(m.data[5:]), other
                    other.append(list(m.data[4:]))
            time.sleep(0.002)
    return None, other


def peek(fire, a):
    d, _ = ask(fire, [0x09] + [(a >> (7 * i)) & 0x7F for i in range(5)], 0x09)
    if not d:
        return None
    w = d[5:10]
    return sum((w[i] & 0x7F) << (7 * i) for i in range(5)) & 0xFFFFFFFF


def byte_at(fire, base, i):
    a = base + i
    w = peek(fire, a & ~3)
    return None if w is None else (w >> (8 * (a & 3))) & 0xFF


def led_pad(r, c, j):                               # fire_map.h led_pad
    L = 3 * c + j
    return ((3 - r) * 6 + 5 - L // 8) * 8 + L % 8


def mode(fire, v):
    d, _ = ask(fire, [0x21, v], 0x21)
    return None if d is None else d[0]


def raw(fire, data):
    with fire.lock:
        fire.o.send(mido.Message("sysex", data=data))


elf = sys.argv[1]
brain_knows = "--brain-knows" in sys.argv
LED, FINE, FB = sym(elf, "led_level"), sym(elf, "led_fine"), sym(elf, "fb")
src = open(os.path.join(HERE, "fire_map.h"), encoding="utf-8").read()
LEDN = {m.group(1): int(m.group(2)) for m in re.finditer(r"\bLED_([A-Z0-9_]+)\s*=\s*(\d+)", src)}

fire = fire_press.Fire()
fire.stop.set()                                     # this script keeps the remote input alive itself (the helper's thread would eat replies)
time.sleep(1.1)


def alive():
    fire.cmd(0, [1])


hello, _ = ask(fire, [0x00], 0x00)
check(hello is not None and hello[0] == 9, f"hello says version 9 (got {hello})")
q, _ = ask(fire, [0x25, 0], 0x25)
check(q is not None and q[2] < 127,
      f"25 00 outside ORC mode: one LED slot per pass, the whole LED interrupt takes {q[2] if q else None} us of its 500 (127 = that or more)")


def duty(v, curve, w):                              # hostm.c hm_duty, in the same whole numbers
    v &= 0x7F
    if not v:
        return 0, 0
    x = v * 32768 // 127
    x2 = x * x >> 15
    x3 = x2 * x >> 15
    a, b, t = (x, x2, curve - 10) if curve <= 20 else (x2, x3, curve - 20)
    y = (a * (10 - t) + b * t) // 10
    u = (y * 21 * 256 + 16384) >> 15
    lw = 256 - w
    mm = min(21, u // lw)
    fmax = min(21, lw // w) if mm < 21 else 21
    return mm, min(fmax, (u - mm * lw + w // 2) // w)
check(mode(fire, 2) == 0, "21 02 while off: answered 21 00, nothing changes")

# ---- on ----
alive()
check(mode(fire, 1) == 1, "21 01: ORC mode on, answered 21 01")
time.sleep(4.0 if brain_knows else 0.2)             # (a brain that knows ORC mode paints for ~2.5 s, then only on a change)
DARK = (lambda a, i: True) if brain_knows else (lambda a, i: byte_at(fire, a, i) == 0)
check(byte_at(fire, LED, LEDN["BROWSER"]) == 21 and byte_at(fire, LED, LEDN["STEP_Y"]) == 3 and DARK(LED, led_pad(0, 0, 0))
      and byte_at(fire, FINE, LEDN["BROWSER"]) == 21 and byte_at(fire, FINE, LEDN["STEP_Y"]) == 3 and DARK(FINE, led_pad(0, 0, 0)),
      "the resting button look in BOTH slots: BROWSER full (21), STEP soft (3), the pads dark")
q, _ = ask(fire, [0x25, 0], 0x25)
W = q[0] if q else 34
check(q is not None and 30 <= q[1] <= 90 and abs(W - (q[1] * 256 + 250) // 500) <= 1 and 12 <= W <= 64,
      f"in ORC mode the short slot runs: {q[1] if q else None} us measured, share {W}/256; the LED interrupt now takes {q[2] if q else None} us (127 = that or more)")

# pad colours the way the ORC sends them (Akai's 65): pad 0 red 127, pad 63 green 64 + blue 127
ask(fire, [0x24, 20], 0x21, timeout=0.3)            # (the brain may have set another curve)
raw(fire, [0x47, 0x7F, 0x43, 0x65, 0, 8, 0, 127, 0, 0, 63, 0, 64, 127])
time.sleep(0.2); mode(fire, 2)
gm, gf = duty(64, 20, W)
got = (byte_at(fire, LED, led_pad(3, 15, 1)), byte_at(fire, FINE, led_pad(3, 15, 1)))
check(byte_at(fire, LED, led_pad(0, 0, 0)) == 21 and byte_at(fire, FINE, led_pad(0, 0, 0)) == 21 and got == (gm, gf)
      and byte_at(fire, LED, led_pad(3, 15, 2)) == 21 and byte_at(fire, LED, led_pad(0, 0, 1)) == 0 and byte_at(fire, FINE, led_pad(0, 0, 1)) == 0,
      f"Akai-format pad colours land in both slots: red 127 -> 21 + 21, green 64 -> {got} (wanted {(gm, gf)}), blue 127 full")
raw(fire, [0x47, 0x7F, 0x43, 0x65, 0, 8, 1, 8, 12, 20, 2, 1, 2, 3])
time.sleep(0.2); mode(fire, 2)
dim = [(byte_at(fire, LED, led_pad(0, 1, j)), byte_at(fire, FINE, led_pad(0, 1, j))) for j in range(3)]
check(dim == [duty(8, 20, W), duty(12, 20, W), duty(20, 20, W)] and all(m == 0 for m, _ in dim) and 1 <= dim[0][1] <= dim[1][1] < dim[2][1],
      f"dim colours (8, 12, 20 of 127) use the SHORT slot only, lit and rising: {dim} (the 22-step driver had 0 / 0 / 1 long steps)")
off = [(byte_at(fire, LED, led_pad(0, 2, j)), byte_at(fire, FINE, led_pad(0, 2, j))) for j in range(3)]
check(off == [(0, 0)] * 3, f"values under half the smallest step (1, 2, 3) stay dark: {off}")
d, _ = ask(fire, [0x24, 10], 0x21, timeout=0.3)     # (24 has no answer; the wait just lets it land)
raw(fire, [0x47, 0x7F, 0x43, 0x65, 0, 4, 63, 0, 64, 127])
time.sleep(0.2); mode(fire, 2)
got = (byte_at(fire, LED, led_pad(3, 15, 1)), byte_at(fire, FINE, led_pad(3, 15, 1)))
check(got == duty(64, 10, W), f"24 10 (a straight curve): green 64 is now {got}")
ask(fire, [0x24, 20], 0x21, timeout=0.3)
alive()

# the screen: a window of bands 2-3 x columns 0-6 all lit (one 7-column group per band = 16 payload bytes)
raw(fire, [0x47, 0x7F, 0x43, 0x0E, 0, 4 + 16, 2, 3, 0, 6] + [0x7F] * 16)
time.sleep(0.3); mode(fire, 2)
w2 = [byte_at(fire, FB, 2 * 128 + i) for i in (0, 3, 6)] + [byte_at(fire, FB, 3 * 128 + 6)]
w0 = byte_at(fire, FB, 2 * 128 + 8)
check(w2 == [0xFF] * 4 and w0 == 0,
      f"an Akai-format bitmap window lands in the framebuffer (bands 2-3 columns 0-6 all lit, column 8 untouched: {w2!r} {w0!r})")

# ---- what the player does comes back as 22 / 23, never as notes ----
alive()
fire.press("P0.0"); a = collect(fire, 0.3)
fire.press("P0.0", False); b = collect(fire, 0.3); mode(fire, 2)
check([x for x in a if x[0] == 0x22] == [[0x22, 54, 100]] and [x for x in b if x[0] == 0x22] == [[0x22, 54, 0]],
      f"the top-left pad: 22 54 100 on the press, 22 54 0 on the release ({a} {b})")
fire.press("P3.15"); a = collect(fire, 0.3); fire.press("P3.15", False); b = collect(fire, 0.3); mode(fire, 2)
check([0x22, 117, 100] in a and [0x22, 117, 0] in b, "the bottom-right pad is note 117")
alive()
fire.press("STEP"); a = collect(fire, 0.3); fire.press("STEP", False); b = collect(fire, 0.3); mode(fire, 2)
check([0x22, 44, 127] in a and [0x22, 44, 0] in b and mode(fire, 2) == 1,
      "STEP goes to the ORC as note 44 and does NOT leave ORC mode (in the pad surface every mode button did)")
fire.turn("K2", 3); a = collect(fire, 0.4); mode(fire, 2)
fire.turn("SELECT", -2); b = collect(fire, 0.4); mode(fire, 2)
up = sum(x[2] for x in a if x[:2] == [0x23, 1] and x[2] < 64)
dn = sum(128 - x[2] for x in b if x[:2] == [0x23, 4] and x[2] > 64)
check(up == 3 and dn == 2, f"knob 2 turned +3 arrives as 23 01 (sum {up}); SELECT -2 as 23 04 (sum {dn})")
alive()
s, _ = ask(fire, [0x07], 0x07)
check(s is not None and s[12] == 0 and s[0] == 0, "07: no menu opened, no bars started by any of that")

# ---- the way out ----
fire.press("SHIFT"); a = collect(fire, 0.2)
fire.press("BROWSER"); b = collect(fire, 0.4)
fire.press("BROWSER", False); fire.press("SHIFT", False); c = collect(fire, 0.3)
check([0x22, 48, 127] in a and [0x21, 0] in b and not any(x[0] == 0x22 and x[1] == 33 for x in b + c),
      f"SHIFT + BROWSER leaves: the Fire says 21 00 and BROWSER is never sent ({b})")
time.sleep(0.3)
check(mode(fire, 2) == 0 and byte_at(fire, FB, 2 * 128 + 3) not in (None, 0xFF),
      "after leaving: 21 02 answers 00 and the Fire drew its own screen over the ORC's bitmap")
s, _ = ask(fire, [0x07], 0x07)
check(s is not None and s[12] == 0, "... and the SHIFT + BROWSER that left did not open the menu")
alive()

# ---- the host goes quiet ----
if brain_knows:
    print("skip  the quiet-host check (--brain-knows: the brain keeps ORC mode alive)")
else:
    check(mode(fire, 1) == 1, "on again")
    t0 = time.time(); got = []
    while time.time() - t0 < 7.5 and [0x21, 0] not in got:
        got += collect(fire, 0.25)
    dt = time.time() - t0
    check([0x21, 0] in got and 4.5 <= dt <= 6.5, f"no 'still here' for 5 s: the Fire went back to its sequencer by itself after {dt:.1f} s")
alive()
check(mode(fire, 1) == 1 and mode(fire, 0) == 0, "21 01 then 21 00 from the host: on, then off")
time.sleep(0.3)
d, _ = ask(fire, [0x06], 0x06)
check(d is not None and len(d) > 2 and d[1] == 0, f"no crash on record (06: valid = {d[1] if d else None})")
s, _ = ask(fire, [0x07], 0x07)
if s:
    print(f"      07: deepest stack {(s[14] << 7) | s[15]} B, longest main-loop pass {(s[18] << 7) | s[19]} us")
fire.close()
print("ALL OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
