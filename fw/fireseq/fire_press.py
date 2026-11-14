"""Play the Fire remotely (host command 20, firmware v0.31r+): press its buttons / pads, turn its knobs / SELECT.
For troubleshooting: a script can play it for hours the way hands do. Injected input goes through the same code as real
input (only the chain scan in TIM4 is skipped). Run on the Pi.

  python3 fire_press.py tap PLAY                    press + release (default 80 ms down)
  python3 fire_press.py hold SHIFT 1.5              hold for 1.5 s
  python3 fire_press.py combo SHIFT REC             hold SHIFT, tap REC, let go (= undo)
  python3 fire_press.py turn K1 +5                  knob 1 five detents clockwise (K1-K4, SELECT)
  python3 fire_press.py script file.txt [--log x]   run a script (below), every action timestamped
  python3 fire_press.py names                       the input names

Input names: the firmware's own (fire_map.h, IN_*, without IN_): PLAY STOP REC SHIFT ALT STEP NOTE DRUM PERFORM
BROWSER SELECT (= pushing SELECT) PAT_UP PAT_DN GRID_L GRID_R KNOBMODE PATSONG ROW1-ROW4, and pads as P<row>.<col>
(row 0-3 from the top, col 0-15), e.g. P0.0 = top left.

Script lines (# = comment):
  tap NAME [ms]   hold NAME ms   press NAME   release NAME   combo NAME... (the last is tapped while the rest are held)
  turn K1..K4|SELECT +n|-n       wait ms      loop N ... end (nests)      release_all
Safety (the firmware's): nothing works until this tool arms it; it re-arms every second; if this tool dies the Fire
lets go of everything by itself 5 s later; on exit it disarms (every held input is released)."""
import os, re, sys, threading, time

sys.path.insert(0, os.path.expanduser("~/orc/orc1"))
try:
    import orc1_midisafe
    orc1_midisafe.install()                          # the ORC's mido ALSA-client leak fix (CLAUDE.md)
except Exception:
    pass
import mido

HERE = os.path.dirname(os.path.abspath(__file__))
HDR = [0x7D, 0x4F, 0x52, 0x43]
RESULT = {0: "ok", 1: "not armed", 2: "bad", 3: "full"}
TURNERS = {"K1": 0, "K2": 1, "K3": 2, "K4": 3, "SELECT": 4}


def load_names():
    """IN_* from fire_map.h next to this file (the firmware's own numbering), + the 64 pads."""
    src = open(os.path.join(HERE, "fire_map.h"), encoding="utf-8").read()
    names = {m.group(1): int(m.group(2)) for m in re.finditer(r"\bIN_([A-Z0-9_]+)\s*=\s*(\d+)", src)}
    for r in range(4):
        for c in range(16):
            n = r * 16 + c
            names[f"P{r}.{c}"] = (7 - n // 8) * 8 + n % 8   # = in_pad(r, c)
    return names


class Fire:
    def __init__(self, log=None):
        out = next((n for n in mido.get_output_names() if ("FLAME seq" in n or "ORC FW seq" in n)), None)
        inn = next((n for n in mido.get_input_names() if ("FLAME seq" in n or "ORC FW seq" in n)), None)
        if not out or not inn:
            sys.exit("no ORC FW port: is the Fire plugged in and on our firmware?")
        self.o, self.i = mido.open_output(out), mido.open_input(inn)
        self.lock = threading.Lock()
        self.log = open(log, "a") if log else None
        self.stop = threading.Event()
        self.names = load_names()
        r = self.cmd(0, [1])
        if r is None:
            sys.exit("the Fire did not answer 20 00 01: its firmware has no remote input (v0.31r+ needed)")
        threading.Thread(target=self._keepalive, daemon=True).start()

    def note(self, s):
        line = f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d} {s}"
        if self.log:
            self.log.write(line + "\n"); self.log.flush()
        return line

    def cmd(self, sub, data, timeout=0.5):
        """Send 20 <sub> <data>, wait for 20 <sub> <result> <armed>. Returns (result, armed) or None."""
        with self.lock:
            for _ in self.i.iter_pending():
                pass
            self.o.send(mido.Message("sysex", data=HDR + [0x20, sub] + list(data)))
            end = time.time() + timeout
            while time.time() < end:
                for m in self.i.iter_pending():
                    d = list(m.data) if m.type == "sysex" else []
                    if d[:6] == HDR + [0x20, sub] and len(d) >= 8:
                        return d[6], d[7]
                time.sleep(0.002)
        return None

    def _keepalive(self):
        while not self.stop.wait(1.0):
            self.cmd(0, [1])

    def _check(self, what, r):
        if r is None:
            raise RuntimeError(self.note(f"{what}: NO ANSWER (did the Fire crash or reboot?)"))
        if r[0]:
            raise RuntimeError(self.note(f"{what}: {RESULT.get(r[0], r[0])}"))
        self.note(what)

    def idx(self, name):
        name = name.upper()
        if name not in self.names:
            raise ValueError(f"unknown input {name!r} (try: names)")
        return self.names[name]

    def press(self, name, down=True):
        self._check(f"{'press' if down else 'release'} {name}", self.cmd(1, [self.idx(name), 1 if down else 0]))

    def tap(self, name, ms=80):
        self.press(name); time.sleep(ms / 1000); self.press(name, False)

    def turn(self, which, n):
        k = TURNERS.get(which.upper())
        if k is None:
            raise ValueError(f"unknown turner {which!r}: K1-K4 or SELECT")
        while n:
            step = max(-127, min(127, n))
            self._check(f"turn {which} {step:+d}", self.cmd(2, [k, 1 if step > 0 else 0, abs(step)]))
            n -= step

    def release_all(self):
        self._check("release all", self.cmd(3, []))

    def close(self):
        self.stop.set()
        r = self.cmd(0, [0])
        self.note("disarmed (all released)" if r else "disarm: NO ANSWER")


