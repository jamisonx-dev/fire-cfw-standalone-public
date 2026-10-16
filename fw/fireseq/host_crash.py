#!/usr/bin/env python3
"""Ask the Fire what its last crash was (host protocol command 06).

    python3 host_crash.py                 # print the last crash on record
    python3 host_crash.py --clear         # print it, then clear it (06 01)
    python3 host_crash.py --elf fireseq.elf   # also turn the PC / LR into source lines (needs arm-none-eabi-addr2line)
    python3 host_crash.py --elf auto      # find the ELF of the build that crashed (the release loop keeps them)
    python3 host_crash.py --save          # also write ~/fire_fw/crashes/crash_<time>_<build>.txt (implies --elf auto);
                                          # orc1-firevu runs this by itself whenever the Fire reports a new crash

Since the firmware records faults and watchdog resets (crash.h) and restarts itself, a crash no longer leaves the Fire
dark: it restarts in about a second and keeps the cause here. Three early crashes in a row still drop it to DFU.
"""
import glob
import os
import shutil
import subprocess
import sys
import time

import mido

import host_vu as H


def elf_build(path):
    """The build id an ELF / bin carries (the firmware's "ORCBUILD=xxxxxxxx" tag), or None."""
    try:
        data = open(path, "rb").read()
    except OSError:
        return None
    i = data.find(b"ORCBUILD=")
    if i < 0:
        return None
    tag = data[i + 9:i + 17].decode("ascii", "replace")
    return tag if all(ch in "0123456789abcdef" for ch in tag) else None


ELF_DIRS = [os.path.expanduser("~/fire_fw"), os.path.expanduser("~/fire_fw/fw/fireseq")]
CRASH_DIR = os.path.expanduser("~/fire_fw/crashes")


def find_elf(build):
    """The newest kept ELF whose ORCBUILD tag is `build` (the release loop keeps every flashed image's ELF), or None."""
    if not build:
        return None
    hits = []
    for d in ELF_DIRS:
        for f in glob.glob(os.path.join(d, "*.elf")):
            t = elf_build(f)
            if t and int(t, 16) == build:
                hits.append(f)
    return max(hits, key=os.path.getmtime) if hits else None


def main(argv):
    clear, save = "--clear" in argv, "--save" in argv
    elf = argv[argv.index("--elf") + 1] if "--elf" in argv and argv.index("--elf") + 1 < len(argv) else None
    if save and elf is None:
        elf = "auto"
    lines = []
    say = lines.append
    outs = [n for n in mido.get_output_names() if "ORC FW" in n]
    ins = [n for n in mido.get_input_names() if "ORC FW" in n]
    if not outs or not ins:
        print("no Fire found (its MIDI port is called 'ORC FW ...'; it may be in DFU / update mode)")
        return 1
    out, inp = mido.open_output(outs[0]), mido.open_input(ins[0])
    time.sleep(0.2)
    list(inp.iter_pending())
    out.send(mido.Message("sysex", data=H.HDR + [0x06]))          # read first; clear (06 01) only at the very end,
    out.send(mido.Message("sysex", data=H.HDR + [0x08]))          # because clearing also drops the stack snapshot
    t0, c, st = time.time(), None, None
    while time.time() - t0 < 2.0 and (c is None or st is None):
        for m in inp.iter_pending():
            if m.type == "sysex":
                c = H.decode_crash(m.data) or c
                st = H.decode_stack(m.data) or st
        time.sleep(0.01)
    if c is None:
        print("no answer to command 06 (an older firmware, or the Fire is busy)")
        return 1
    say(H.describe_crash(c))
    if c["valid"]:
        say(f"  read {time.strftime('%Y-%m-%d %H:%M:%S')}; cause {c['cause_name']}  stage {c['stage']} ({c['stage_name']})  uptime {c['up_ms']} ms")
        if c.get("build"):
            say(f"  build {c['build']:08x}")
        if c.get("sp"):
            say(f"  SP at the fault 0x{c['sp']:08x} ({0x20010000 - c['sp']} bytes deep); the stack's lowest point since boot "
                f"0x{c['stack_min']:08x} ({0x20010000 - c['stack_min']} bytes deep; RAM ends at 0x20010000)")
        say(f"  PC 0x{c['pc']:08x}  LR 0x{c['lr']:08x}  xPSR 0x{c['psr']:08x}  CFSR 0x{c['cfsr']:08x}  HFSR 0x{c['hfsr']:08x}  addr 0x{c['addr']:08x}")
        if elf == "auto":
            elf = find_elf(c.get("build"))
            say(f"  ELF: {elf}" if elf else f"  ELF: none kept for build {c.get('build') or 0:08x} in {', '.join(ELF_DIRS)}: not symbolised")
        if elf:
            tag = elf_build(elf)
            rec = c.get("build")
            if rec is None or rec == 0:
                say(f"  build: the record does not say which image crashed (older firmware); the ELF is {tag or 'untagged'}:"
                    " only trust the lines below if that image was the one running")
            elif tag is None:
                say(f"  build: the crash was in build {rec:08x}, but the ELF has no ORCBUILD tag: lines below may be WRONG")
            elif int(tag, 16) != rec:
                say(f"  !! build MISMATCH: the crash was in build {rec:08x}, the ELF is build {tag}. Not symbolising"
                    " (the lines would be wrong). Try --elf auto.")
                elf = None
            else:
                say(f"  build {tag}: the ELF matches the image that crashed")
        sym = None
        if elf and shutil.which("arm-none-eabi-addr2line"):
            def sym(a):
                r = subprocess.run(["arm-none-eabi-addr2line", "-f", "-C", "-e", elf, hex(a)], capture_output=True, text=True)
                return " @ ".join(r.stdout.split())
            for name in ("pc", "lr"):
                say(f"  {name.upper()} -> " + sym(c[name]))
        frame = c["cause"] in (1, 2, 3, 4, 7)          # a fault / the watchpoint: the snapshot starts at an exception frame
        if st is None:
            say("  stack: no answer to command 08 (firmware before v0.17c)")
        elif not st["valid"]:
            say("  stack: no snapshot for this record (a watchdog hang has no fault frame, or it was cleared)")
        else:
            say(f"  stack from 0x{st['base']:08x} " + ("(the exception frame first: R0-R3, R12, LR, PC, xPSR; then the interrupted code's frames)"
                if frame else "(the stack where the canary check failed; no exception frame)")
                + ". Words that look like return addresses into the app = the likely call chain:")
            for i, w in enumerate(st["words"]):
                tag = ""
                if H.looks_like_code(w):
                    tag = "  <- code" + (": " + sym(w) if sym else "")
                elif frame and i < 8:
                    tag = "  (" + ("R0", "R1", "R2", "R3", "R12", "LR", "PC", "xPSR")[i] + ")"
                say(f"    +{4 * i:3d}  0x{w:08x}{tag}")
    print("\n".join(lines))
    if save and c["valid"]:
        os.makedirs(CRASH_DIR, exist_ok=True)
        fn = os.path.join(CRASH_DIR, f"crash_{time.strftime('%Y%m%d_%H%M%S')}_{c.get('build') or 0:08x}.txt")
        with open(fn, "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"(saved {fn})")
    if clear:
        out.send(mido.Message("sysex", data=H.HDR + [0x06, 0x01]))
        time.sleep(0.2)
        print("(cleared)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
