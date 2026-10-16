#!/usr/bin/env python3
"""How the TOPS of the Fire's VU bars move, for different Fire-side behaviours, on a captured recording.
The Fire draws a bar of height h (0..1 of 4 rows) as whole pads + a partly lit top pad (brightness = coverage),
and a dim "peak" pad above it. What reads as "jumpy at the tops" is the top edge changing too often:
  row hops/s    how often the lit-pad count of a bar changes (per bar)
  top steps/s   how often the top pad's brightness changes by a visible step (a quarter of its range) (per bar)
  peak hops/s   how often the peak dot moves (per bar)
    python3 host_vu_tops.py /tmp/vu_capture.npy
"""
import sys

import numpy as np

import host_vu as H

path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/vu_capture.npy"
wins = np.load(path)
times = np.load(path.replace(".npy", ".times.npy"))
meter = H.Meter()
host = np.array([meter.levels(w, t) for w, t in zip(wins, times)], float) / 127.0     # what is sent (0..1)
FPS = 30
dtf = 1.0 / FPS
T = int(times[-1] * FPS)


def sim(mode, fall=1.2, up_tau=0.04, down_tau=0.12, peak_hold=0.0, peak_fall=0.45):
    """-> bar heights h (T, 16) in rows 0..4 and peak heights (T, 16) in rows"""
    bar = np.zeros(16)
    target = np.zeros(16)
    peak = np.zeros(16)
    hold = np.zeros(16)
    ei, hs, ps = 0, [], []
    for f in range(T):
        t = f * dtf
        while ei < len(times) and times[ei] <= t:
            target = host[ei]
            if mode == "max":
                bar = np.maximum(bar, target)                    # today: jump up to the level
            ei += 1
        if mode == "max":
            bar = np.maximum(0.0, bar - dtf * fall)              # ... and fall in a straight line
        else:                                                    # follow the sent level smoothly both ways
            k_up, k_dn = 1 - np.exp(-dtf / up_tau), 1 - np.exp(-dtf / down_tau)
            bar = bar + (target - bar) * np.where(target > bar, k_up, k_dn)
        hold = np.where(bar >= peak, peak_hold, np.maximum(0.0, hold - dtf))
        peak = np.where(bar >= peak, bar, np.where(hold > 0, peak, np.maximum(bar, peak - dtf * peak_fall)))
        hs.append(bar * 4.0)
        ps.append(peak * 4.0)
    return np.array(hs), np.array(ps)


def stats(h, p):
    secs = len(h) / FPS
    rows = np.ceil(h - 1e-6)                                     # pads lit
    frac = h - np.floor(h)
    top = np.round(frac * 4)                                     # the top pad's brightness in quarters
    row_hops = np.sum(np.diff(rows, axis=0) != 0) / secs / 16
    top_steps = np.sum(np.diff(top, axis=0) != 0) / secs / 16
    pk = np.floor(p - 1e-3)
    peak_hops = np.sum(np.diff(pk, axis=0) != 0) / secs / 16
    return row_hops, top_steps, peak_hops, float(np.mean(h) / 4)


print(f"{len(host)} host frames over {times[-1]:.1f} s of real music\n")
print(f"{'Fire-side behaviour':54s} {'row hops/s':>10s} {'top steps/s':>11s} {'peak hops/s':>11s} {'mean h':>7s}")
for name, kw in [
    ("today: jump up (max), fall 1.2/s straight", dict(mode="max")),
    ("track the level: up 40 ms / down 120 ms", dict(mode="track")),
    ("track: up 60 ms / down 160 ms", dict(mode="track", up_tau=0.06, down_tau=0.16)),
    ("track: up 80 ms / down 220 ms", dict(mode="track", up_tau=0.08, down_tau=0.22)),
    ("track 60/160 + peak holds 0.4 s, falls 0.8 row/s", dict(mode="track", up_tau=0.06, down_tau=0.16, peak_hold=0.4, peak_fall=0.2)),
]:
    h, p = sim(**kw)
    r, tstep, pkh, m = stats(h, p)
    print(f"{name:54s} {r:10.1f} {tstep:11.1f} {pkh:11.1f} {m:7.2f}")
