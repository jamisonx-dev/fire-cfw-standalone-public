"""Velocity capture v2: per-selector-position PA1 peaks for each pad press."""
import mido, sys, time
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 180
name = next(n for n in mido.get_input_names() if 'ORC FW probe' in n)
i = mido.open_input(name); o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x7D, 0x13] + [0x0] * 58))
o.send(mido.Message('sysex', data=[0x7D, 0x16, 1, 0]))
o.send(mido.Message('sysex', data=[0x7D, 0x1A, 1]))
o.send(mido.Message('sysex', data=[0x7D, 0x01] + list(b'LEDS ON: pads s/h')))
BARS = " .:-=+*#%@"
t0 = time.time()
print(f"# start {time.strftime('%H:%M:%S')}", flush=True)
while time.time() - t0 < secs:
    for m in i.iter_pending():
        if m.type == 'sysex' and m.data[:2] == (0x7D, 0x19):
            d = m.data; pad = d[2]
            peaks = [(d[3+3*p] << 8) | (d[4+3*p] << 4) | d[5+3*p] for p in range(8)]
            best = d[27]; rate = ((d[28] << 7) | d[29]) / 10
            pts = [(d[30+3*k] << 8) | (d[31+3*k] << 4) | d[32+3*k] for k in range(64)]
            top = max(pts) or 1
            spark = ''.join(BARS[min(9, p * 10 // (top + 1))] for p in pts)
            print(f"{time.time()-t0:7.2f}s pad {pad >> 3}.{pad & 7}  peaks {' '.join(f'{v:4d}' for v in peaks)}  best p{best}  {rate:.1f} sw/ms  |{spark}|", flush=True)
    time.sleep(0.002)
print("# end", flush=True)
