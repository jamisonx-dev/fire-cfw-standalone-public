"""Run the probe's LED walk and record output bit -> pressed input. Usage: python3 walk_log.py [start_k]"""
import mido, sys, time, json
start = int(sys.argv[1]) if len(sys.argv) > 1 else 0
name = next(n for n in mido.get_input_names() if 'ORC FW probe' in n)
i = mido.open_input(name); o = mido.open_output(name)
o.send(mido.Message('sysex', data=[0x7D, 0x17, start >> 7, start & 127]))
print(f"# walk from {start} at {time.strftime('%H:%M:%S')}", flush=True)
pairs = {}
try:
    pairs = json.load(open('walk_map.json'))
except Exception:
    pass
t_end = time.time() + 30 * 60
while time.time() < t_end:
    for m in i.iter_pending():
        if m.type == 'sysex' and m.data[:2] == (0x7D, 0x18):
            k = (m.data[2] << 7) | m.data[3]; inp = (m.data[4] << 7) | m.data[5]
            pairs[str(k)] = None if inp == 0x3FFF else inp
            json.dump(pairs, open('walk_map.json', 'w'))
            print(f"{time.strftime('%H:%M:%S')} out {k:3d} (byte {k >> 3:2d} bit {k & 7}) -> "
                  + ("none" if inp == 0x3FFF else f"input {inp >> 3}.{inp & 7}"), flush=True)
            if k >= 231:
                print('# done', flush=True); sys.exit(0)
    time.sleep(0.005)
