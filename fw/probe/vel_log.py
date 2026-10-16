"""Velocity capture v3: baseline, raw peak, smoothed peak, steepest rise per pad press."""
import mido, sys, time
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 180
name = next(n for n in mido.get_input_names() if 'ORC FW probe' in n)
i = mido.open_input(name); o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x7D, 0x13] + [0xF] * 58))
o.send(mido.Message('sysex', data=[0x7D, 0x16, 1, 0]))
o.send(mido.Message('sysex', data=[0x7D, 0x1A, 1]))
o.send(mido.Message('sysex', data=[0x7D, 0x01] + list(b'PADS soft/med/hard')))
BARS = " .:-=+*#%@"
t0 = time.time()
print(f"# start {time.strftime('%H:%M:%S')}", flush=True)
def v12(d, k): return (d[k] << 8) | (d[k+1] << 4) | d[k+2]
while time.time() - t0 < secs:
    for m in i.iter_pending():
        if m.type == 'sysex' and m.data[:2] == (0x7D, 0x19):
            d = m.data; pad = d[2]
            base, raw, smooth, rise = (v12(d, 3 + 3 * k) for k in range(4))
            pts = [v12(d, 15 + 3 * k) for k in range(64)]
            top = max(max(pts), 1)
            spark = ''.join(BARS[min(9, p * 10 // (top + 1))] for p in pts)
            print(f"{time.time()-t0:7.2f}s pad {pad >> 3}.{pad & 7}  base {base:4d}  raw {raw:4d}  smooth {smooth:4d}  rise {rise:4d}  |{spark}|", flush=True)
    time.sleep(0.002)
print("# end", flush=True)
