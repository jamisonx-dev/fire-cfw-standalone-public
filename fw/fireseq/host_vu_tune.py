#!/usr/bin/env python3
"""Offline tuning of the VU meter on a captured recording (host_vu_capture.py): replays the windows through
several meter variants, simulates what the Fire's pads do with each stream, and prints how jumpy / lively each is.
    python3 host_vu_tune.py /tmp/vu_capture.npy
"""
import sys

import numpy as np

import host_vu as H

wins = np.load(sys.argv[1] if len(sys.argv) > 1 else "/tmp/vu_capture.npy")
times = np.load((sys.argv[1] if len(sys.argv) > 1 else "/tmp/vu_capture.npy").replace(".npy", ".times.npy"))
N = H.N
win = np.hanning(N).astype("f4")
freq = np.fft.rfftfreq(N, 1.0 / H.RATE)
edges = H.F_LO * (H.F_HI / H.F_LO) ** (np.arange(H.NBANDS + 1) / H.NBANDS)
centre = (edges[:-1] * edges[1:]) ** 0.5
tilt = H.TILT_DB_OCT * np.log2(centre / 1000.0)


def band_bins(min_bins):
    idx = []
    for lo, hi, c in zip(edges[:-1], edges[1:], centre):
        ix = np.nonzero((freq >= lo) & (freq < hi))[0]
        if len(ix) < min_bins:                                  # widen narrow bands around their centre
            k = int(np.argmin(np.abs(freq - c)))
            half = min_bins // 2
            ix = np.arange(max(0, k - half), min(len(freq), k + half + 1))
        idx.append(ix)
    return idx


def run(mode="max", attack=1.0, release=1.0, range_db=H.RANGE_DB, min_bins=1, avg_windows=1, decay_db_s=H.REF_DECAY_DB_S):
    """-> levels (frames, 16) 0..127"""
    idx = band_bins(min_bins)
    ref = H.REF_FLOOR_DB
    s = np.zeros(H.NBANDS)
    out, hist, tprev = [], [], None
    for w, t in zip(wins, times):
        dt = 0.0 if tprev is None else min(1.0, t - tprev)
        tprev = t
        ref = max(H.REF_FLOOR_DB, ref - decay_db_s * dt)
        rms = float(np.sqrt(np.mean(w * w)))
        if rms < H.GATE:
            v = np.zeros(H.NBANDS)
        else:
            mag = np.abs(np.fft.rfft(w * win)) / (N / 4.0)
            hist.append(mag ** 2)
            hist = hist[-avg_windows:]
            p = np.mean(hist, axis=0)                           # power averaged over the last few windows
            if mode == "max":
                b = np.array([np.sqrt(p[ix].max()) for ix in idx])
            else:
                b = np.array([np.sqrt(p[ix].mean()) for ix in idx])
            db = 20 * np.log10(b + 1e-9) + tilt
            ref = max(ref, float(db.max()))
            v = np.clip((db - (ref - range_db)) / range_db, 0, 1)
        k = np.where(v > s, attack, release)
        s = s + (v - s) * k
        out.append(np.round(s * 127))
    return np.array(out)


def fire_sim(levels, fall=1.2, fps=30):
    """What the Fire's bars do with these frames (lights_host = max, lights_step = linear fall), sampled at the
    Fire's frame rate. -> bar heights (frames, 16) in rows (0..4)."""
    bar = np.zeros(H.NBANDS)
    ev = list(zip(times, levels))
    out, ei, t = [], 0, 0.0
    dtf = 1.0 / fps
    while t < times[-1]:
        while ei < len(ev) and ev[ei][0] <= t:
            bar = np.maximum(bar, ev[ei][1] / 127.0)
            ei += 1
        bar = np.maximum(0.0, bar - dtf * fall)
        out.append(bar * 4.0)
        t += dtf
    return np.array(out)


def metrics(levels, fall=1.2):
    jump = float(np.mean(np.abs(np.diff(levels, axis=0))))
    h = fire_sim(levels, fall)
    q = np.round(h * 4) / 4                                    # visible in quarter-row steps (brightness of the top pad)
    changes = float(np.sum(np.abs(np.diff(q, axis=0)) > 0) / (len(q) / 30.0) / H.NBANDS)
    big = float(np.mean(np.abs(np.diff(h, axis=0)) > 0.5)) * 30   # jumps of half a row or more, per second per band
    live = float(np.mean(levels))
    peak = float(np.percentile(levels, 95))
    return jump, changes, big, live, peak


rows = [
    ("A  today: bin max, raw, Fire falls 1.2/s", dict(), 1.2),
    ("B  band energy (3+ bins)", dict(mode="energy", min_bins=3), 1.2),
    ("C  B + attack 0.6 / release 0.25", dict(mode="energy", min_bins=3, attack=0.6, release=0.25), 1.2),
    ("D  B + attack 0.5 / release 0.15", dict(mode="energy", min_bins=3, attack=0.5, release=0.15), 1.2),
    ("E  B + avg 2 windows + att 0.6/rel 0.25", dict(mode="energy", min_bins=3, avg_windows=2, attack=0.6, release=0.25), 1.2),
    ("F  E, range 30 dB", dict(mode="energy", min_bins=3, avg_windows=2, attack=0.6, release=0.25, range_db=30.0), 1.2),
    ("G  E, range 42 dB", dict(mode="energy", min_bins=3, avg_windows=2, attack=0.6, release=0.25, range_db=42.0), 1.2),
    ("H  E, ref relaxes 1 dB/s", dict(mode="energy", min_bins=3, avg_windows=2, attack=0.6, release=0.25, decay_db_s=1.0), 1.2),
    ("I  E + Fire falls 3.0/s", dict(mode="energy", min_bins=3, avg_windows=2, attack=0.6, release=0.25), 3.0),
]
print(f"{len(wins)} windows, {times[-1]:.1f} s of real music\n")
print(f"{'variant':45s} {'raw jump':>8s} {'pad steps/s':>11s} {'big jumps/s':>11s} {'mean lvl':>8s} {'p95 lvl':>7s}")
for name, kw, fall in rows:
    lv = run(**kw)
    j, c, b, m, p = metrics(lv, fall)
    print(f"{name:45s} {j:8.1f} {c:11.1f} {b:11.2f} {m:8.1f} {p:7.0f}")
def shipped():
    m = H.Meter()
    return np.array([m.levels(w, t) for w, t in zip(wins, times)], float)


lv = shipped()
j, c, b, m_, p95 = metrics(lv, 1.2)
print(f"{'S  the SHIPPED host_vu.Meter (what runs now)':45s} {j:8.1f} {c:11.1f} {b:11.2f} {m_:8.1f} {p95:7.0f}")
print("\nraw jump = mean frame-to-frame change of the 0-127 levels sent; pad steps/s = visible brightness/height steps per second per band;")
print("big jumps/s = half-row-or-more jumps per second per band; mean / p95 level = how lively the bars are (don't want it to go dead).")
