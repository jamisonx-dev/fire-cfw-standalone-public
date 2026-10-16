#!/usr/bin/env python3
"""Scroll a message across the Fire's pads (host protocol command 05).

    python3 host_text.py "HELLO WORLD"
    python3 host_text.py --stop          # end the scrolling (the pads come back)

Up to 32 printable ASCII characters; lower case shows as capitals. Works from any machine that can see the
Fire's MIDI port (it is found by name: "ORC FW"). Needs: pip install mido python-rtmidi
"""
import sys
import time

import mido

HDR = [0x7D, 0x4F, 0x52, 0x43]          # F0 7D 'O' 'R' 'C' ... F7 (mido adds the F0 / F7)
MAX = 32


def port_names(names):
    return [n for n in names if "ORC FW" in n]


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    outs = port_names(mido.get_output_names())
    if not outs:
        print("no Fire found (its MIDI port is called 'ORC FW ...')")
        return 1
    out = mido.open_output(outs[0])
    if argv[1] == "--stop":
        out.send(mido.Message("sysex", data=HDR + [0x02, 0]))      # (ends host lights; a hand-started show: touch the Fire)
        print("sent stop")
        return 0
    text = " ".join(argv[1:])
    data = [ord(c) if 32 <= ord(c) < 127 else 32 for c in text][:MAX]
    out.send(mido.Message("sysex", data=HDR + [0x05] + data))
    time.sleep(0.05)
    print(f"sent: {''.join(chr(d) for d in data)!r}" + (" (cut to 32)" if len(text) > MAX else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
