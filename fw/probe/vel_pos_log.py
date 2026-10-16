"""Combined velocity experiment logger: per selector position, PA1 resting level -> peak for each pad press."""
import mido, sys, time
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 180
name = next(n for n in mido.get_input_names() if 'ORC FW probe' in n)
i = mido.open_input(name); o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x7D, 0x13] + [0xF] * 58))      # LEDs off
o.send(mido.Message('sysex', data=[0x7D, 0x16, 1, 0]))             # LED drivers enabled
o.send(mido.Message('sysex', data=[0x7D, 0x1A, 1]))                # velocity mode (combined setup)
o.send(mido.Message('sysex', data=[0x7D, 0x01] + list(b'VEL v2: soft/med/hard')))
def v12(d, k): return (d[k] << 8) | (d[k + 1] << 4) | d[k + 2]
t0 = time.time()
print(f"# start {time.strftime('%H:%M:%S')}   columns = selector position 0..7: rest -> peak (gain)", flush=True)
while time.time() - t0 < secs:
    for m in i.iter_pending():
        if m.type == 'sysex' and m.data[:2] == (0x7D, 0x1B):
            d = m.data
            base = [v12(d, 3 + 3 * p) for p in range(8)]
            peak = [v12(d, 27 + 3 * p) for p in range(8)]
            cols = '  '.join(f"{b:4d}>{pk:4d}({pk - b:+5d})" for b, pk in zip(base, peak))
            print(f"{time.time() - t0:7.2f}s pad {d[2] >> 3}.{d[2] & 7}  {cols}", flush=True)
    time.sleep(0.002)
print("# end", flush=True)
