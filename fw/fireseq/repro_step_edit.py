"""Reproduce the owner's crash conditions with the remote input (v0.31r+): STEP mode, PLAYING, his hands on the kick and
snare rows (T1 / T2): taps on / off, short and long holds, SHIFT + pad (velocity), hold + knob 1 / 2 (ratchet / chance).
Mostly kick, like he played, in bursts of 3-12 edits with 3.5-8 s rests (the owner: "give it a sec or 4 to record to
memory every now and then": a rest lets the save run while the sequencer plays). Run it under systemd, NOT from an ssh
shell (a dying ssh session sent the first run a SIGINT after 3 minutes, 2026-11-11):

    sudo systemd-run --uid=pibox --unit=fire-repro --working-directory=/home/pibox/fire_fw \\
        python3 -B repro_step_edit.py --minutes 120 --elf fireseq_v031r_20261111.elf
    (stop early: sudo systemctl kill -s INT fire-repro  -- the clean-up still runs)

or by hand:

    python3 -B repro_step_edit.py --minutes 120 [--seed N] [--pattern 30]

SAFE FOR HIS PATTERNS: it works on ONE empty test pattern (default 30, must be empty: it checks the picker reply) and
at the end wipes it by copying the empty pattern after it over it (ALT + PATTERN down), then goes back to the pattern
that was current. His patterns are never touched. It DOES play: T1 / T2 send their notes on their channels, so the
ORC's drums sound (and skipback records it).

Every action is logged with a timestamp and the random seed, so the exact sequence that preceded a crash can be
replayed (--seed). Every minute: 07 (loop time, stack). Every 5 min and whenever the Fire stops answering:
host_crash.py (crash record + the TIM4 check's mismatch note)."""
import argparse, os, random, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fire_press
import mido

HDR = [0x7D, 0x4F, 0x52, 0x43]
HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--minutes", type=float, default=60)
ap.add_argument("--seed", type=int, default=None)
ap.add_argument("--pattern", type=int, default=30, help="2-31, an EMPTY pattern; the one after it must be empty too")
ap.add_argument("--elf", required=True, help="the flashed .elf (to read the step pool + current pattern from RAM)")
a = ap.parse_args()
if not 2 <= a.pattern <= 31:
    sys.exit("--pattern must be 2-31 (the one after it is the wipe source)")
nm = subprocess.run(["arm-none-eabi-nm", a.elf], capture_output=True, text=True).stdout
SYM = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
seed = a.seed if a.seed is not None else int(time.time())
rnd = random.Random(seed)
log = os.path.join(HERE, f"repro_{time.strftime('%Y%m%d_%H%M%S')}_seed{seed}.log")
fire = fire_press.Fire(log)
fire.note(f"START seed {seed}, {a.minutes} min, test pattern {a.pattern}")
print("log:", log)


def ask(data, want, timeout=1.0):
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


def crash_report(tag):
    r = subprocess.run([sys.executable, "-B", os.path.join(os.path.expanduser("~/fire_fw"), "host_crash.py"), "--elf", "auto"],
                       capture_output=True, text=True, timeout=60)
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    key = [l for l in lines if "crash" in l.lower() or "mismatch" in l.lower() or "TIM4" in l][:4] or lines[:2]
    fire.note(f"{tag}: " + " | ".join(key))
    return r.stdout


def status():
    d = ask([0x07], 0x07)
    if not d:
        return None
    return {"playing": d[8], "saves": d[7], "unsaved": d[5], "stack": d[14] << 7 | d[15],
            "loop_us": (d[18] << 7 | d[19]) if len(d) > 19 else -1}


def pat(n0):                                        # 0D: switch / queue to pattern n0 (0-31); reply current, queued
    d = ask([0x0D, n0], 0x0D)
    return d


def peek(addr):
    d = ask([0x09] + [(addr >> (7 * k)) & 0x7F for k in range(5)], 0x09)
    if not d:
        raise RuntimeError("no 09 reply")
    return sum((d[5 + k] & 0x7F) << (7 * k) for k in range(5)) & 0xFFFFFFFF


def off_at(k):                                      # steps.c off[k] (uint16)
    w = peek(SYM["off"] + 2 * (k & ~1))
    return (w >> 16) if k & 1 else (w & 0xFFFF)


def pattern_bytes(p1):                              # live step bytes of pattern p1 (1-32)
    return off_at(p1 * 32) - off_at((p1 - 1) * 32)


def saved_bytes(p1):
    w = peek(SYM["steps_saved"] + 2 * ((p1 - 1) & ~1))
    return (w >> 16) if (p1 - 1) & 1 else (w & 0xFFFF)


PADS = {0: list(range(16)), 1: list(range(16))}     # rows 0 / 1 = T1 kick / T2 snare (view scrolled to the top)

