#!/usr/bin/env python3
"""Akai Fire custom firmware: crash report for bug reports.

Asks the Fire for the last crash it recorded and prints plain text you can paste into a GitHub issue.

    Windows:        py -m pip install mido python-rtmidi
                    py fire_crash_report.py
    macOS / Linux:  python3 -m pip install mido python-rtmidi
                    python3 fire_crash_report.py
    add --hello to the last command           (also asks the Fire for its protocol version, which tells the firmware
                                            generation: 1 = v0.29 and older, 9 = v0.34)

What it does: finds the Fire's MIDI port (the first FLAME port, "FLAME seq" / "FLAME"; "ORC FW seq" on older builds), sends two READ-ONLY questions
(host command 06 = last crash record, 08 = stack snapshot) and prints the answers.
What it never does: clear the crash record (command "06 01"), write anything, or change settings.
Close other MIDI programs first (a program that holds the Fire's port can block it).

It cannot turn addresses into source lines (that needs the matching ELF file); it prints the raw numbers plus the
build id so the developer can do that later with host_crash.py.

The decoding below is copied from host_vu.py / host_crash.py of the ORC-1 project (the owner's Pi tools), trimmed
to what a report needs. No ORC code is imported.
"""
import re
import sys
import time

try:
    import mido
except ImportError:
    print("This needs the 'mido' and 'python-rtmidi' packages.")
    print("Windows:  py -m pip install mido python-rtmidi")
    print("macOS / Linux:  python3 -m pip install mido python-rtmidi")
    sys.exit(2)

HDR = [0x7D, 0x4F, 0x52, 0x43]                      # F0 7D 'O' 'R' 'C' ... F7

# ---- copied from host_vu.py (ORC-1): the Fire's crash record format (crash.h in the firmware) ----
CRASH_CAUSES = {0: "none", 1: "HardFault", 2: "MemManage", 3: "BusFault", 4: "UsageFault", 5: "watchdog (a hang)",
                6: "stack smash (a buffer overran into a stack canary)",
                7: "watchpoint (a write hit TIM4's watched canary)",
                8: "TIM4 stack slip (diagnostic: TIM4's stack check failed before it returned)"}
CRASH_STAGES = {1: "USB + MIDI + host protocol", 2: "sequencer", 3: "note-off / ratchet queues", 4: "input events",
                5: "idle lights + save gates", 6: "flash saver", 7: "lights", 8: "knobs", 9: "paint LEDs",
                10: "render + OLED", 11: "loop tail", 12: "start-up"}
CRASH_REPLY_LENS = {1: 46, 2: 61, 3: 91}            # reply version -> SysEx body length
T4_VERDICTS = {0: "ok", 1: "MISREAD (memory agreed on a second look; not a crash)", 2: "STALE yardstick (not a crash)",
               3: "CHANGED (the slot itself held a wrong return: a crash)", 4: "ENTRY MISREAD (counted, not a crash)"}
STACK_WORDS = 64
STACK_REPLY_LEN = 4 + 1 + 1 + 1 + 5 + STACK_WORDS * 5
APP_LO, APP_HI = 0x08008000, 0x08020000             # the app's flash: an odd word in here looks like a return address


def unpack32(b):
    """Five 7-bit bytes, low bits first -> a 32-bit value."""
    v = 0
    for x in reversed(list(b)[:5]):
        v = (v << 7) | (x & 0x7F)
    return v & 0xFFFFFFFF


def decode_crash(data):
    """Reply to host command 06 (SysEx bytes without F0/F7) -> dict, or None if it is not one."""
    d = list(data)
    if len(d) < 6 or d[:5] != HDR + [0x06] or CRASH_REPLY_LENS.get(d[5]) != len(d):
        return None
    nf = {1: 7, 2: 10, 3: 16}[d[5]]
    f = [unpack32(d[11 + 5 * i:16 + 5 * i]) for i in range(nf)]
    t4 = dict(zip(("t4_count", "t4_ms", "t4_kind", "t4_seen", "t4_want", "t4_exc"), f[10:16])) if nf == 16 else {}
    return dict(t4, reply_version=d[5], valid=bool(d[6]), cause=d[7],
                cause_name=CRASH_CAUSES.get(d[7], f"cause {d[7]} (unknown to this script: it is older than the firmware)"),
                stage=d[8], stage_name=CRASH_STAGES.get(d[8], "?"), count=d[9], total=d[10],
                pc=f[0], lr=f[1], psr=f[2], cfsr=f[3], hfsr=f[4], addr=f[5], up_ms=f[6],
                build=f[7] if nf >= 10 else None, sp=f[8] if nf >= 10 else None, stack_min=f[9] if nf >= 10 else None)


def decode_stack(data):
    """Reply to host command 08 -> dict(valid, base, words), or None."""
    d = list(data)
    if len(d) != STACK_REPLY_LEN or d[:5] != HDR + [0x08] or d[5] != 1:
        return None
    return dict(valid=bool(d[6]), base=unpack32(d[7:12]),
                words=[unpack32(d[12 + 5 * i:17 + 5 * i]) for i in range(STACK_WORDS)])


def looks_like_code(w):
    return APP_LO <= w < APP_HI and w & 1 == 1


