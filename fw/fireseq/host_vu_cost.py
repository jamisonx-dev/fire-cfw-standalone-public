#!/usr/bin/env python3
"""Measures what host_vu.py costs while it is really analysing audio: a FAKE skipback ring holds a loud
two-tone signal and its ring.pos advances like the recorder's (every ~43 ms); the sender runs against it
(--dry, no MIDI). Reports steady-state CPU of the sender (startup excluded) and the bars it drew.
Touches nothing on the real rig:  python3 host_vu_cost.py [seconds]
"""
import json
import os
import resource
import subprocess
import sys
import tempfile
import time

import numpy as np

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 12.0
here = os.path.dirname(os.path.abspath(__file__))
frames = 48000 * 10
t = np.arange(frames)
wave = (6000 * np.sin(2 * np.pi * 220 * t / 48000) + 4000 * np.sin(2 * np.pi * 3000 * t / 48000)
        + 2000 * np.random.default_rng(1).standard_normal(frames)).astype("<i2")
with tempfile.TemporaryDirectory() as root:
    d = os.path.join(root, "t0")
    os.makedirs(d)
    pcm = np.empty(frames * 2, "<i2")
    pcm[0::2] = pcm[1::2] = wave
    open(os.path.join(d, "ring.raw"), "wb").write(pcm.tobytes())
    open(os.path.join(d, "ring.pos"), "w").write("0")
    json.dump({"tracks": {"0": {"name": "MAIN", "store": "ram", "channels": 2, "dir": "t0", "buffer_bytes": frames * 4}}},
              open(os.path.join(root, "heads.json"), "w"))
    p = subprocess.Popen([sys.executable, os.path.join(here, "host_vu.py"), "--dry", "--print", "--root", root],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1.0)                                       # let it start (imports) before the clock starts
    cpu0 = float(open(f"/proc/{p.pid}/stat").read().split()[13]) + float(open(f"/proc/{p.pid}/stat").read().split()[14])
    t0, pos = time.time(), 0
    while time.time() - t0 < secs:
        pos = (pos + 2048 * 4) % (frames * 4)             # ~43 ms of audio per tick
        open(os.path.join(d, "ring.pos"), "w").write(str(pos))
        time.sleep(0.0427)
    st = open(f"/proc/{p.pid}/stat").read().split()
    cpu1 = float(st[13]) + float(st[14])
    rss = int(open(f"/proc/{p.pid}/status").read().split("VmRSS:")[1].split()[0])
    wall = time.time() - t0
    p.terminate()
    out = p.communicate()[0].strip().splitlines()
    hz = os.sysconf("SC_CLK_TCK")
    print("\n".join(out[-4:]))
    print(f"steady state: {(cpu1 - cpu0) / hz:.2f} s of CPU in {wall:.1f} s = {(cpu1 - cpu0) / hz / wall * 100:.1f}% of one core, RSS {rss // 1024} MB")
