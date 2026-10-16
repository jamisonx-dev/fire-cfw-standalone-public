"""Live checks of the 2026-10-13 review fixes ON THE REAL FIRE (run on the Pi, Fire idle, nobody touching it).

Uses host command 07 (status) to SEE what the pads and the saver are doing, so every check is measured, not assumed.
Expects the saved Show = PI VU (14), idle start = 1 min (the owner's settings). Restores the light speed it changes.

  python3 fire_live_test.py [phase ...]    phases: text host idle lazy clock   (default: all)

  text   4.9 / 4.5: host text pins (levels don't steal it), 02 00 ends a host-started show
  host   4.4: PI VU with the Pi gone falls back to SPARKLE (pads stay lit) and comes back; host-started bars end
  idle   4.6 (stopped half): idle lights start after the idle time and take the PI VU bars when levels arrive
  lazy   4.1: a setting changed while stopped is saved ~20 s after the last change (saves counter), and the star clears
  clock  4.3 + 4.6: under an external clock lights keep drawing, idle lights don't start, note timing with lights on
         is no worse than with them off; after STOP the idle time counts from the stop

Stops / starts orc1-firevu (sudo) for the host phases and leaves it running.
"""
import mido, subprocess, sys, threading, time, statistics

HDR = [0x7D, 0x4F, 0x52, 0x43]
FIELDS = ["lt_on", "cur", "host", "by_host", "pinned", "unsaved", "lazy", "saves", "playing", "ext", "mode", "speed", "menu", "frame", "stack_hi", "stack_lo", "surface", "surf_ch"]
fails = 0


def find(names):
    return next(n for n in names if "ORC FW" in n)


out = mido.open_output(find(mido.get_output_names()))
inp = mido.open_input(find(mido.get_input_names()))
notes = []                                          # (time, note) of the Fire's note-ons, while recording
rec = False


def drain(timeout=0.0):
    """Read the Fire; return the newest status reply seen (or None)."""
    st, end = None, time.monotonic() + timeout
    while True:
        for m in inp.iter_pending():
            if m.type == "sysex" and list(m.data[:5]) == HDR + [7]:
                st = dict(zip(FIELDS, m.data[5:]))
            elif rec and m.type == "note_on" and m.velocity:
                notes.append(time.monotonic())
        if st is not None or time.monotonic() >= end:
            return st
        time.sleep(0.001)


def sx(*data):
    out.send(mido.Message("sysex", data=HDR + list(data)))


def status():
    drain()
    sx(7)
    st = drain(1.0)
    if st is None:
        raise SystemExit("no answer to 07: is the new firmware on the Fire?")
    return st


def check(cond, label, st=None):
    global fails
    print(("  ok   " if cond else "  FAIL ") + label + ("" if cond or st is None else f"   {st}"), flush=True)
    if not cond:
        fails += 1


def firevu(on):
    subprocess.run(["sudo", "systemctl", "start" if on else "stop", "orc1-firevu"], check=True)


def levels(secs, v=40):
    end = time.monotonic() + secs
    while time.monotonic() < end:
        sx(1, *([v] * 16)); time.sleep(0.033)


def wait_for(pred, secs, step=0.25):
    end = time.monotonic() + secs
    while True:
        st = status()
        if pred(st) or time.monotonic() > end:
            return st
        time.sleep(step)


def lights_off():
    sx(2, 1); time.sleep(0.1); sx(2, 0); time.sleep(0.2)   # host on, then host off = a clean "off"


def phase_text():
    print("text (4.9, 4.5):")
    firevu(False); lights_off()
    sx(5, *b"TEST"); time.sleep(0.2)
    st = status()
    check(st["lt_on"] and st["cur"] == 14 and st["pinned"] and st["by_host"], "05 text shows, pinned, host-started", st)
    levels(1.0)
    st = status()
    check(st["cur"] == 14 and not st["host"], "levels arriving do NOT steal pinned text", st)
    sx(5); time.sleep(0.2)
    st = status()
    check(st["cur"] == 14 and st["lt_on"], "05 with no text is ignored (text keeps showing)", st)
    sx(4, 3); time.sleep(0.2)
    st = status()
    check(st["cur"] == 3 and st["pinned"], "04 pins an effect", st)
    sx(2, 0); time.sleep(0.2)
    st = status()
    check(not st["lt_on"], "02 00 ends a show the HOST started (was: only host bars)", st)


def phase_host():
    print("host (4.4):")
    firevu(False); lights_off()
    sx(2, 1); levels(0.5)
    st = status()
    check(st["lt_on"] and st["host"] and st["by_host"], "02 01 + levels = host bars", st)
    t0 = time.monotonic()
    st = wait_for(lambda s: not s["lt_on"], 6)
    check(not st["lt_on"] and 2.5 < time.monotonic() - t0 < 5, f"host-started bars END ~3 s after levels stop ({time.monotonic() - t0:.1f} s)", st)
    # a show PICKED as PI VU: the idle start (no 02 01) is the only way without pressing buttons; covered in phase idle


