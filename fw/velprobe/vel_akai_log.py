"""Log the velprobe firmware: one line per pad press, in Akai's own units.

Run on the Pi with the velprobe image on the Fire:  python3 vel_akai_log.py [seconds] [--no-drivers] [--idle]

Columns: rest = mean filtered level before the press (16-bit scale, i.e. ADC counts x16),
         peak = highest filtered value in the next 30 ms, peak1 = same for y[1] only (Akai's gate: must pass 280),
         vel  = what Akai's mapping would send.  Akai's own reference (stock firmware, same pad, measured
         2026-10-06): soft taps 32, medium ~54-63, hard up to ~123.
"""
import mido, sys, time

args = [a for a in sys.argv[1:] if not a.startswith('--')]
secs = float(args[0]) if args else 180
show_idle = '--idle' in sys.argv
name = next(n for n in mido.get_input_names() if 'ORC FW velprobe' in n)
i = mido.open_input(name)
o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x7D, 0x13] + [0xF] * 58))          # all LEDs off
if '--no-drivers' not in sys.argv:
    o.send(mido.Message('sysex', data=[0x7D, 0x16, 1, 0]))             # PA7 low = LED drivers enabled
o.send(mido.Message('sysex', data=[0x7D, 0x01] + list(b'VEL: soft/med/hard')))
o.send(mido.Message('sysex', data=[0x7D, 0x1A, 1]))                    # start measuring


def n16(d, k):
    return (d[k] << 12) | (d[k + 1] << 8) | (d[k + 2] << 4) | d[k + 3]


t0 = time.time()
print(f"# start {time.strftime('%H:%M:%S')}  rest/peak/peak1 in Akai units (ADC counts x16); gate = 280", flush=True)
try:
    while time.time() - t0 < secs:
        for m in i.iter_pending():
            if m.type != 'sysex' or tuple(m.data[:4]) != (0x7D, 0x4F, 0x52, 0x43):
                continue
            d = (0x7D,) + tuple(m.data[4:])          # drop 'ORC' so the offsets below stay as documented
            if d[1] == 0x1C:
                rest, peak, peak1 = n16(d, 3), n16(d, 7), n16(d, 11)
                vel = (d[15] << 4) | d[16]
                nfr = (d[17] << 4) | d[18]
                print(f"{time.time() - t0:7.2f}s pad {d[2] >> 3}.{d[2] & 7}  rest {rest:5d}  peak {peak:5d} "
                      f"(+{peak - rest:5d})  peak1 {peak1:5d}  -> vel {vel:3d}   [{nfr} frames]", flush=True)
            elif d[1] == 0x1D and show_idle:
                print("   idle slots(code0..7):", ' '.join(f"{n16(d, 2 + 4 * k):5d}" for k in range(8)), flush=True)
        time.sleep(0.002)
finally:
    o.send(mido.Message('sysex', data=[0x7D, 0x1A, 0]))
print("# end", flush=True)