def run_lines(fire, lines):
    i = 0
    while i < len(lines):
        w = lines[i].split()
        i += 1
        if not w:
            continue
        op = w[0].lower()
        if op == "loop":
            depth, j = 1, i
            while j < len(lines):
                t = lines[j].split()
                if t and t[0].lower() == "loop": depth += 1
                if t and t[0].lower() == "end":
                    depth -= 1
                    if not depth: break
                j += 1
            for _ in range(int(w[1])):
                run_lines(fire, lines[i:j])
            i = j + 1
        elif op == "tap": fire.tap(w[1], int(w[2]) if len(w) > 2 else 80)
        elif op == "hold": fire.press(w[1]); time.sleep(int(w[2]) / 1000); fire.press(w[1], False)
        elif op == "press": fire.press(w[1])
        elif op == "release": fire.press(w[1], False)
        elif op == "combo":
            for n in w[1:-1]: fire.press(n)
            fire.tap(w[-1])
            for n in reversed(w[1:-1]): fire.press(n, False)
        elif op == "turn": fire.turn(w[1], int(w[2]))
        elif op == "wait": time.sleep(int(w[1]) / 1000)
        elif op == "release_all": fire.release_all()
        elif op == "end": raise ValueError("'end' without 'loop'")
        else: raise ValueError(f"unknown script line: {' '.join(w)}")


def main(a):
    if not a or a[0] in ("-h", "--help"):
        print(__doc__); return 0
    if a[0] == "names":
        n = load_names()
        print(" ".join(k for k in n if not re.match(r"P\d", k)), "+ pads P0.0 .. P3.15"); return 0
    log = None
    if "--log" in a:
        k = a.index("--log"); log = a[k + 1]; del a[k:k + 2]
    fire = Fire(log)
    try:
        op = a[0]
        if op == "tap": fire.tap(a[1], int(a[2]) if len(a) > 2 else 80)
        elif op == "hold": fire.press(a[1]); time.sleep(float(a[2])); fire.press(a[1], False)
        elif op == "combo": run_lines(fire, ["combo " + " ".join(a[1:])])
        elif op == "turn": fire.turn(a[1], int(a[2]))
        elif op == "script":
            lines = [l.split("#")[0] for l in open(a[1], encoding="utf-8")]
            run_lines(fire, lines)
        else:
            print(__doc__); return 2
        return 0
    except (RuntimeError, ValueError) as e:
        print(e); return 1
    except KeyboardInterrupt:
        return 130
    finally:
        fire.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