def phase_idle():
    print("idle (4.6 stopped, 4.4 picked PI VU):")
    firevu(False); lights_off()
    st = status()
    if st["mode"] != 14:
        check(False, f"needs the saved Show = PI VU (14); it is {st['mode']}", st); return
    t0 = time.monotonic()
    st = wait_for(lambda s: s["lt_on"], 75, 0.5)
    dt = time.monotonic() - t0
    check(st["lt_on"] and 58 < dt < 63, f"idle lights start after the idle time ({dt:.1f} s, setting 1 min)", st)
    check(st["cur"] == 1 and not st["host"], "with no Pi, the idle PI VU show runs SPARKLE", st)
    f0 = st["frame"]; time.sleep(0.5); st = status()
    check(st["frame"] != f0, "and it is drawing (frames move)", st)
    firevu(True)
    st = wait_for(lambda s: s["host"], 20, 0.5)
    check(st["lt_on"] and st["host"] and st["cur"] == 13, "levels arrive (firevu started) -> the PI VU bars take the pads", st)
    firevu(False)
    t0 = time.monotonic()
    st = wait_for(lambda s: not s["host"], 8)
    check(st["lt_on"] and st["cur"] == 1, f"the Pi goes quiet -> pads FALL BACK to SPARKLE, still lit ({time.monotonic() - t0:.1f} s)", st)
    firevu(True)
    st = wait_for(lambda s: s["host"], 20, 0.5)
    check(st["lt_on"] and st["host"], "and take the bars again when the Pi is back", st)


def phase_lazy():
    print("lazy save (4.1):")
    firevu(True)
    st = status(); old, s0 = st["speed"], st["saves"]
    new = old + 1 if old < 127 else old - 1
    sx(3, new); time.sleep(0.2)
    st = status()
    check(st["speed"] == new and st["lazy"] and st["unsaved"], "03 speed while stopped: lazy save armed, star shows", st)
    time.sleep(15)
    st = status()
    check(st["saves"] == s0 and st["lazy"], "not saved yet at 15 s", st)
    st = wait_for(lambda s: s["saves"] != s0, 10)
    check(st["saves"] == (s0 + 1) & 127 and not st["lazy"] and not st["unsaved"], "saved ~20 s after the change, star gone", st)
    s1 = st["saves"]
    sx(3, old)
    st = wait_for(lambda s: s["saves"] != s1, 25)
    check(st["speed"] == old and st["saves"] == (s1 + 1) & 127 and not st["unsaved"], f"restored speed {old} saved too", st)


def clock_run(secs, bpm, lights):
    """Play the Fire from our clock for secs; return the note offsets (ms) after the step's tick."""
    global rec
    tick = 60.0 / bpm / 24
    sent = []
    stop = threading.Event()

    def clk():
        t = time.monotonic()
        out.send(mido.Message("start")); sent.append(t)
        n = 0
        while not stop.is_set():
            n += 1
            nxt = t + n * tick
            while time.monotonic() < nxt - 0.002:
                time.sleep(0.001)
            while time.monotonic() < nxt:
                pass
            out.send(mido.Message("clock")); sent.append(time.monotonic())
    notes.clear(); rec = True
    th = threading.Thread(target=clk, daemon=True); th.start()
    time.sleep(1.0)
    if lights:
        sx(4, 7)                                     # RIPPLE, pinned: drawing every frame
    frames = []
    end = time.monotonic() + secs
    while time.monotonic() < end:
        st = status(); frames.append(st["frame"])
        drain(0.5)
    st = status()
    stop.set(); th.join(); out.send(mido.Message("stop")); rec = False
    steps = sent[1::6]                              # every 6th tick = a step (the first clock after START = step 0)
    offs = []
    for t in notes:
        prev = [s for s in steps if s <= t + 0.0005]
        if prev:
            offs.append((t - prev[-1]) * 1000)
    return st, frames, offs


def phase_clock():
    print("external clock (4.3, 4.6):")
    firevu(False); lights_off()
    st0, _, off_dark = clock_run(25, 120, False)
    check(st0["ext"] and not st0["lt_on"], "playing from our clock, no lights", st0)
    lights_off()
    st1, frames, off_lit = clock_run(25, 120, True)
    moving = len(set(frames)) > len(frames) // 2
    check(st1["lt_on"] and moving, f"lights keep DRAWING under an external clock ({len(set(frames))} distinct frame counts in {len(frames)} polls)", st1)

    def summ(o):
        o = sorted(o)
        return (statistics.median(o), o[int(len(o) * 0.99) - 1], o[-1]) if len(o) > 20 else (0, 0, 0)
    d, l = summ(off_dark), summ(off_lit)
    print(f"     note after its tick, ms: lights off median {d[0]:.2f} p99 {d[1]:.2f} max {d[2]:.2f} ({len(off_dark)} notes)")
    print(f"                              lights on  median {l[0]:.2f} p99 {l[1]:.2f} max {l[2]:.2f} ({len(off_lit)} notes)")
    check(len(off_lit) > 20 and l[1] <= d[1] + 1.0, "note timing with lights on is within 1 ms (p99) of lights off")
    lights_off()
    print("     now 70 s: playing 70 s from our clock, the idle time (1 min) must NOT start lights while it plays")
    global rec
    st, frames, _ = clock_run(70, 120, False)
    check(not st["lt_on"], "no idle lights during 70 s of external play (was: idle timer ignored transport)", st)
    t0 = time.monotonic()
    st = wait_for(lambda s: s["lt_on"], 75, 0.5)
    dt = time.monotonic() - t0
    check(st["lt_on"] and 58 < dt < 63, f"after STOP, idle lights come the idle time later ({dt:.1f} s)", st)
    lights_off()


PHASES = {"text": phase_text, "host": phase_host, "idle": phase_idle, "lazy": phase_lazy, "clock": phase_clock}
try:
    for p in (sys.argv[1:] or ["text", "host", "idle", "lazy", "clock"]):
        PHASES[p]()
finally:
    try:
        lights_off()
    finally:
        firevu(True)
print(f"\n{'ALL OK' if not fails else f'{fails} FAILED'}")
sys.exit(1 if fails else 0)
