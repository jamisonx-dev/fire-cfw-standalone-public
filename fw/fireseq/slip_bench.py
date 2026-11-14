#!/usr/bin/env python3
"""Bench test of the v0.29d TIM4 stack check on a REAL Fire running a CRASH_TEST build of it (main.c host_cmd 7E 06 / 07).
Fakes the two failures the check exists for and confirms each is recorded as cause 8 (TIM4 stack slip) with the right
kind, SPs and snapshot, and that the Fire restarts by itself. Never three early crashes in a row (no DFU). Run on the Pi:

    python3 slip_bench.py [fireseq.elf]

It starts by checking the record is EMPTY (a real record is never overwritten: save it with host_crash.py first),
and clears the record at the end.
"""
import subprocess
import sys
import time

import mido

import host_vu as H

passed = failed = 0


# (copied from crash_bench.py: importing it would RUN its test, which ends by sending the Fire to DFU)
def usb():
    return subprocess.run(["lsusb"], capture_output=True, text=True).stdout


def ports():
    try:                                    # (while the Fire re-enumerates, ALSA can list a port that is already gone)
        outs = [n for n in mido.get_output_names() if ("FLAME seq" in n or "ORC FW seq" in n)]
        ins = [n for n in mido.get_input_names() if ("FLAME seq" in n or "ORC FW seq" in n)]
        return (mido.open_output(outs[0]), mido.open_input(ins[0])) if outs and ins else (None, None)
    except Exception:
        return None, None


def wait_alive(timeout):
    """-> seconds until the Fire answers a hello again (opening FRESH ports each time), or None."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        out, inp = ports()
        if out:
            list(inp.iter_pending())
            out.send(mido.Message("sysex", data=H.HDR + [0x00]))
            t1 = time.time()
            while time.time() - t1 < 0.4:
                for m in inp.iter_pending():
                    if m.type == "sysex" and list(m.data[:5]) == H.HDR + [0x00]:
                        out.close(); inp.close()
                        return time.time() - t0
                time.sleep(0.01)
            out.close(); inp.close()
        time.sleep(0.2)
    return None


def ask(clear=False):
    out, inp = ports()
    list(inp.iter_pending())
    out.send(mido.Message("sysex", data=H.HDR + ([0x06, 0x01] if clear else [0x06])))
    t0, c = time.time(), None
    while time.time() - t0 < 1.5 and c is None:
        for m in inp.iter_pending():
            if m.type == "sysex":
                c = H.decode_crash(m.data) or c
        time.sleep(0.01)
    out.close(); inp.close()
    return c


def check(name, ok, detail=""):
    global passed, failed
    passed, failed = passed + bool(ok), failed + (not ok)
    print(f"  {'ok  ' if ok else 'FAIL'} {name}" + ("" if ok else f"  {detail}"))


def ask_stack():
    out, inp = ports()
    list(inp.iter_pending())
    out.send(mido.Message("sysex", data=H.HDR + [0x08]))
    t0, st = time.time(), None
    while time.time() - t0 < 1.5 and st is None:
        for m in inp.iter_pending():
            if m.type == "sysex":
                st = H.decode_stack(m.data) or st
        time.sleep(0.01)
    out.close(); inp.close()
    return st


def poke(cmd):
    out, inp = ports()
    out.send(mido.Message("sysex", data=H.HDR + [0x7E, cmd]))
    out.close(); inp.close()


ONLY_B = "B" in sys.argv[1:]                # slip_bench.py B: part B only (after a run that stopped between A and B)
print("start: the Fire answers and has no record (a real one must be saved first)")
check("the Fire answers", wait_alive(10) is not None, usb())
c = ask()
if c is None or c["valid"]:
    print("  STOP: " + ("no reply" if c is None else "a crash is on record: " + H.describe_crash(c) + " -- save it (host_crash.py --save), then clear"))
    sys.exit(1)
if not ONLY_B:
    print("  (waiting 35 s so a crash is not 'early')")
    time.sleep(35)
    c = ask()
    check("no slip recorded while it ran normally for 35 s (the checks give no false alarm)", c is not None and not c["valid"], c)

    print("\nA. TIM4's stack pointer moved by 8 bytes (7E 06)")
    poke(0x06)
    t = wait_alive(40)
    check("it restarted by itself and answers again", t is not None, usb())
    c, st = ask(), ask_stack()
    print("    ", H.describe_crash(c) if c else "no reply")
    check("recorded as cause 8, kind 3 (seen after the SPI chain)", c and c["valid"] and c["cause"] == 8 and c["cfsr"] == 3, c)
    check("...SP now = SP at the start - 8", c and c.get("sp") == c["hfsr"] - 8, c)
    check("...TIM4's frame (entry SP - its start SP) is a sane size", c and 64 <= c["addr"] - c["hfsr"] <= 256, c)
    check("...the interrupted code's PC is in the app and its xPSR has the Thumb bit",
          c and H.APP_LO <= c["pc"] < H.APP_HI and c["psr"] & (1 << 24), c)
    check("...the snapshot starts at the lower SP (SP now)", st and st["valid"] and c and st["base"] == c.get("sp"), st and hex(st["base"]))

    print("\n   (waiting 35 s so the next crash is not 'early')")
    time.sleep(35)

print("\nB. TIM4's saved return address overwritten with 0x2e (7E 07), like the v0.29c crashes")
poke(0x07)
t = wait_alive(40)
check("it restarted by itself and answers again", t is not None, usb())
c, st = ask(), ask_stack()
print("    ", H.describe_crash(c) if c else "no reply")
check("recorded as cause 8, kind 5 (saved return changed), SP unmoved", c and c["valid"] and c["cause"] == 8 and c["cfsr"] == 5
      and c.get("sp") == c["hfsr"], c)
if st and st["valid"] and c:
    i = (c["addr"] - 4 - st["base"]) // 4
    check("...the snapshot shows 0x2e in the saved-return slot (entry SP - 4)", 0 <= i < len(st["words"]) and st["words"][i] == 0x2E,
          [hex(w) for w in st["words"][max(0, i - 2):i + 3]])
else:
    check("...a stack snapshot was kept", False, st)

ask(clear=True)
c = ask()
check("cleared at the end", c is not None and not c["valid"], c)
print(f"\nslip_bench: {passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
