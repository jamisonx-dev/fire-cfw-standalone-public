"""Record the sequencer's note-ons for N seconds and report step-timing jitter."""
import mido, sys, time, statistics
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
name = next(n for n in mido.get_input_names() if 'ORC FW seq' in n)
i = mido.open_input(name)
t0 = time.time(); kicks = []; notes = 0
while time.time() - t0 < secs:
    for m in i.iter_pending():
        if m.type == 'note_on' and m.velocity:
            notes += 1
            if m.note == 36: kicks.append(time.time())
    time.sleep(0.0005)
print(f"notes {notes}, kicks {len(kicks)}")
if len(kicks) > 3:
    d = [(b - a) * 1000 for a, b in zip(kicks, kicks[1:])]
    print(f"kick interval ms: median {statistics.median(d):.2f}  min {min(d):.2f}  max {max(d):.2f}  stdev {statistics.pstdev(d):.2f}")
