"""The Fire's supply voltage and its TIM4 misread counter (firmware v0.33b+), read from the running Fire.
    python3 -B fire_vdd.py <the flashed .elf>            print once
    python3 -B fire_vdd.py <the flashed .elf> --reset    restart the low / high marks (07 01) and the loop-time mark first
    python3 -B fire_vdd.py <the flashed .elf> --watch 60 print every 60 s until stopped (one line each, for a log)

The supply: the chip reads its own 1.21 V reference against VDDA 200 times a second (fire_hw.c fire_knobs_poll);
VDD = 1.21 x 4095 / raw. The reference is 1.21 V typical (1.18-1.24 between chips), so the absolute figure is good to
about 3 %; the DIFFERENCE between the highest and the lowest reading is exact. 2 flash wait states need 2.7 V at 84 MHz.
The misread counter: the TIM4 note of host 06 (kind 4 = TIM4 was handed an impossible entry address: the 2026-11-11
crashes, counted instead of resetting since v0.33b). Opens its own port; reads only."""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    sys.path.insert(0, os.path.expanduser("~/orc/orc1"))
    import orc1_midisafe
    orc1_midisafe.install()
except Exception:
    pass
import mido
import host_vu as H

HDR = [0x7D, 0x4F, 0x52, 0x43]
VREF = 1.21


def volts(raw):
    return VREF * 4095.0 / raw if raw else 0.0


def main(a):
    elf = a[0]
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout
    addr = next(int(l.split()[0], 16) for l in out.splitlines() if l.split()[-1] == "vdd_stat")
    o = mido.open_output(next(n for n in mido.get_output_names() if ("FLAME seq" in n or "ORC FW seq" in n)))
    i = mido.open_input(next(n for n in mido.get_input_names() if ("FLAME seq" in n or "ORC FW seq" in n)))

    def ask(data, want, timeout=1.0):
        for _ in i.iter_pending():
            pass
        o.send(mido.Message("sysex", data=HDR + data))
        end = time.time() + timeout
        while time.time() < end:
            for m in i.iter_pending():
                if m.type == "sysex" and list(m.data[:5]) == HDR + [want]:
                    return list(m.data)
            time.sleep(0.002)
        return None

    def word(x):
        d = ask([0x09] + [(x >> (7 * k)) & 0x7F for k in range(5)], 0x09)
        if not d:
            raise SystemExit("no answer to a peek (is this the ELF of the image on the Fire?)")
        w = d[10:15]
        return sum((w[k] & 0x7F) << (7 * k) for k in range(5)) & 0xFFFFFFFF

    def once():
        w0, w1, n = word(addr), word(addr + 4), word(addr + 8)
        last, hi, lo = w0 & 0xFFFF, w0 >> 16, w1 & 0xFFFF
        c = H.decode_crash(ask([0x06], 0x06) or []) or {}
        st = ask([0x07], 0x07)
        loop = ((st[23] << 7) | st[24]) if st and len(st) >= 25 else None
        t4 = f"{c.get('t4_count', 0)} TIM4 note(s)" + (f", last kind {c.get('t4_kind')} at {c.get('t4_ms', 0) / 1000:.0f} s up" if c and c.get("t4_count") else "")
        crash = "CRASH ON RECORD" if c and c.get("valid") else "no crash on record"
        if n < 17 or lo == 0xFFFF:
            sup = f"supply: now {volts(last):.3f} V (raw {last}); no low / high yet ({n} readings)"
        else:
            sup = (f"supply: now {volts(last):.3f} V, lowest {volts(hi):.3f} V, highest {volts(lo):.3f} V "
                   f"(swing {1000 * (volts(lo) - volts(hi)):.0f} mV; raw {last} / {hi} / {lo}; {n} readings)")
        return f"{time.strftime('%H:%M:%S')} {sup}; {t4}; {crash}" + (f"; longest loop pass {loop} us" if loop is not None else "")

    if "--reset" in a:
        ask([0x07, 0x01], 0x07)
        time.sleep(0.5)
    if "--watch" in a:
        every = float(a[a.index("--watch") + 1])
        while True:
            print(once(), flush=True)
            time.sleep(every)
    print(once())


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