def is_fire_seq_port(name):
    """The Fire's FIRST port (the sequencer, which answers the host protocol), as each OS names it.
    FLAME (v0.36+) has two ports: Linux "FLAME:FLAME seq 20:0" / "FLAME:FLAME DAW 20:1"; Windows 10 names them
    after the device, "FLAME 0" and "MIDIOUT2 (FLAME) 1" (the DAW port); macOS may show "FLAME seq" / "FLAME DAW".
    Older builds (up to v0.35b): one port, "ORC FW seq". Never the DAW port, never other makers' "Flame ..." gear."""
    if "ORC FW" in name:
        return True
    if "FLAME DAW" in name or "MIDIOUT2" in name or "MIDIIN2" in name:
        return False
    if "FLAME seq" in name:
        return True
    return re.fullmatch(r"(\d+- )?FLAME( \d+)?", name) is not None     # Windows: "FLAME 0", "2- FLAME 3"


def find_fire_ports():
    outs = [n for n in mido.get_output_names() if is_fire_seq_port(n)]
    ins = [n for n in mido.get_input_names() if is_fire_seq_port(n)]
    return outs, ins


def build_report(c, st):
    L = []
    say = L.append
    say(f"Fire crash report (read {time.strftime('%Y-%m-%d %H:%M:%S')}, fire_crash_report.py)")
    b = c.get("build")
    if b is None:
        say("Firmware build id: not reported (an older firmware)")
    elif b == 0:
        say("Firmware build id: 00000000 (the record carries no id)")
    else:
        say(f"Firmware build id: {b:08x}")
    say(f"Record format version: {c['reply_version']}")
    if not c["valid"]:
        say("Crash on record: NO (the Fire has not crashed since its record was last cleared)")
        return L
    say("Crash on record: YES")
    say(f"  cause:  {c['cause_name']}")
    say(f"  stage:  {c['stage']} ({c['stage_name']})   (what the main loop was doing)")
    say(f"  uptime at the crash: {c['up_ms']} ms ({c['up_ms'] / 1000:.1f} s since the Fire booted)")
    say(f"  crashes in a row: {c['count']}, total: {c['total']}")
    say(f"  PC 0x{c['pc']:08x}  LR 0x{c['lr']:08x}  xPSR 0x{c['psr']:08x}")
    say(f"  CFSR 0x{c['cfsr']:08x}  HFSR 0x{c['hfsr']:08x}  fault address 0x{c['addr']:08x}")
    if c.get("sp"):
        say(f"  SP at the crash 0x{c['sp']:08x}; stack low point 0x{c['stack_min']:08x}")
    if c.get("t4_count"):
        say(f"  TIM4 check: {c['t4_count']} note(s); last at {c['t4_ms'] / 1000:.1f} s up: "
            f"{T4_VERDICTS.get(c['t4_kind'], 'kind ' + str(c['t4_kind']))}; "
            f"seen 0x{c['t4_seen']:08x} want 0x{c['t4_want']:08x} handed 0x{c['t4_exc']:08x}")
    if st is None:
        say("Stack snapshot: no answer (firmware older than v0.17c)")
    elif not st["valid"]:
        say("Stack snapshot: none for this record (a hang has no fault frame)")
    else:
        say(f"Stack snapshot from 0x{st['base']:08x} (64 words; '<- code' = might be a return address):")
        for i, w in enumerate(st["words"]):
            say(f"  +{4 * i:3d}  0x{w:08x}" + ("  <- code" if looks_like_code(w) else ""))
    return L


def main():
    outs, ins = find_fire_ports()
    if not outs or not ins:
        print("No Fire found. Its MIDI port is called 'FLAME seq' or 'FLAME' ('ORC FW seq' on older builds).")
        print("Check: USB plugged in; the custom firmware is installed (not the original Akai one); the Fire is not")
        print("in update mode (dark screen); no other program (DAW, MIDI monitor) is holding its port.")
        print("MIDI ports seen: " + (", ".join(mido.get_output_names()) or "none"))
        return 1
    out, inp = mido.open_output(outs[0]), mido.open_input(ins[0])
    time.sleep(0.2)
    list(inp.iter_pending())
    # Read only. Never "06 01": that clears the record, and the clear also drops the stack snapshot.
    out.send(mido.Message("sysex", data=HDR + [0x06]))
    out.send(mido.Message("sysex", data=HDR + [0x08]))
    hello = "--hello" in sys.argv
    if hello:                                       # optional: 00 = hello, the Fire answers with its protocol version
        out.send(mido.Message("sysex", data=HDR + [0x00]))
    t0, c, st, ver = time.time(), None, None, None
    while time.time() - t0 < 2.0 and (c is None or st is None or (hello and ver is None)):
        for m in inp.iter_pending():
            if m.type == "sysex":
                d = list(m.data)
                if len(d) == 6 and d[:5] == HDR + [0x00]:
                    ver = d[5]
                c = decode_crash(m.data) or c
                st = decode_stack(m.data) or st
        time.sleep(0.01)
    out.close()
    inp.close()
    if c is None:
        print("The Fire did not answer command 06 (firmware older than v0.17, or the Fire is busy). Try again.")
        return 1
    lines = build_report(c, st)
    if hello:
        lines.insert(1, "Host protocol version (hello): " + (str(ver) if ver is not None else "no answer"))
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
