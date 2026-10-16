#!/usr/bin/env python3
"""Records the audio windows host_vu.py would analyse (the same ring, the same cadence) to a .npy file, so the
meter can be tuned offline on REAL music instead of guesses. Read-only; idle priority.
    python3 host_vu_capture.py [seconds] [out.npy]
Writes an array of shape (frames, 2048) plus a second file out.times.npy with each window's arrival time (s).
"""
import sys
import time

import numpy as np

import host_vu as H

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 12.0
out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/vu_capture.npy"
H.idle_priority()
src = H.RingSource()
wins, times = [], []
t0 = time.monotonic()
while time.monotonic() - t0 < secs:
    now = time.monotonic()
    m = src.read(now)
    if m is not None:
        wins.append(m.copy())
        times.append(now - t0)
    time.sleep(0.015)
np.save(out, np.array(wins, "f4"))
np.save(out.replace(".npy", ".times.npy"), np.array(times))
print(f"captured {len(wins)} windows in {secs:.0f} s -> {out}  (ring: {src.dir})")
if len(times) > 2:
    d = np.diff(times) * 1000
    print(f"arrival gap ms: median {np.median(d):.0f}  min {d.min():.0f}  max {d.max():.0f}")
    rms = [float(np.sqrt(np.mean(w * w))) for w in wins]
    print(f"rms dBFS: min {20*np.log10(min(rms)+1e-9):.0f}  median {20*np.log10(np.median(rms)+1e-9):.0f}  max {20*np.log10(max(rms)+1e-9):.0f}")