# ---- refuse unless the test pattern and the one after it are empty (live and saved) ----
for p1 in (a.pattern, a.pattern + 1):
    lb, sb = pattern_bytes(p1), saved_bytes(p1)
    if lb or sb:
        fire.close()
        sys.exit(f"pattern {p1} is NOT empty ({lb} live / {sb} saved bytes): pick another --pattern")
home = (peek(SYM["G"]) >> 16) & 0xFF                # G.cur_pat (main.c global_t: bpm u16, then cur_pat)
fire.note(f"patterns {a.pattern} and {a.pattern + 1} are empty; the current pattern is {home + 1}")

# ---- set up: stopped, STEP mode, the test pattern, top of the track list, page 1 ----
st = status()
if st and st["playing"]:
    fire.tap("STOP"); time.sleep(0.3)
fire.tap("STEP"); time.sleep(0.2)
first = pat(a.pattern - 1)
if not first or first[0] != 1:
    sys.exit(f"could not switch to pattern {a.pattern}: {first}")
fire.note(f"switched to pattern {a.pattern} (reply {first})")
for _ in range(40):                                 # scroll the track list to the top (one row per pass)
    fire.turn("SELECT", -1); time.sleep(0.03)
for _ in range(8):
    fire.tap("GRID_L", 40); time.sleep(0.03)
fire.tap("PLAY"); time.sleep(0.3)
crash_report("before")
end = time.time() + a.minutes * 60
next_st, next_crash, n = time.time() + 60, time.time() + 300, 0
gone = False
burst = rnd.randint(3, 12)                          # the owner: edits come in bursts with a few seconds' rest, and a rest
try:                                                # of 3 s+ is when a save happens (3 s after the last change) WHILE playing
    while time.time() < end:
        if burst <= 0:
            rest = rnd.uniform(3.5, 8.0)
            fire.note(f"rest {rest:.1f} s (a save is due 3 s after the last edit)")
            time.sleep(rest)
            burst = rnd.randint(3, 12)
        burst -= 1
        r = 0 if rnd.random() < 0.7 else 1          # mostly kick
        pad = f"P{r}.{rnd.choice(PADS[r])}"
        x = rnd.random()
        try:
            if x < 0.55:
                fire.tap(pad, rnd.randint(30, 250))
            elif x < 0.67:
                fire.press("SHIFT"); fire.tap(pad, rnd.randint(40, 120)); fire.press("SHIFT", False)
            elif x < 0.80:
                fire.press(pad); time.sleep(0.05)
                for _ in range(rnd.randint(1, 3)):
                    fire.turn(rnd.choice(["K1", "K2"]), rnd.choice([-2, -1, 1, 2])); time.sleep(rnd.uniform(0.03, 0.2))
                fire.press(pad, False)
            elif x < 0.84:
                fire.press(pad); time.sleep(rnd.uniform(0.6, 1.5)); fire.press(pad, False)   # a long hold
            else:
                time.sleep(rnd.uniform(0.2, 1.0))   # hands off a moment
            n += 1
            if gone:
                fire.note("the Fire answers again"); gone = False
                crash_report("after it came back")
        except RuntimeError as e:                   # no answer: the Fire rebooted / crashed / was unplugged
            if not gone:
                fire.note(f"FIRE NOT ANSWERING after {n} actions: {e}")
                gone = True
            time.sleep(2)
            try:
                fire.cmd(0, [1])                    # re-arm when it is back
            except Exception:
                pass
            continue
        time.sleep(rnd.uniform(0.05, 0.4))
        now = time.time()
        if now >= next_st:
            s = status(); fire.note(f"status after {n} actions: {s}"); next_st = now + 60
        if now >= next_crash:
            crash_report("check"); next_crash = now + 300
finally:
    fire.note(f"END after {n} actions: cleaning up")
    try:
        fire.release_all()
        fire.tap("STOP"); time.sleep(0.5)
        for _ in range(8):
            fire.tap("GRID_L", 40); time.sleep(0.03)
        nxt = pat(a.pattern)                        # the (empty) pattern after the test one
        fire.note(f"to pattern {a.pattern + 1}: {nxt}")
        fire.press("ALT"); fire.tap("PAT_DN"); fire.press("ALT", False)   # copy it (empty) over the test pattern
        time.sleep(0.5)
        left = pattern_bytes(a.pattern)
        fire.note(f"wiped pattern {a.pattern} ({left} live bytes left: must be 0)")
        fire.note(f"back to pattern {home + 1}: {pat(home)}")
    except Exception as e:
        fire.note(f"CLEAN-UP FAILED ({e}): pattern {a.pattern} may still hold test steps; wipe it by hand")
    crash_report("after")
    fire.close()
