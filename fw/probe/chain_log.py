"""Log every bit change of the Fire's input chain (probe firmware) with timestamps.
Usage (on the Pi): python3 chain_log.py <seconds> > log.txt"""
import mido, sys, time
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 300
name = next(n for n in mido.get_input_names() if 'ORC FW probe' in n)
i = mido.open_input(name); o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x7D, 0x11, 0]))          # latch style 0
prev = None; t0 = time.time()
print(f"# start {time.strftime('%H:%M:%S')}", flush=True)
while time.time() - t0 < secs:
    for m in i.iter_pending():
        if m.type != 'sysex' or m.data[:2] != (0x7D, 0x10): continue
        d = m.data[3:]; cur = bytes((d[2*k] << 4) | d[2*k+1] for k in range(29))
        if prev is not None and cur != prev:
            for k in range(29):
                x = cur[k] ^ prev[k]
                for b in range(7, -1, -1):
                    if x >> b & 1:
                        print(f"{time.time()-t0:7.2f}s  byte {k:2d} bit {b}  -> {cur[k] >> b & 1}", flush=True)
        prev = cur
    time.sleep(0.002)
print("# end", flush=True)
