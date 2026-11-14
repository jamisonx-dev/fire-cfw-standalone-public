"""Live check of v0.31r remote input ON THE REAL FIRE (run on the Pi, nobody touching it). Edits nothing: it opens the
BROWSER menu, walks to Pi VU, starts the bars, wakes the pads, closes the menu; then proves a dead script lets go.
Measured with 07 (status) and 09 (peek of `rin`, address from the flashed ELF).
    python3 -B remote_live_check.py <the flashed .elf>"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fire_press                                   # (installs the ORC's mido leak fix)
import mido

HDR = [0x7D, 0x4F, 0x52, 0x43]
F = ["lt_on", "cur", "host", "by_host", "pinned", "unsaved", "lazy", "saves", "playing", "ext", "mode", "speed", "menu", "frame",
     "stack_hi", "stack_lo", "surface", "surf_ch", "loop_hi", "loop_lo"]
fails = 0


def check(c, what):
    global fails
    print(("ok    " if c else "FAIL  ") + what)
    fails += not c


def rin_addr(elf):
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout
    return next(int(l.split()[0], 16) for l in out.splitlines() if l.endswith(" rin"))


def ask(fire, data, want, timeout=1.0):
    with fire.lock:
        for _ in fire.i.iter_pending():
            pass
        fire.o.send(mido.Message("sysex", data=HDR + data))
        end = time.time() + timeout
        while time.time() < end:
            for m in fire.i.iter_pending():
                if m.type == "sysex" and list(m.data[:5]) == HDR + [want]:
                    return list(m.data[5:])
            time.sleep(0.002)
    return None


def status(fire):
    d = ask(fire, [0x07], 0x07)
    return dict(zip(F, d)) if d else None


def peek(fire, a):
    d = ask(fire, [0x09] + [(a >> (7 * i)) & 0x7F for i in range(5)], 0x09)
    if not d:
        return None
    w = d[5:10]
    return sum((w[i] & 0x7F) << (7 * i) for i in range(5)) & 0xFFFFFFFF


elf = sys.argv[1]
A = rin_addr(elf)
fire = fire_press.Fire()
hello = ask(fire, [0x00], 0x00)
check(hello is not None and hello[0] >= 7, f"hello says version 7 or later (got {hello})")
s = status(fire)
check(s is not None and s["menu"] == 0 and s["lt_on"] == 0, "start: no menu, no bars")
fire.tap("BROWSER"); time.sleep(0.2)
check(status(fire)["menu"] == 1, "remote BROWSER tap opens the menu")
for _ in range(8 if hello and hello[0] >= 8 else 7):   # Autosave, Save now, ORC mode, (v0.33: ORC pads,) DAW page, Playing, Knobs, Display, -> Pi VU
    fire.turn("SELECT", 1); time.sleep(0.05)        # (the menu moves one row per pass, like a real detent at a time)
time.sleep(0.2)
fire.tap("SELECT"); time.sleep(0.2)                 # opens the Pi VU page
fire.tap("SELECT"); time.sleep(0.4)                 # Start Pi VU
s = status(fire)
check(s["lt_on"] == 1 and s["menu"] == 0 and s["cur"] == 13 and s["by_host"] == 0, f"remote SELECT turn + pushes started Pi VU by hand (07: {s['lt_on']} {s['cur']} by_host {s['by_host']} menu {s['menu']})")
f0 = s["frame"]; time.sleep(1.0); f1 = status(fire)["frame"]
check(f0 != f1, f"the bars are drawing (frames {f0} -> {f1})")
fire.tap("GRID_L"); time.sleep(0.3)
s = status(fire)
check(s["lt_on"] == 0, "a remote GRID tap only woke the pads (bars off)")
# held state + the per-input path: hold SHIFT, read rin.held from RAM
fire.press("SHIFT"); time.sleep(0.1)
w = peek(fire, A + 8)                               # held[0..3] (armed @0, last_ms @4, held @8)
w2 = peek(fire, A + 16)                             # held[8..10] (SHIFT = input 79 = byte 9 bit 7)
check(w2 is not None and (w2 >> 8) & 0x80, f"SHIFT held from the Pi shows in rin.held (word {w2:#010x})")
fire.press("SHIFT", False); time.sleep(0.1)
check(not ((peek(fire, A + 16) >> 8) & 0x80), "and released")
fire.close(); time.sleep(0.2)
check(peek(fire, A) & 0xFF == 0, "the tool's exit disarmed it")
fire.stop.set()

# a dead script: arm, hold SHIFT, then stop talking (no disarm): the Fire must let go by itself 5 s later
o = fire.o
o.send(mido.Message("sysex", data=HDR + [0x20, 0x00, 0x01]))
o.send(mido.Message("sysex", data=HDR + [0x20, 0x01, 79, 1]))
time.sleep(0.5)
check(peek(fire, A) & 0xFF == 1 and (peek(fire, A + 16) >> 8) & 0x80, "dead-script test: armed, SHIFT held")
time.sleep(5.2)
check(peek(fire, A) & 0xFF == 0 and not ((peek(fire, A + 16) >> 8) & 0x80), "5 s of silence: the Fire disarmed itself and let go of SHIFT")
o.send(mido.Message("sysex", data=HDR + [0x20, 0x01, 79, 1]))
time.sleep(0.2)
check(not ((peek(fire, A + 16) >> 8) & 0x80), "and a press after that is refused (not armed)")
print("ALL OK" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
