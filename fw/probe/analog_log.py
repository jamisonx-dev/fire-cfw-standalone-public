"""Log analog-channel moves (> threshold) and raw input-pin changes from the probe firmware.
Usage (on the Pi): python3 analog_log.py <seconds> > log.txt"""
import mido, sys, time
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 180
TH = 60
name = next(n for n in mido.get_input_names() if 'ORC FW probe' in n)
i = mido.open_input(name)
base = None; pins_prev = None; t0 = time.time()
print(f"# start {time.strftime('%H:%M:%S')}", flush=True)
while time.time() - t0 < secs:
    for m in i.iter_pending():
        if m.type != 'sysex': continue
        d = m.data; t = time.time() - t0
        if d[:2] == (0x7D, 0x15):
            a = [(d[2+3*k] << 8) | (d[3+3*k] << 4) | d[4+3*k] for k in range(16)]
            if base is None: base = a[:]; continue
            moved = [f"p{k//2}/{'PA0' if k % 2 == 0 else 'PA1'} {base[k]}->{a[k]}" for k in range(16) if abs(a[k] - base[k]) > TH]
            if moved:
                print(f"{t:7.2f}s  analog " + "  ".join(moved), flush=True)
                for k in range(16):
                    if abs(a[k] - base[k]) > TH: base[k] = a[k]
        if d[:2] == (0x7D, 0x14):
            pins = [(d[2+4*p] << 12) | (d[3+4*p] << 8) | (d[4+4*p] << 4) | d[5+4*p] for p in range(4)]
            if pins_prev is not None:
                for p, port in enumerate('ABCD'):
                    x = pins[p] ^ pins_prev[p]
                    for b in range(16):
                        if x >> b & 1: print(f"{t:7.2f}s  pin P{port}{b} -> {pins[p] >> b & 1}", flush=True)
            pins_prev = pins
    time.sleep(0.002)
print("# end", flush=True)
