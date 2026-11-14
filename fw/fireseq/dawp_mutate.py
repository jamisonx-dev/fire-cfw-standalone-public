"""Mutation check for DAW mode's protocol translator (daw.c, Mackie Control + HUI): break each rule in a temp copy;
daw_test must turn red for every one. (daw_mutate.py is the older one, for the Pi-driven DAW page in ctl.c.)
    python3 -B dawp_mutate.py .        (on the Pi: needs gcc)"""
import os, shutil, subprocess, sys, tempfile
src = sys.argv[1] if len(sys.argv) > 1 else "."
d = tempfile.mkdtemp(dir=os.path.expanduser("~"))
for f in ("daw.c", "daw.h", "daw_test.c"):
    shutil.copy(os.path.join(src, f), d)
M = [  # (old, new): one rule each
    # --- Auto: lock to the first host, stay silent before ---
    ("        if (!want) return 0;\n", "        if (!want) return mcu_msg(S.status, a, v, ev);\n"),  # decodes what neither protocol owns
    ("    if (S.proto == DAW_AUTO || strip < 0 || strip >= DAW_STRIPS) return 0;",
     "    if (strip < 0 || strip >= DAW_STRIPS) return 0;"),                                 # sends before a host spoke
    ("    if (S.mode != DAW_AUTO) return 0;               /* a forced protocol never changes */\n", ""),   # re-locks / forced ignored
    ("b == 0x14 || b == 0x10 ? DAW_MCU", "b == 0x14 ? DAW_MCU"),                              # Logic Control id 10
    (": b == 0x05 ? DAW_HUI : 0;", ": 0;"),                                                   # HUI SysEx locks
    ("        if (b != M[p - 1]) S.sx = 2;", "        (void)M;"),                                # 00 00 66 not checked
    ("    S.proto = S.mode;\n", "\n"),                                                           # forced mode not applied
    # --- HUI ping ---
    ("    out3(0x90, 0x00, 0x7F);", "    out3(0x90, 0x00, 0x00);"),                             # wrong answer
    (" && S.proto != DAW_MCU) {      /* HUI ping (or", ") {      /* HUI ping (or"),
    ("    S.pings = 0;                                    /* v0.38b", "    /* v0.38b"),
    ("        if (S.proto == DAW_AUTO && ++S.pings < 2) return 0;", "        if (S.proto == DAW_AUTO && ++S.pings < 1) return 0;"),
    ("    if (hi == 0xE0 || hi == 0x90) return DAW_MCU;", "    if (hi == 0x90) return DAW_MCU;"),
    ("    if (a == 0x0C || a == 0x2C ||", "    if (a == 0x2C ||"),
    ("        memset(S.led_on, 0, sizeof S.led_on); memset(S.led_blink, 0, sizeof S.led_blink);\n", ""),               # MCU's REC 1 off = a ping
    ("S.status == 0x90 && a == 0 && v == 0 &&", "S.status == 0x90 && a == 0 &&"),             # 90 00 7F taken as a ping
    # --- MCU handshake ---
    ("        default: return 0;\n        }", "        default: break;\n        }"),             # unknown commands answered
    ("case 0x00: mcu_reply(0x01,", "case 0x00: mcu_reply(0x03,"),
    ("case 0x02: mcu_reply(0x03, DAW_SERIAL, 7, 0, 0)", "case 0x02: mcu_reply(0x03, DAW_SERIAL, 7, CHALLENGE, 4)"),
    ("case 0x13: mcu_reply(0x14,", "case 0x13: mcu_reply(0x13,"),
    ("    m[4] = S.dev ? S.dev : 0x14;", "    m[4] = 0x14;"),                                    # Logic Control's id not echoed
    ("static const uint8_t CHALLENGE[4] = {0x01, 0x02, 0x03, 0x04};", "static const uint8_t CHALLENGE[4] = {0x01, 0x02, 0x03, 0x05};"),
    ("    if (S.proto != DAW_MCU) return 0;\n    mcu_reply(0x01", "    mcu_reply(0x01"),          # HUI announces
    # --- the byte parser ---
    ("        S.sx = 0;\n        S.status = b < 0xF0", "        S.status = b < 0xF0"),            # status inside SysEx not aborting
    ("    if (b >= 0xF8) return 0;", "    if (b >= 0xFF) return 0;"),                           # clock breaks messages
    ("    S.nd = 0;                                       /* complete",
     "    S.nd = 0; S.status = 0;                         /* complete"),                        # no running status
    # --- MCU in ---
    ("level_ev(ch, v << 7 | a, ev)", "level_ev(ch, a << 7 | v, ev)"),                          # pitch bend bytes swapped
    ("ch < DAW_STRIPS ? level_ev", "ch <= DAW_STRIPS ? level_ev"),                            # channel 10 = a strip
    ("kind == 0x80 || v == 0 ? 0", "v == 0 ? 0"),                                             # note-off = lit
    ("v == 1 ? 1 : 2, ev);", "v == 1 ? 2 : 2, ev);"),                                         # no blink
    ("        if (ch) return 0;\n        int id = mcu_note_id(a);", "        int id = mcu_note_id(a);"),   # any channel
    ("    if (n < 40) return n;", "    if (n < 32) return n;"),                                  # V-pot push lights lost
    ("    91, 92, 93, 94, 95,", "    91, 92, 94, 93, 95,"),                                     # stop / play swapped
    ("    54, 55, 56, 57, 58, 59, 60, 61,", "    54, 55, 56, 57, 58, 59, 60, 62,"),             # F8 wrong
    ("    40, 41, 42, 43, 44,", "    40, 42, 41, 43, 44,"),                                     # assign send / pan swapped
    ("if (S.tpos < DAW_MCU_LCD) text_put", "if (S.tpos < DAW_TEXT) text_put"),                 # LCD past 112
    ("c >= 0x20 && c <= 0x7E ? c : ' '", "c"),                                                # control codes kept
    ("            if (p == 6) { S.tpos = b; return; }", "            if (p == 6) { S.tpos = 0; return; }"),   # offset ignored
    ("a >= 64 && a <= 75", "a >= 64 && a <= 76"),                                             # digit 13
    ("(c < 0x20 ? c + 0x40 : c)", "c"),                                                       # 7-seg letters
    ("(ch == 0 || ch == 15) && a >= 64", "ch == 0 && a >= 64"),                               # channel 16 digits
    ("(v & 0x40 ? 0x80 : 0)", "0"),                                                           # digit dot
    ("a >= 48 && a <= 55", "a >= 48 && a <= 56"),                                             # ring 9
    ("            S.ring[a - 48] = v;\n", "\n"),                                                 # ring not kept
    # --- HUI in ---
    ("        if (S.hi_strip != a - 0x20) return 0;", ""),                                     # lo with another strip's hi
    ("S.hi_val << 7 | v, ev)", "v << 7 | S.hi_val, ev)"),                                     # hi / lo swapped
    ("    if (a <= 0x07) {", "    if (a <= 0x08) {"),                                         # fader 9 in HUI
    ("a >= 0x10 && a <= 0x17", "a >= 0x10 && a <= 0x18"),                                     # ring 9
    ("set_led(id, v & 0x40 ? 2 : 0, ev)", "set_led(id, v ? 2 : 0, ev)"),                      # off = on
    ("        if (S.zone > 0x1D) return 0;\n", "\n"),                                            # bad zone wraps
    ("const uint8_t DAW_HUI_STRIP_PORT[7] = {7, 3, 2, 1, 5, 4, 6};", "const uint8_t DAW_HUI_STRIP_PORT[7] = {7, 2, 3, 1, 5, 4, 6};"),
    ("    ZP(0x0A, 1), ZP(0x0A, 3), ZP(0x0A, 0), ZP(0x0A, 2),", "    ZP(0x0A, 0), ZP(0x0A, 2), ZP(0x0A, 1), ZP(0x0A, 3),"),
    ("ZP(0x0E, 3), ZP(0x0E, 4), ZP(0x0E, 5),", "ZP(0x0E, 4), ZP(0x0E, 3), ZP(0x0E, 5),"),       # stop / play
    ("    ZP(0x18, 2), ZP(0x18, 4),", "    ZP(0x18, 4), ZP(0x18, 2),"),                           # read / write
    ("    ZP(0x16, 0), ZP(0x16, 2), ZP(0x16, 3),", "    ZP(0x16, 0), ZP(0x16, 1), ZP(0x16, 3),"),
    ("    NO, ZP(0x0B, 7), ZP(0x0B, 2), NO, NO,", "    NO, ZP(0x0B, 2), ZP(0x0B, 7), NO, NO,"),
    ("    if (p == 5) { if (b != 0x00) S.sx = 2; return; }", "    if (p == 5) { return; }"),   # 00 after HUI id
    ("if (p == 7) { if (b > 8) S.sx = 2; else S.tpos = (uint8_t)(DAW_HUI_STRIP + b * 4); return; }",
     "if (p == 7) { S.tpos = (uint8_t)(DAW_HUI_STRIP + b * 4); return; }"),                   # strip 127 wraps
    ("        if (p <= 11) text_put", "        if (p <= 12) text_put"),                       # 5-char strips
    ("            if (b > 7) { S.sx = 2; return; }\n", "\n"),                                    # main zone 8
    ("(b / 4) * 40 + (b % 4) * 10", "(b % 4) * 40 + (b / 4) * 10"),                           # row / column swapped
    ("        int k = (p - 7) % 11;", "        int k = (p - 7) % 10;"),                       # block length
    ("(b & 0x10 ? 0x80 : 0)", "0"),                                                           # timecode dot
    ("((b & 0x0F) < 10 ? '0' + (b & 0x0F) : ' ')", "('0' + (b & 0x0F))"),                     # digit 10+ shown
    # --- out: buttons ---
    ("        out3(0x90, (uint8_t)n, down ? 0x7F : 0x00);", "        out3(0x90, (uint8_t)n, 0x7F);"),   # no release
    ("(id < DB_AUTO ? id : NO)", "id"),                                                       # HUI-only sent in MCU
    ("        if (n == NO || (id >= DB_LED_SMPTE && id <= DB_LED_RUDE)) return 0;", "        if (n == NO) return 0;"),
    ("        if (zp == NO || (id >= DB_LED_SMPTE && id <= DB_LED_RUDE)) return 0;", "        if (zp == NO) return 0;"),
    ("    out3(0xB0, 0x0F, zone);", "    out3(0xB0, 0x0C, zone);"),                             # LED zone CC used
    ("    out3(0xB0, 0x2F, (uint8_t)((on ? 0x40 : 0x00) | port));", "    out3(0xB0, 0x2C, (uint8_t)((on ? 0x40 : 0x00) | port));"),
    ("(uint8_t)((on ? 0x40 : 0x00) | port)", "(uint8_t)(0x40 | port)"),                      # HUI release = press
    ("ZP(id & 7, DAW_HUI_STRIP_PORT[id >> 3])", "ZP(id >> 3, DAW_HUI_STRIP_PORT[id & 7])"),   # zone / group swapped
    # --- out: faders + touch ---
    ("    if (!(S.touched >> strip & 1)) touch(strip, 1);", ""),                               # no touch
    ("    if (!(S.touched >> strip & 1)) touch(strip, 1);", "    touch(strip, 1);"),             # touch every time
    ("    if (S.proto == DAW_MCU) out3(0x90, (uint8_t)(104 + s)", "    if (S.proto == DAW_MCU) out3(0x90, (uint8_t)(105 + s)"),
    ("#define HUI_TOUCH_PORT 0", "#define HUI_TOUCH_PORT 1"),
    (">= DAW_TOUCH_MS) touch(s, 0);", "> DAW_TOUCH_MS) touch(s, 0);"),                        # 1 ms late
    ("(uint16_t)((uint16_t)now_ms - S.touch_ms[s])", "(now_ms - S.touch_ms[s])"),             # wrap
    ("    S.touch_ms[strip] = (uint16_t)now_ms;\n", "\n"),                                       # no refresh
    ("    else S.touched &= (uint16_t)~(1u << s);", "    else (void)0;"),                       # release repeats
    ("    fine = fine < 0 ? 0 : fine > 16383 ? 16383 : fine;\n", "\n"),                          # no clamp
    ("    if (S.proto == DAW_HUI && strip >= 8) return 0;", ""),                                 # HUI master
    ("(uint8_t)(0xE0 | strip), (uint8_t)(fine & 0x7F), (uint8_t)(fine >> 7));",
     "(uint8_t)(0xE0 | strip), (uint8_t)(fine >> 7), (uint8_t)(fine & 0x7F));"),             # MCU bytes swapped
    ("    else { out3(0xB0, (uint8_t)strip, (uint8_t)(fine >> 7)); out3(0xB0, (uint8_t)(0x20 + strip), (uint8_t)(fine & 0x7F)); }",
     "    else { out3(0xB0, (uint8_t)(0x20 + strip), (uint8_t)(fine & 0x7F)); out3(0xB0, (uint8_t)strip, (uint8_t)(fine >> 7)); }"),
    # --- out: V-pots, jog, banks ---
    ("    if (S.proto == DAW_HUI) cw = !cw;\n", "\n"),                                           # HUI direction
    ("    if (n > 63) n = 63;\n", "\n"),                                                         # no clamp
    ("(S.proto == DAW_MCU ? 16 : 0x40) + pot", "(S.proto == DAW_MCU ? 16 : 0x10) + pot"),
    ("(S.proto == DAW_MCU ? 16 : 0x40) + pot", "(S.proto == DAW_MCU ? 48 : 0x40) + pot"),
    ("pot > 7 || !detents) return 0;", "pot > 7) return 0;"),
    ("S.proto == DAW_MCU ? 60 : 0x0D", "60"),
    ("    daw_release_all();                              /* the strips", "    /* the strips"),
    ("dir < 0 ? DB_BANK_L : DB_BANK_R", "dir < 0 ? DB_BANK_R : DB_BANK_L"),
    ("dir < 0 ? DB_CH_L : DB_CH_R", "dir < 0 ? DB_BANK_L : DB_BANK_R"),
    # --- init ---
    ("    memset(S.text, ' ', sizeof S.text);\n", "\n"),
    ("    S.zone = S.hi_strip = 0xFF;", "    S.hi_strip = 0xFF;"),                               # stale zone 0
]
red = 0
p = os.path.join(d, "daw.c")
orig = open(p, encoding="utf-8", newline="").read()
base = orig.replace("\r\n", "\n")
exe = os.path.join(d, "dt")
for a, b in M:
    if a not in base:
        print("NOT APPLIED:", a.strip()[:70]); continue
    open(p, "w", encoding="utf-8", newline="").write(base.replace(a, b, 1))
    c = subprocess.run(["gcc", "-O1", "-w", "-I" + d, "-o", exe, os.path.join(d, "daw_test.c"), p], capture_output=True, text=True)
    r = subprocess.run([exe], capture_output=True, text=True, timeout=20) if c.returncode == 0 else None
    ok = r is None or r.returncode != 0
    red += ok
    print("red  " if ok else "GREEN", ("(no build) " if r is None else "") + a.strip()[:70].replace("\n", " "))
open(p, "w", encoding="utf-8", newline="").write(orig)
print(f"{red}/{len(M)} mutations red")
shutil.rmtree(d)
sys.exit(0 if red == len(M) else 1)
