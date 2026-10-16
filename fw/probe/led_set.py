"""Light exactly the given output bits (LEDs active low, PA7 low), show a label. Usage: led_set.py "label" bit bit ..."""
import mido, sys, time
label = sys.argv[1]; bits = [int(b) for b in sys.argv[2:]]
out = [0xFF] * 29
for k in bits: out[k >> 3] &= ~(1 << (k & 7))
name = next(n for n in mido.get_output_names() if 'ORC FW probe' in n)
o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x7D, 0x1A, 0]))           # velocity mode off
o.send(mido.Message('sysex', data=[0x7D, 0x13] + [n for b in out for n in (b >> 4, b & 15)]))
o.send(mido.Message('sysex', data=[0x7D, 0x16, 1, 0]))        # PA7 low: LEDs enabled
o.send(mido.Message('sysex', data=[0x7D, 0x01] + list(label.encode()[:20])))
time.sleep(0.2); print('lit', bits)
