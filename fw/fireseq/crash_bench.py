#!/usr/bin/env python3
"""Bench test of the crash recorder on a REAL Fire running a CRASH_TEST build (the shipped image has no test hook).
It deliberately crashes the Fire and checks, each time, that it restarted by itself, what it recorded, and that a crash
loop (3 early crashes in a row) falls back to DFU. Run on the Pi:

    python3 crash_bench.py [fireseq.elf]        # the ELF of the bench build, to turn PC into a source line

CRASH_TEST build = the normal source with `#define CRASH_TEST 1` on its first line (see main.c host_cmd 7E).
"""
import subprocess
import sys
import time

import mido

import host_vu as H

ELF = sys.argv[1] if len(sys.argv) > 1 else None
passed = failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  ok   {name}")
    else:
        failed += 1
        print(f"  FAIL {name}  {detail}")


def usb():
    return subprocess.run(["lsusb"], capture_output=True, text=True).stdout


def ports():
    outs = [n for n in mido.get_output_names() if "ORC FW" in n]
    ins = [n for n in mido.get_input_names() if "ORC FW" in n]
    return (mido.open_output(outs[0]), mido.open_input(ins[0])) if outs and ins else (None, None)


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


def addr2line(pc):
    if not ELF:
        return ""
    r = subprocess.run(["arm-none-eabi-addr2line", "-f", "-C", "-e", ELF, hex(pc)], capture_output=True, text=True)
    return " @ ".join(r.stdout.split())


def crash(cmd, label):
    out, inp = ports()
    out.send(mido.Message("sysex", data=H.HDR + [0x7E, cmd]))
    out.close(); inp.close()
    time.sleep(0.3)


print("start: the Fire answers, and clear any old record")
check("the Fire answers", wait_alive(10) is not None)
ask(clear=True)
c = ask()
check("the record is empty after clearing", c is not None and not c["valid"], c)

print("\nA. a bus fault (read from 0xFFFFFFF0)")
crash(0, "bus fault")
t = wait_alive(15)
check("it restarted by itself and answers again", t is not None, "no answer: did it fall to DFU? " + usb())
print(f"     back after {t:.1f} s" if t else "")
c = ask()
print("    ", H.describe_crash(c) if c else "no reply")
check("recorded as a HardFault (a bus fault escalates)", c and c["valid"] and c["cause"] == 1, c)
check("...in the USB+MIDI stage (that is where the command runs)", c and c["stage"] == 1, c)
check("...with the faulting address 0xFFFFFFF0 and a precise-bus-error flag", c and c["addr"] == 0xFFFFFFF0 and c["cfsr"] & (1 << 9), c)
check("...count 1, total 1", c and c["count"] == 1 and c["total"] == 1, c)
if c:
    print("     PC", hex(c["pc"]), addr2line(c["pc"]))
    check("...and the PC is in flash, in the app", 0x08008000 <= c["pc"] < 0x08020000, hex(c["pc"]))

print("\n   (waiting 35 s so the next crash is not 'early')")
time.sleep(35)

print("\nB. a hang (an endless loop: only the watchdog can end it)")
crash(1, "hang")
t = wait_alive(20)
check("the watchdog reset it, it restarted and answers again", t is not None, "no answer: " + usb())
print(f"     back after {t:.1f} s" if t else "")
c = ask()
print("    ", H.describe_crash(c) if c else "no reply")
check("recorded as a watchdog reset", c and c["valid"] and c["cause"] == 5, c)
check("...in the USB+MIDI stage (the loop was stuck there)", c and c["stage"] == 1, c)
check("...with an uptime of at least 30 s (the run before it was healthy), so the streak restarted: count 1, total 2",
      c and c["up_ms"] >= 30000 and c["count"] == 1 and c["total"] == 2, c)

print("\nC. an undefined instruction, straight away (an EARLY crash: count 2)")
crash(2, "undefined instruction")
t = wait_alive(15)
check("it restarted by itself", t is not None, usb())
c = ask()
print("    ", H.describe_crash(c) if c else "no reply")
check("recorded with the undefined-instruction flag, count 2, total 3",
      c and c["valid"] and c["cfsr"] & (1 << 16) and c["count"] == 2 and c["total"] == 3, c)

print("\nD. a third early crash in a row: the crash-loop guard sends it to DFU (update mode)")
crash(0, "bus fault again")
time.sleep(3)
u = usb()
check("it is now in DFU (STM32 bootloader), not restarting forever", "0483:df11" in u, u)

print(f"\ncrash_bench: {passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
