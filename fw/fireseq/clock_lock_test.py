"""Check that the Fire's MIDI clock stays locked to its own notes (run on the Pi while it plays
with clock out on and the kick on step 1 of each beat, swing at 50 %).

For each kick note-on, find the nearest clock tick and record (kick time - tick time). If the
clock drifted against the notes, that offset would creep steadily; locked, it stays flat.
Usage: python3 clock_lock_test.py [seconds]
"""
import mido, sys, time, statistics
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
name = next(n for n in mido.get_input_names() if 'ORC FW seq' in n)
i = mido.open_input(name)
ticks, kicks = [], []
t0 = time.time()
while time.time() - t0 < secs:
    for m in i.iter_pending():
        now = time.time()
        if m.type == 'clock': ticks.append(now)
        elif m.type == 'note_on' and m.velocity and m.note == 36: kicks.append(now)
    time.sleep(0.0002)
print(f"{len(ticks)} ticks, {len(kicks)} kicks in {secs:.0f} s")
if len(ticks) > 30 and len(kicks) > 4:
    d = [(b - a) * 1000 for a, b in zip(ticks, ticks[1:])]
    med = statistics.median(d)
    print(f"clock: median tick {med:.3f} ms = {60000 / (med * 24):.2f} BPM")
    offs = []
    for k in kicks:
        nearest = min(ticks, key=lambda t: abs(t - k))
        offs.append((k - nearest) * 1000)
    half = len(offs) // 2
    print(f"kick - nearest tick (ms): first half median {statistics.median(offs[:half]):+.2f}, "
          f"second half median {statistics.median(offs[half:]):+.2f}, spread {min(offs):+.2f}..{max(offs):+.2f}")
