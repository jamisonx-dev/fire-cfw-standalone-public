#!/usr/bin/env python3
"""Mutation check for host_vu.py: each deliberate break below must turn host_vu_test.py red.
    python3 host_vu_mutate.py     (run in the folder that holds host_vu.py and host_vu_test.py)
Works on temporary copies; the real files are never touched.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MUTS = [
    ("gate removed", "if rms < GATE:", "if False:"),
    ("ring wrap read removed", "if len(raw) < n:", "if False:"),
    ("frame clamp removed", "[max(0, min(127, int(x))) for x in levels]", "[int(x) for x in levels]"),
    ("gain follower removed", "self.ref = max(self.ref, float(db.max()))", "pass"),
    ("ring ranking ignores RAM", "return (not ram, ", "return (False, "),
    ("tilt sign flipped", "(TILT_DB_OCT * np.log2", "(-TILT_DB_OCT * np.log2"),
    ("idle priority dropped", "    idle_priority()\n    mido = None", "    mido = None"),
    ("band edges reversed", "edges = F_LO * (F_HI / F_LO) ** (np.arange(NBANDS + 1) / NBANDS)",
     "edges = F_LO * (F_HI / F_LO) ** (np.arange(NBANDS + 1)[::-1] / NBANDS)"),
    ("silence keeps old gain", "self.ref = max(REF_FLOOR_DB, self.ref - REF_DECAY_DB_S * dt)", "pass"),
    ("smoothing off (attack = release = 1)", "ATTACK = 0.6 ", "ATTACK = 1.0 "),
    ("no glide down (release = 1)", "RELEASE = 0.25 ", "RELEASE = 1.0 "),
    ("smoothing never applied", "self.s = self.s + (v - self.s) * np.where(v > self.s, ATTACK, RELEASE)", "self.s = v"),
    ("reference floor back at -60 (quiet music can't reach full)", "REF_FLOOR_DB = -66.0", "REF_FLOOR_DB = -60.0"),
    ("bins not averaged (W rows sum to the bin count)", "self.W[i, ix] = 1.0 / len(ix)", "self.W[i, ix] = 1.0"),
    # review 4.7 (2026-10-14): link health independent of audio, guards, the averaging matrix
    ("link serviced only when the ring has audio (the old bug)",
     "    if fire:\n        fire.service(now)\n    mono = src.read(now)\n    if mono is None:\n        return None\n",
     "    mono = src.read(now)\n    if mono is None:\n        return None\n    if fire:\n        fire.service(now)\n"),
    ("send() does the link work again (opens ports / hellos)",
     "        if self.out is None:\n            return False\n        return self._send(data)",
     "        self.service(now)\n        if self.out is None:\n            return False\n        return self._send(data)"),
    ("port listing unguarded",
     "        try:\n            outs = [n for n in self.mido.get_output_names() if \"ORC FW\" in n]\n        except Exception as e:",
     "        if True:\n            outs = [n for n in self.mido.get_output_names() if \"ORC FW\" in n]\n        if False:"),
    ("warnings flood (logged every time)", "    if key not in _warned:", "    if True:"),
    ("an outage is logged once per process, not per outage",
     '_warned.difference_update([k for k in _warned if k[0] == "fire"])', "pass"),
    ("pick(): int() on heads.json unguarded", "        except (TypeError, ValueError):", "        except ZeroDivisionError:"),
    ("pick(): channels 0 accepted", "if size <= 0 or ch <= 0:", "if size <= 0:"),
    ("pick(): a non-table tracks entry is not caught", "            if not isinstance(tracks, dict):", "            if False:"),
    ("pick(): a non-table track entry is not caught", "        if not isinstance(t, dict):", "        if False:"),
    # the Fire link must notice a Fire that rebooted (a stale ALSA handle drops messages silently)
    ("replies never read (a stale link looks alive)", "        self._replies(now)\n        if self.inp is not None", "        if self.inp is not None"),
    ("a silent Fire is never declared dead", "if self.inp is not None and now - self.last_reply > self.DEAD_AFTER:", "if False:"),
    ("no hello is ever sent", "if now - self.last_hello > self.HELLO_EVERY:", "if False:"),
    ("any SysEx counts as the Fire's reply", 'list(m.data[:5]) == HDR + [0x00]', "True"),
    ("the Fire's input port is never opened", "self.inp = self.mido.open_input(ins[0]) if ins else None", "self.inp = None"),
    # the crash record
    ("crash fields unpacked in the wrong order", "for x in reversed(list(b)[:5]):", "for x in list(b)[:5]:"),
    ("unpack32 not truncated to 32 bits", "return v & 0xFFFFFFFF ", "return v "),
    ("a crash is logged on every repeat", 'if c["valid"] and key != self._crash_seen:', 'if c["valid"]:'),
    ("an empty record is logged as a crash", 'if c["valid"] and key != self._crash_seen:', 'if key != self._crash_seen:'),
    ("the crash query is never sent", "if not self._crash_asked:", "if False:"),
    ("no new query after a reconnect", "        self._crash_asked = False                   # ...and ask", "        pass                                        # ...and ask"),
    ("the reply version/length pairing is not checked", "or CRASH_REPLY_LENS.get(d[5]) != len(d):", "or len(d) not in (46, 61):"),
    ("the reply length is not checked", "or CRASH_REPLY_LENS.get(d[5]) != len(d):", "or d[5] not in CRASH_REPLY_LENS:"),
    ("a missing ring is never said", "        elif not self._said_none and now - self._none_since >= self.NO_RING_SAY_AFTER:", "        elif False:"),
    ("a missing ring is said every look", "            self._said_none = True\n            print(", "            print("),
    ("the grace period is ignored", "now - self._none_since >= self.NO_RING_SAY_AFTER", "now - self._none_since >= 0"),
    ("the ring's return is never noticed", "        self._ring_ok(d)\n", "\n"),
    ("the stack reply's version is not checked", "d[:5] != HDR + [0x08] or d[5] != 1:", "d[:5] != HDR + [0x08]:"),
    ("an even word passes as a return address", "return APP_LO <= w < APP_HI and w & 1 == 1", "return APP_LO <= w < APP_HI"),
    ("Akai's sectors pass as our code", "APP_LO, APP_HI = 0x08008000, 0x08020000", "APP_LO, APP_HI = 0x08000000, 0x08020000"),
    ("an unknown exception's IRQ number is off by one", '(f" (IRQ {n - 16})"', '(f" (IRQ {n - 15})"'),
    ("the running context is not described", "running: {running_in(c['psr'])}", "running: ?"),
    ("a new crash's full report is never saved", "                            if self.on_crash:", "                            if False:"),
    ("a v2 reply's build id is dropped", "nf = 10 if d[5] >= 2 else 7", "nf = 7"),
]
bad = 0
for name, old, new in MUTS:
    d = tempfile.mkdtemp()
    for f in ("host_vu.py", "host_vu_test.py"):
        shutil.copy(os.path.join(HERE, f), d)
    p = os.path.join(d, "host_vu.py")
    s = open(p, encoding="utf-8").read().replace("\r\n", "\n")
    if s.count(old) != 1:
        print(f"SKIPPED  {name}: anchor found {s.count(old)}x (the mutation did not apply)")
        bad += 1
        shutil.rmtree(d)
        continue
    open(p, "w", encoding="utf-8").write(s.replace(old, new))
    r = subprocess.run([sys.executable, "-B", "host_vu_test.py"], cwd=d, capture_output=True, text=True)
    fails = r.stdout.count("FAIL")
    print(f"{'RED   ' if r.returncode else 'GREEN (NOT CAUGHT)'}  {name}: {fails} check(s) fail")
    bad += r.returncode == 0
    shutil.rmtree(d)
print("all mutations caught" if not bad else f"{bad} problem(s)")
sys.exit(1 if bad else 0)
