"""LED enable hunt: try PA2/PA7 levels with chain outputs all-0 and all-1. Step label shown on the OLED."""
import mido, time, sys
delay = float(sys.argv[1]) if len(sys.argv) > 1 else 30
name = next(n for n in mido.get_output_names() if 'ORC FW probe' in n)
o = mido.open_output(name)
def label(t): o.send(mido.Message('sysex', data=[0x7D, 0x01] + list(t.encode())))
def pin(i, v): o.send(mido.Message('sysex', data=[0x7D, 0x16, i, v]))
def out(nib): o.send(mido.Message('sysex', data=[0x7D, 0x13] + [nib] * 58))
label('starting soon...')
time.sleep(delay)
steps = [(0, 1, 0x0), (0, 1, 0xF), (1, 0, 0x0), (1, 0, 0xF), (0, 0, 0x0), (0, 0, 0xF), (1, 1, 0x0)]
for n, (pa2, pa7, nib) in enumerate(steps, 1):
    pin(0, pa2); pin(1, pa7); out(nib)
    label(f'STEP {n} A2={pa2} A7={pa7} {"00" if nib == 0 else "FF"}')
    print(f"{time.strftime('%H:%M:%S')} step {n}: PA2={pa2} PA7={pa7} out={'00' if nib == 0 else 'FF'}", flush=True)
    time.sleep(6)
pin(0, 1); pin(1, 1); out(0xF); label('done (back to stock)')
print('done', flush=True)
