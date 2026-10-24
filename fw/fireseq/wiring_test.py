"""main.c is bare-metal and cannot run on the host, so ctl_test.c proves the RULES and this proves main.c USES them
(each check names the review finding it guards). fire_live_test.py then proves the behaviour on the real Fire.
    python3 wiring_test.py        (any OS; reads main.c / lights.c next to it)"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, "main.c"), encoding="utf-8").read().replace("\r\n", "\n")
LIGHTS = open(os.path.join(HERE, "lights.c"), encoding="utf-8").read().replace("\r\n", "\n")
EXPECTED = 94
p = f = 0


def check(cond, label):
    global p, f
    if cond:
        p += 1
    else:
        f += 1
        print("  FAIL:", label)


def body(name, src=SRC):
    """The text of a C function `name` (from its signature to the matching close brace), '' if missing."""
    m = re.search(r"^[a-z].*\b" + re.escape(name) + r"\([^;{]*\)\s*\{", src, re.M)
    if not m:
        return ""
    i, depth = m.end() - 1, 0
    for j in range(i, len(src)):
        depth += src[j] == "{"
        depth -= src[j] == "}"
        if depth == 0:
            return src[m.start():j + 1]
    return ""


def code(s):
    """Without comments (a check must not be satisfied by a comment)."""
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", s, flags=re.S)


lt = code(body("lights_task"))
check("ext_playing" not in lt, "4.3 lights_task no longer stops for an external clock")
check("ctl_frame_due(now, lt_last_us)" in lt, "4.3 the frame timer is ctl_frame_due (unsigned, wrap-safe)")
check("ctl_host_timeout(" in lt and "HT_FALLBACK" in lt and "HT_OFF" in lt, "4.4 the host-quiet rule is ctl_host_timeout, both outcomes handled")
check("ctl_cycle_due(G.lt_mode, lt_cur, lt_pinned" in lt, "4.5 CYCLE leaves a pinned host effect alone")

check("touched_lazy()" in code(body("touched_global")) and "touched_lazy()" in code(body("touched_pattern")),
      "4.1 touched_global / touched_pattern arm the lazy save")
check("ctl_lazy_arm(ms_ticks)" in code(body("touched_lazy")) and "!playing && !ext_playing" in code(body("touched_lazy")),
      "4.1 the lazy save is armed only while stopped")
check("commit_pending()" in code(body("stop")), "4.1 STOP commits what waits")
ov = code(body("overlay_event"))
check(re.search(r"IN_STOP && !playing && !ext_playing\) \{ lights_next\(1\); commit_pending\(\);", ov) is not None,
      "4.1 STOP while lights run (next show) also commits")
check("save_lazy_due" in code(body("unsaved")), "4.1 the star shows during the lazy window")
loop = code(body("main"))
check("ctl_lazy_due(save_lazy_due, ms_ticks)" in loop, "4.1 the main loop promotes a due lazy save")
check("save_forced_run = save_force" in code(body("saver_start")), "4.1 a save remembers if it was forced")
check("if (save_forced_run) save_force = true" in code(body("saver_poll")), "4.1 a failed forced save retries forced")
check("if (save_forced_run) save_force = true" in code(body("cancel_save_of")), "4.1 a cancelled forced save re-arms forced")
check(re.search(r"if \(converted\) \{[^}]*save_force = true", code(body("load_state"))) is not None,
      "4.1 the boot format migration saves even with AUTOSAVE off")

hc = code(body("host_cmd"))
check("ctl_host_parse(p, n)" in hc and "ctl_host_reengage(" in hc and "ctl_host_stop_ends(lt_on, lt_host, lt_by_host)" in hc,
      "4.4/4.9 host_cmd uses the tested parser, re-engage and stop rules")
check(re.search(r"case HC_TEXT:\s*if \(!h\.len\) break;", hc) is not None, "4.5 an empty text message is ignored")
check("lt_by_host = lt_pinned = true" in code(body("host_pin")), "4.5/4.9 a host effect / text is pinned + host-started")
check("ctl_sysex_payload(sx, sx_len, sizeof sx" in code(body("midi_in_byte")), "the SysEx collector uses ctl_sysex_payload")

check("ctl_idle_should_start(&in)" in loop and re.search(r"if \(run\) lt_input_ms = ms_ticks;", loop) is not None,
      "4.6 idle start = ctl_idle_should_start, and playing (incl. external) counts as activity")
check("lt_input_ms = ms_ticks" in code(body("lights_off")), "idle time counts from when the pads went back (host off)")
check("lights_clear_bars()" in code(body("lights_on")), "4.8 the bars start empty")
check("lights_clear_bars()" in code(body("lights_set_bar_style", LIGHTS)), "4.8 a new bar look starts empty")
check("RTC->BKP14R = BUILD_ID" in code(body("crash_record")) and "ci->build = RTC->BKP14R" in code(body("crash_load"))
      and 'BUILD_TAG[] = "ORCBUILD=" BUILD_ID_STR' in SRC, "a crash record names the build that crashed (and the image carries its tag)")

check(re.search(r"mpu_ram_xn\(\);", loop) is not None and "MPU_RASR_XN_Msk" in code(body("mpu_ram_xn"))
      and "MEMFAULTENA" in code(body("mpu_ram_xn")), "v0.17c: main enables the MPU with RAM execute-never + MemManage")
check("snap_take(f)" in code(body("crash_from_fault")) and "snap_take(" in code(body("__stack_chk_fail")),
      "v0.17c: a fault and a stack smash keep the stack snapshot")
check(all(re.search(r"static void __attribute__\(\(noinline\)\) " + n + r"\(", SRC) for n in
          ("render", "draw_lights_screen", "host_cmd", "midi_in_byte", "midi_task", "handle_events", "paint", "draw_menu")),
      "v0.17c: main's buffer-owning callees are noinline (so their canaries are checked)")

HW = open(os.path.join(HERE, "fire_hw.c"), encoding="utf-8").read().replace("\r\n", "\n")
tim4 = code(body("TIM4_IRQHandler", HW))
_inner = tim4[tim4.find("{") + 1:tim4.rfind("}")].strip() if "{" in tim4 else ""
check(_inner.startswith("uint8_t out[CHAIN], in[CHAIN];\n    canary_watch_arm(out, in, CHAIN);") and _inner.endswith("canary_watch_disarm();"),
      "TIM4 arms the canary watchpoint first and disarms it last")
_aw = code(body("canary_watch_arm", HW))
check("DWT->FUNCTION0 = 6" in _aw and "w[i] == g" in _aw and "i < 3" in _aw and "__get_MSP" not in _aw,
      "the watchpoint is a data-WRITE watch on the guard word just above the arrays (never a first match from SP)")
check(re.search(r"dbgmon_init\(\);", loop) is not None and "MON_EN" in code(body("dbgmon_init")) and "DebugMonitor_IRQn, 0" in code(body("dbgmon_init")),
      "main enables the debug monitor at priority 0 (it must preempt TIM4)")
check("CRASH_VECTOR(DebugMon_Handler, 7)" in SRC, "DebugMon records a crash (cause 7, watchpoint)")
check(code(body("set_mode")).find("surface_on = false") < code(body("set_mode")).find("if (m == mode) return;")
      and "surface_on = false" in code(body("set_mode")),
      "v0.19: every mode button leaves the surface (even the button of the mode you are in)")
_he = code(body("handle_events"))
_lr = code(body("live_release"))
check(0 < _he.find("if (pad >= 0 && surface_on)") < _he.find("if (pad >= 0 && picker_on())")
      and _he.find("else live_release(pad);") < _he.find("if (pad >= 0 && surface_on)") and "live[pad].on = false;" in _lr,
      "v0.19: surface pads are handled before any other pad meaning, and their release goes through live[]")
_sp = _he[_he.find("if (pad >= 0 && surface_on)"):_he.find("if (pad >= 0 && picker_on())")]
check("surf_send(pad, vel);" in _sp and "0x90" not in _sp and "live[pad].sx = true" in _sp
      and "0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x0C" in body("surf_send"),
      "v0.21: a surface pad press is the 0C SysEx, never a note on a channel (no channel taken from anyone)")
check(0 < _lr.find("if (live[pad].sx)") < _lr.find("surf_send(pad, 0);") < _lr.find("notes_held[")
      and "live[pad].sx = false; live[pad].cc = false; live[pad].on = true;" in _he,
      "v0.21: a surface pad's release is a 0C with velocity 0 (not a note-off); a normal press clears sx")
check("|| surface_on," in loop, "v0.19: idle lights never start over the surface")
_ms = code(body("menu_surface_now"))
check('{"ORC mode", 0, MK_ACT,    0, 0, 0, menu_surface_now}' in SRC and "if (!surf_known)" in _ms
      and "surface_on = true" in _ms and "surf_known = true" in code(body("host_cmd")),
      "v0.19b: BROWSER > ORC surface brings the surface back (only once the host has set one up)")
# ── v0.20: the menu's structure, knob banks, beat marks ──
_mb = re.search(r"static const mitem_t MENU\[\] = \{(.*?)\n\};", SRC, re.S).group(1)
_items = re.findall(r'\{"([^"]*)",\s*(\d+),\s*(MK_\w+),\s*[^,]+,\s*(\d+|[A-Z_]+),', _mb)
_npages = int(re.search(r"#define MENU_PAGES (\d+)", SRC).group(1))
_pmax = int(re.search(r"#define MENU_PAGE_MAX (\d+)", SRC).group(1))
_titles = re.findall(r'"([^"]*)"', re.search(r"MENU_TITLE\[MENU_PAGES\] = \{(.*?)\};", SRC, re.S).group(1))
_parents = [int(x) for x in re.findall(r"\d+", re.search(r"MENU_PARENT\[MENU_PAGES\] = \{(.*?)\};", SRC, re.S).group(1))]
_subs = {int(_n): int(_pg) for _nm, _pg, _kd, _n in _items if _kd == "MK_SUB"}
_count = {}
for _nm, _pg, _kd, _n in _items:
    _count[int(_pg)] = _count.get(int(_pg), 0) + 1
check(len(_titles) == _npages and len(_parents) == _npages and all(len(t) <= 10 for t in _titles),
      "every menu page has a title (<= 10 chars) and a parent")
check(sorted(_subs) == list(range(1, _npages)) and all(_parents[pg] == par for pg, par in _subs.items()),
      "every page is opened by exactly one item, and BROWSER goes back to the page that holds it")
check(all(c <= _pmax for c in _count.values()) and all(len(_nm) <= 19 for _nm, _pg, _kd, _n in _items if _kd == "MK_INFO"),
      "no page holds more than MENU_PAGE_MAX items; every shortcut line fits beside the scroll bar (<= 19)")
_ev = code(body("handle_events"))
check(re.search(r"case IN_KNOBMODE:\s*if \(held_t >= 0 && held_row < 0\) \{ knob_page \^= 1; held_pad_turned = true; \}\s*else \{ roll_key = true; roll_key_used = false;", _ev) is not None,
      "KNOB MODE: a held step flips its knob page (and counts as an edit, so releasing keeps the step); otherwise it is the roller key")
check("int kc = kctx(), b = *kc_bank(kc) & 3, f = kb_fn(kc, b, k);" in _ev and "uint8_t ch = (uint8_t)(kb_chan(kc, b, k) - 1);" in _ev
      and "midi_send3((uint8_t)(0xB0 | ch), (uint8_t)f" in _ev,
      "the free knobs send what their SECTION's bank says (CC + channel from kb_fn / kb_chan)")
_kf = code(body("kb_fn"))
check("return (k & 3) == 0 ? KB_SWING : 16 + (k & 3);" in _kf and "return 16 + 4 * (b & 3) + (k & 3);" in _kf,
      "bank 1 out of the box = the old knobs (swing, CC 17-19); banks 2-4 = CC 20-31")
check("!G.mark_off && (G.mark_space == 1 ? s % 16 == 0 : s % 4 == 0)" in code(body("paint")),
      "beat / bar marks follow Display: off, every beat / every bar / both")
_ls = code(body("load_state"))
check(re.search(r"if \(!G\.lt_idle_v18\) \{[^}]*G\.lt_idle = 0;[^}]*save_force = true;", _ls) is not None
      and "G.lt_idle = 0;" in code(body("lights_defaults")),
      "v0.18: Idle start is OFF by default and switched off once in an existing save (and that is saved)")

# ── 2026-10-15: drum groups on the MPC's panel pads (the owner's JJOS2XL map, CC on ch 14) ──
check("#define MPC_PANEL_CH 14" in SRC
      and "static const uint8_t MPC_PAD_CC[16] = {20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 52, 53, 54, 55};" in SRC
      and "return grid_mpc(g) ? MPC_PANEL_CH :" in code(body("grid_chan")),
      "MPC pads: pad 1-12 = CC 20-31, 13-16 = CC 52-55, always on channel 14")
check(re.search(r"if \(grid_mpc\(g\)\) \{\s*midi_send3\(\(uint8_t\)\(0xB0 \| ch\), note, 127\);[^}]*live\[pad\]\.cc = true;[^}]*continue;\s*\}\s*int lv = ", _ev) is not None,
      "MPC pads: a press sends its CC = 127 and is never recorded as a note")
check(re.search(r"if \(live\[pad\]\.cc\) \{\s*live\[pad\]\.cc = false;\s*midi_send3\(\(uint8_t\)\(0xB0 \| live\[pad\]\.chan\), live\[pad\]\.note, 0\);", _lr) is not None,
      "MPC pads: the release sends the same CC = 0 (not a note off)")

# ── 2026-10-19: several row buttons at once; PATTERN/SONG taps ──
check("int m = ctl_row_press(&held_row, &turned, &row_consumed, row);" in _ev
      and "ctl_row_release(&held_row, &turned, &row_consumed, row)" in _ev
      and "toggle_mute(row_trk[m])" in _ev and "toggle_mute(held_row_t)" not in _ev,
      "row buttons: the mute tap goes through ctl_row_press / ctl_row_release (several rows can be down at once; a row another lands on mutes then)")
check("row_consumed = 0;" in code(body("set_mode")),
      "row buttons: changing mode forgets a pending chord (a stale 'consumed' bit would swallow the next tap)")
_ps = _ev[_ev.find("case IN_PATSONG:"):_ev.find("case IN_STEP:")]
check("if (!fire_held(IN_SHIFT)) { set_mode(MODE_STEP); song_edit = !song_edit;" in _ps and "G.song_on = !G.song_on;" in _ps
      and _ps.find("song_edit = !song_edit") < _ps.find("G.song_on = !G.song_on;"),
      "PATTERN/SONG: a plain tap opens / closes the song editor; SHIFT + tap is song mode on / off")

# ── v0.23: per-track volume (the STEP faders) ──
check("ctl_trk_vel(LEVEL_VEL[lv], trk_level(t))" in SRC and "ch, *lock_cc_p(t), tvel, &held_notes[t]" in SRC
      and "ratq[q].vel = tvel;" in SRC and "ratq[q].vel = LEVEL_VEL[lv]" not in SRC,
      "track volume: the track's level scales the velocity of its steps AND of its ratchets")
check("if (mode == MODE_STEP && !song_edit && !surface_on && !picker_on()) { trkvol = !trkvol; held_t = held_s = -1; held_pad = -1; dirty = true; }" in _ev
      and "else set_mode(MODE_STEP);" in _ev,
      "STEP pressed again (on the steps, no editor / surface / picker) flips the track volume faders; from any other mode it just goes to the steps")
check("trk_level_set(vtrack(view_top + pad / 16), ctl_trk_pad_level(pad % 16))" in _ev
      and "trk_level_set(vtrack(view_top + k), trk_level(vtrack(view_top + k)) + v)" in _ev,
      "track volume: a pad tap and the knobs set the level of the track on that row")
check("{ song_edit = false; trkvol = false; }" in code(body("set_mode")),
      "track volume: any other mode button leaves the faders")

check(SRC.count("lights_off(); song_edit = false; trkvol = false; held_t = held_s = -1; held_pad = -1;") == 2
      and "trkvol && held_row < 0 && !surface_on) {" in _ev,
      "track volume: both ways onto the ORC surface leave the faders, and the volume knobs never run under the surface (review 2026-10-19)")
# ── 2026-10-19: fader pads: bright up to a dim TIP pad at the level, dark above ──
_pf, _pt = code(body("paint_faders")), code(body("paint_trkvol"))
check("int pst = ctl_fader_pad(c, G.fad_val[f]);" in _pf and "else if (pst == 1) { R = rgb[0] ? 1 : 0;" in _pf
      and "int R, Gn, B, pst = ctl_fader_pad(c, tl);" in _pt and "else if (pst == 1) { R = rgb[0] ? 1 : 0;" in _pt
      and "else { R = Gn = B = 0; }" in _pt and "c == PAGE - 1" not in _pf + _pt and "lit" not in _pf.replace("lit_", "") + _pt.replace("lit_", ""),
      "fader pads (PERFORM faders and the track volume faders): bright up to the level's tip pad, the tip dim, dark above (ctl_fader_pad)")
# ── v0.24: tap tempo = ALT + STEP ──
_tt = code(body("tap_tempo"))
_stp = _ev[_ev.find("case IN_STEP:"):_ev.find("case IN_NOTE:")]
check("if (fire_held(IN_ALT)) { tap_tempo(); break; }" in _stp
      and _stp.find("tap_tempo()") < _stp.find("trkvol = !trkvol")
      and "if (ext_clock) return;" in _tt and "int b = ctl_tap(&tap_state, ms_ticks);" in _tt
      and "BPM = (uint16_t)b;" in _tt and "pop_kind = 1;" in _tt,
      "tap tempo: ALT + STEP (before the plain-STEP handling) feeds ms_ticks to ctl_tap, sets BPM, shows the big number, and does nothing under an external clock")
# ── v0.26: the Memory page (how many steps fit) ──
_mv = code(body("menu_value"))
check("case MK_ROOM:" in _mv and "steps_room()" in _mv and "room / ST_DRUM_SIZE" in _mv and "room / ST_MEL_SIZE" in _mv
      and "m->kind == MK_INFO || m->kind == MK_ROOM" in SRC and SRC.count("MK_ROOM, 0,") == 3
      and '{"Memory",      0, MK_SUB,    0, 16, 0, 0}' in SRC,
      "Memory page: the top menu opens it, its 3 rows read steps_room() (drum 3 bytes, melodic 6), and SELECT on them changes nothing")
# ── 2026-10-23: real-time bytes go out as CIN 0xF packets (a hardware USB host dropped CIN 0x5) ──
_ms1 = code(body("midi_send1"))
check("const uint8_t pkt[4] = {0x0F, a, 0, 0};" in _ms1 and "tud_midi_packet_write(pkt);" in _ms1
      and "stream_write" not in _ms1,
      "clock / start / stop: one USB-MIDI packet with CIN 0xF (single byte), never stream_write (CIN 0x5)")
# ── v0.28: pad release hold-off (drum pads retriggered: a flicker as a hit lands = note-off + a fresh note-on) ──
_pu = code(body("pads_up_due"))
check(0 < _he.find("pads_up_due();") < _he.find("while ((e = fire_event()) >= 0)")
      and re.search(r"if \(pad >= 0 && \(up_wait >> pad & 1\)\) \{\s*if \(down\) up_wait &= ~\(\(uint64_t\)1 << pad\);\s*else up_ms\[pad\] = ms_ticks;\s*continue;\s*\}", _he) is not None
      and _he.find("(up_wait >> pad & 1)") < _he.find("overlay_event(idx, pad)"),
      "hold-off: a pad back down inside its hold-off is the same hit (nothing sent, never reaches the menu / lights); due releases run every pass")
check(re.search(r"if \(pad >= 0 && !down && live\[pad\]\.on\) \{[^}]*if \(pad_hold_ms\(\)\) \{ up_wait \|= \(uint64_t\)1 << pad; up_ms\[pad\] = ms_ticks; \}\s*else live_release\(pad\);", _he) is not None
      and "ms_ticks - up_ms[pad] >= pad_hold_ms()" in _pu and "live_release(pad);" in _pu and "if (!live[pad].on) return;" in _lr,
      "hold-off: a live pad's release waits PAD_HOLD_MS (off = at once), then live_release ends exactly that note")
check("static const uint8_t PAD_HOLD_MS[6] = {0, 10, 20, 30, 40, 60};" in SRC and "#define PAD_HOLD_DEFAULT 2" in SRC
      and '{"Pad release", 2, MK_ENUM,   &G.pad_hold, 6, NM_PADHOLD, 0}' in SRC
      and "if (!G.pad_hold_v28) { G.pad_hold = PAD_HOLD_DEFAULT; G.pad_hold_v28 = 1; }" in loop,
      "hold-off: BROWSER > Playing > Pad release, 20 ms by default (also for a save from before it)")
# ── v0.28: the MPC remote (DRUM again): the owner's layout of his JJOS2XL panel map, checked pad by pad ──
# his map (memory mpc1000-midi-map), notes counted C3 = 60 like the MPC; layout = his, 2026-10-23
_NN = {n: i for i, n in enumerate(["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"])}
def _note(s): return 12 * (int(s[-1]) + 2) + _NN[s[:-1]]
_WANT = [
    ["CC52", "CC53", "CC54", "CC55", "G2", "G#2", "F#2", "C#3", "A2", "CC17", "D1", "D#1", "E1", "F1", "F#1", "G1"],      # pads 13-16, TAP ERASE UNDO NUMERIC, FULL LV, BANK A, F1-F6
    ["CC28", "CC29", "CC30", "CC31", "C#2", "E2", "F2", "C2", "A#2", "CC18", "-", "F3", "E3", "-", "-", "-"],              # pads 9-12, MODE MAIN WINDOW AFTER1, 16 LV, BANK B, STEP back/fwd
    ["CC24", "CC25", "CC26", "CC27", "C1", "G#1", "C#1", "D#2", "B2", "CC19", "-", "D#3", "D3", "-", "-", "CC16"],         # pads 5-8, DATA- UP DATA+ AFTER2, NEXT SEQ, BANK C, LOCATE, PLAY START
    ["CC20", "CC21", "CC22", "CC23", "A#1", "A1", "B1", "D2", "C3", "CC9", "-", "-", "CC12", "CC13", "CC14", "CC15"],       # pads 1-4, LEFT DOWN RIGHT SHIFT, T.MUTE, BANK D, REC OVD STOP PLAY
]
_mt = re.search(r"MPCREM\[64\] = \{(.*?)\n\};", SRC, re.S)
_ent = re.findall(r"MRN\((\d+), (K_\w+)\)|MRC\((\d+), (K_\w+)\)|(MRX)", code(_mt.group(1))) if _mt else []
_got = [f"CC{c}" if c else ("-" if x else str(n)) for n, kn, c, kc, x in _ent]
_exp = [w if w.startswith("CC") or w == "-" else str(_note(w)) for row in _WANT for w in row]
check(_got == _exp and _note("B2") == 59 and _note("C1") == 36,
      "MPC remote: all 64 pads match the owner's layout of his panel map (55 controls, 9 empty, notes C3 = 60)")
# his colours, sent from the picker 2026-10-23 (top row first, '-' = no button)
_COLS = [
    "blue blue blue blue grey white red mint orange amber indigo purple indigo purple indigo purple",
    "blue blue blue blue green grey yellow indigo orange amber - white white - - -",
    "blue blue blue blue teal white teal indigo orange amber - yellow yellow - - white",
    "blue blue blue blue white white white sky orange amber - - red red grey white",
]
_gotc = [(kn or kc)[2:].lower() if not x else "-" for n, kn, c, kc, x in _ent]
_order = ["red", "orange", "amber", "yellow", "lime", "green", "mint", "teal", "cyan", "sky", "blue", "indigo", "purple", "magenta", "pink", "white", "grey"]
_kenum = re.search(r"enum \{ K_RED,(.*?)\};", SRC, re.S)
_kn = ["red"] + [s.strip()[2:].lower() for s in _kenum.group(1).split(",") if s.strip()] if _kenum else []
_fadn = re.findall(r'"(\w+)"', re.search(r"FAD_COL_NAME\[16\] = \{(.*?)\};", SRC, re.S).group(1))
check(_gotc == " ".join(_COLS).split() and _kn == _order and _fadn == _order[:16]
      and "k < 16 ? FAD_RGB[k] : MR_GREY" in code(body("paint_mpcrem")),
      "MPC remote: every pad has the owner's colour; K_ order = the fader colours (FAD_RGB) + grey")
_dr = _ev[_ev.find("case IN_DRUM:"):_ev.find("case IN_REC:")]
check("if (mode == MODE_DRUM && !surface_on && !picker_on()) { mpcrem = !mpcrem; dirty = true; }" in _dr and "else set_mode(MODE_DRUM);" in _dr
      and "if (m != MODE_DRUM) mpcrem = false;" in code(body("set_mode")),
      "MPC remote: DRUM pressed again toggles it; any other mode leaves it")
check(re.search(r"if \(mode == MODE_DRUM && mpcrem\) \{\s*int ty = MPCREM\[pad\]\.type;\s*if \(ty == MR_NONE\) continue;\s*uint8_t ch = MPC_PANEL_CH - 1, v = MPCREM\[pad\]\.val;\s*"
                r"midi_send3\(\(uint8_t\)\(\(ty == MR_CC \? 0xB0 : 0x90\) \| ch\), v, 127\);\s*live\[pad\]\.note = v; live\[pad\]\.chan = ch;[^\n]*live\[pad\]\.cc = ty == MR_CC; live\[pad\]\.on = true;\s*dirty = true;\s*continue;", _he) is not None
      and _he.find("if (mode == MODE_DRUM && mpcrem)") < _he.find("if (mode == MODE_DRUM && fire_held(IN_ALT)) { grid_sel"),
      "MPC remote: a pad sends its note / CC 127 on channel 14 (before ALT or recording), its release goes through live[] (CC 0 / note off)")
check("mode == MODE_DRUM && !mpcrem && fire_held(IN_ALT) && k < 3" in _ev and "else if (mode == MODE_DRUM && mpcrem) paint_mpcrem();" in code(body("paint"))
      and code(body("render")).find("mode == MODE_DRUM && mpcrem") < code(body("render")).find("mode == MODE_DRUM && fire_held(IN_ALT)"),
      "MPC remote: its own pad colours and screen; the knobs don't edit the hidden groups")
check("if (live[q].on && !live[q].cc && !live[q].sx && live[q].note == live[pad].note && live[q].chan == live[pad].chan) other = true;" in _lr,
      "review fix: a held CC pad (MPC pad CC 52) or surface pad never blocks a note-off of the same number (MAIN = note 52)")
# ── v0.29: 32 tracks, CC tracks, undo, the roller ──
_ps = code(body("play_step"))
_vt = [m.start() for m in re.finditer(r"view_top \+ ", code(SRC))]
check(len(_vt) >= 9 and all(code(SRC)[i - 7:i] == "vtrack(" for i in _vt),
      "32 tracks: every display row -> track goes through vtrack (T1-T16, T17-T24, M1-M8)")
check("if (is_cc(t)) {" in _ps and "out_cc(ch, tnote(t), v.lock ? (uint8_t)(v.lock - 1) : LEVEL_VEL[lv]);" in _ps
      and _ps.find("if (is_cc(t)) {") < _ps.find("play_step_notes("),
      "a CC track's step sends CC <its note> = the step's lock value, else the level's velocity (no note)")
check("if (!is_cc(i) && tnote(i) == note && tchan(i) == chan1 &&" in code(body("record_hit"))
      and "if (!is_cc(i) && track_unused(i)) t = i;" in code(body("record_hit")),
      "recorded drum hits go to note tracks (T1-T24), never to a CC track")
check("G.cc_trk[i] &= (uint8_t)(i == 2 ? 0x00 : 0xFF)" in code(body("load_state")),
      "a saved CC bit on a melodic track (16-23) is cleared at boot")
check("track > 31" in code(body("lights_hit", LIGHTS)), "the TRACKS light show takes hits from all 32 tracks")
check(code(body("edited_pattern")).find("undo_edited(p);") >= 0 and code(body("touched_pattern")).find("undo_edited(p);") >= 0,
      "undo: every pattern change (an edit, a recorded hit) can turn the gesture's snapshot into the undo point")
_rs = code(body("record_step"))
check("undo_begin(P, true);" in _rs and _rs.find("undo_begin(P, true);") < _rs.find("steps_write(")
      and re.search(r"undo_begin\(CURPAT, false\);\s*if \(fire_held\(IN_ALT\)\) \{\s*steps_len\[CURPAT\]\[t\]", _ev) is not None
      and 0 <= code(body("pattern_step")).find("undo_begin(to, false);") < code(body("pattern_step")).find("steps_copy_pattern("),
      "undo: a snapshot is taken before a step-pad gesture, a REC take and a pattern copy (the pattern copied into)")
check("undo_open = false;" in _ev[_ev.rfind("undo_open && held_pad < 0"):],
      "undo: a gesture ends when no step pad is held and no REC take is running")
check(re.search(r"case IN_REC:\s*if \(fire_held\(IN_SHIFT\)\) \{ undo_do\(\); break; \}\s*rec_arm = !rec_arm;", _ev) is not None,
      "undo: SHIFT + REC = undo / redo; a plain REC still arms recording")
_ud = code(body("undo_do"))
check(0 <= _ud.find("undo_take(p, spare)") < _ud.find("cancel_save_of(KIND_PAT(p))") < _ud.find("steps_record_load(p,") < _ud.find("edited_pattern(p);")
      and "undo_open = false;" in _ud[:_ud.find("undo_take(")],
      "undo: keeps the redo, cancels a save of that pattern, THEN restores it, then saves it; a REC take running ends")
check("if (undo_gok) { undo_cur ^= 1; undo_pat = p; }" in code(body("undo_edited")) and "if (undo_open && undo_gpat == p) { if (rec) undo_rec = true; return; }" in code(body("undo_begin")),
      "undo: only a snapshot that fitted becomes the undo point; one gesture = one snapshot")
check("uint32_t gp = roll_g(t, g);" in _ps and "steps_read(CURPAT, t, track_pos(t, gp), &v)" in _ps and "gp / (uint32_t)tlen(CURPAT, t)" in _ps
      and "int nr = ctl_roll_hits(roll_c1[t] - 1);" in _ps and "if (!nr) nr = step_ratchet(v.b);" in _ps,
      "roller: each track plays its roll position (steps + conditions) and a 1/32-1/64 roll forces its hits; the real step counter is untouched")
check("track_pos(t, roll_g(t, last))" in code(body("paint")) and "track_pos(t, roll_g(t, last))" in code(body("render")),
      "roller: the playhead (pads + screen) shows where a rolling track really is")
check("if (roll_pads >> pad & 1) roll_release(pad); else live_release(pad);" in code(body("pads_up_due"))
      and re.search(r"if \(pad >= 0 && !down && \(roll_pads >> pad & 1\)\) \{\s*if \(pad_hold_ms\(\)\) \{ up_wait", _ev) is not None,
      "roller: a roller pad's release waits out the same hold-off as a live pad (a flicker doesn't restart the roll)")
check("if (pad >= 0 && down && (roll_ready() || (roll_pads && roll_shown()))) { roll_press(pad); continue; }" in _ev
      and _ev.find("roll_shown()))) { roll_press") < _ev.find("if (pad >= 0 && surface_on)"),
      "roller: with KNOB MODE held, a pad press is a roller pad")
check("if (c >= 0 && !roll_c1[t]) roll_at[t] = roll_anchor();" in code(body("roll_update"))
      and "return (playing && pending >= 0) ? (uint32_t)pending : gstep;" in code(body("roll_anchor")),
      "roller: a roll starts at the next step to sound (a swung step still due counts); a new length keeps the start")
check("memset(roll_at, 0, sizeof roll_at)" in code(body("stop")) and "memset(roll_at, 0, sizeof roll_at)" in code(body("midi_in_byte")),
      "roller: a rewind (STOP, an external START) restarts a held roll from the top")
check("if (!down && idx == IN_KNOBMODE && roll_key) {" in _ev and "if (!roll_key_used) { uint8_t *kb = kc_bank(kctx()); *kb = (uint8_t)((*kb + 1) & 3);" in _ev,
      "KNOB MODE let go: a tap without a roller pad steps the knob bank (of the section the pads are in)")
check("return mode == MODE_NOTE ? KC_NOTE : mode == MODE_DRUM ? (mpcrem ? KC_MPC : KC_DRUM) : KC_STEP;" in code(body("kctx"))
      and "int kb = *kc_bank(kctx()) & 3;" in code(body("paint")) and "menu_kc = kctx();" in code(body("overlay_event")),
      "knob banks per section: STEP / NOTE / DRUM / MPC remote; the lights and BROWSER > Knobs follow the section the pads are in")
check("kc_init_copies();" in code(body("main")) and "if (G.kbx_init) return;" in code(body("kc_init_copies"))
      and "memcpy(G.kbx_fn[c], G.kb_fn, sizeof G.kb_fn);" in code(body("kc_init_copies")),
      "an older save: NOTE / DRUM / MPC banks start as copies of the STEP banks (nothing changes until edited)")
_dp = code(SRC)[code(SRC).find("if (mode == MODE_DRUM && fire_held(IN_ALT)) { grid_sel"):code(SRC).find("if (rec_arm) record_hit(note, (uint8_t)(ch + 1), lv, now);")]
check("if (grid_lvl(g)) {" in _dp and "vel = level16_vel(grid_padnum(r, c));" in _dp
      and "grid_lvl(g) ? (uint8_t)grid_base(g)" in code(body("grid_note")) and "return (uint8_t)(((p + 1) * 127 + 8) / 16);" in code(body("level16_vel")),
      "16 levels: every pad of the group plays its note, at the pad's velocity (pad 1 = 8 .. pad 16 = 127)")
check("lv = vel < 80 ? LV_SOFT : vel < 114 ? LV_NORMAL : LV_ACCENT;" in _dp,
      "16 levels: a recorded hit lands at the nearest step level")
check("static const uint8_t ORDER[3] = {0, 2, 1};" in _ev and "if (grid_mpc(g)) G.grid_layout[g] = 0;" in _ev,
      "16 levels: 1C steps notes -> 16 levels -> MPC pads; 1B keeps 16 levels (only MPC pads, which have no note, go back to notes)")
check("if (undo_open && held_pad < 0 && !(undo_rec && rec_arm && (playing || ext_playing))) undo_open = false;" in _ev
      and "undo_begin(P, true);" in code(body("record_step")),
      "review fix: only a REC take that recorded something stays one edit; plain edits while REC is armed are separate")
check("if (pad >= 0 && down && (roll_ready() || (roll_pads && roll_shown()))) { roll_press(pad); continue; }" in _ev,
      "review fix: while a roll is held the pads stay the roller (no hidden step edits after KNOB MODE is let go)")
check("&& (!is_x(i) || !track_unused(i) || G.x_note[x_of(i)])) t = i;" in code(body("record_hit")),
      "review fix: an empty T17-T24 never catches a recorded hit by its default note")
print(f"wiring_test: {p} passed, {f} failed (expected {EXPECTED})")
sys.exit(1 if f or p + f != EXPECTED else 0)
