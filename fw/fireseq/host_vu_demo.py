"""Demo of the Fire's host light protocol: hello, host lights on, 16 moving VU levels at 30 fps, then off."""
import math, sys, time
import mido

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 10
name = next(n for n in mido.get_output_names() if "ORC FW" in n)
iname = next(n for n in mido.get_input_names() if "ORC FW" in n)
out, inp = mido.open_output(name), mido.open_input(iname)
HDR = [0x7D, 0x4F, 0x52, 0x43]

out.send(mido.Message("sysex", data=HDR + [0x00]))
time.sleep(0.2)
print("hello reply:", [m.data for m in inp.iter_pending()])
out.send(mido.Message("sysex", data=HDR + [0x02, 1]))            # host lights on
t0 = time.time()
n = 0
while time.time() - t0 < secs:
    t = time.time() - t0
    lv = []
    for c in range(16):
        # a few travelling humps + a kick-like pulse every half second
        v = 0.5 + 0.5 * math.sin(c * 0.6 - t * 3.0)
        v *= 0.55 + 0.45 * math.sin(t * 1.3 + c * 0.2)
        if (t % 0.5) < 0.08 and c < 5:
            v = max(v, 1.0 - c * 0.12)
        lv.append(max(0, min(127, int(v * 127))))
    out.send(mido.Message("sysex", data=HDR + [0x01] + lv))
    n += 1
    time.sleep(1 / 30)
out.send(mido.Message("sysex", data=HDR + [0x02, 0]))            # host lights off
print("sent", n, "level frames; host lights off")
