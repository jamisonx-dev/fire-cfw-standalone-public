"""Record every note-on for N seconds and flag timing hiccups.
Steps land on a 16th grid; the gap between consecutive note events should be a whole number of
steps. Any gap that misses its nearest whole step by > 3 ms is a hiccup.
Usage (on the Pi, while the Fire plays): python3 hitch_test.py [seconds]  (keep swing at 50 %)"""
import mido, sys, time, statistics
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 90
name = next(n for n in mido.get_input_names() if 'ORC FW seq' in n)
i = mido.open_input(name)
t, last = [], None
spreads, cur0 = [], None
t0 = time.time()
while time.time() - t0 < secs:
    for m in i.iter_pending():
        if m.type == 'note_on' and m.velocity:
            now = time.time()
            if last is None or now - last > 0.008:       # a new step (notes within 8 ms = one step)
                t.append(now); cur0 = now
            else:
                spreads.append((now - cur0) * 1000)
            last = now
    time.sleep(0.0002)
gaps = [(b - a) * 1000 for a, b in zip(t, t[1:])]
if len(gaps) < 8: sys.exit(f"only {len(t)} step events: is it playing?")
import json; json.dump(t, open('hitch_times.json', 'w'))
lo = min(g for g in gaps if g > 20)
step = statistics.median([g for g in gaps if g < lo * 1.5])   # typical one-step gap
bad = []
for k, g in enumerate(gaps):
    n = max(1, round(g / step))
    err = g - n * step
    if abs(err) > 2: bad.append((round(t[k + 1] - t0, 2), round(g, 1), n, round(err, 1)))
print(f"{len(t)} step events in {secs:.0f} s, one step ~ {step:.1f} ms")
if spreads: print(f"notes within a step: worst spread {max(spreads):.2f} ms, {sum(1 for x in spreads if x > 0.5)} of {len(spreads)} extra notes more than 0.5 ms after the first")
errs = sorted(abs(g - max(1, round(g / step)) * step) for g in gaps)
print(f"hiccups (> 2 ms off the grid): {len(bad)}   |  typical error: median {errs[len(errs)//2]:.2f} ms, 99th pct {errs[len(errs)*99//100]:.2f} ms, worst {errs[-1]:.2f} ms")
for b in bad[:20]: print(f"   at {b[0]:6.2f}s  gap {b[1]} ms = {b[2]} steps {b[3]:+} ms")
