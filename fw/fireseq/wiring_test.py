"""main.c is bare-metal and cannot run on the host, so ctl_test.c proves the RULES and this proves main.c USES them
(each check names the review finding it guards). fire_live_test.py then proves the behaviour on the real Fire.
    python3 wiring_test.py        (any OS; reads main.c next to it)"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, "main.c"), encoding="utf-8").read().replace("\r\n", "\n")
EXPECTED = 215
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


check(not re.search(r"\blt_on\b|\blt_host\b|\blt_eat\b|lights_on\(|lights_off\(|lights_task|draw_lights_screen|menu_lights_now", code(SRC))
      and '"Pi VU"' not in SRC and '"Start Pi VU"' not in SRC and "lights.c" not in open(os.path.join(HERE, "Makefile")).read()
      and re.search(r"case HC_LEVELS:\s*case HC_LIGHTS:\s*break;", code(body("host_cmd"))) is not None,
      "v0.36 the PI VU bars are gone: no state, no task, no menu, no lights.c in the build; 01 / 02 are accepted and ignored")

check("touched_lazy()" in code(body("touched_global")) and "touched_lazy()" in code(body("touched_pattern")),
      "4.1 touched_global / touched_pattern arm the lazy save")
check("ctl_lazy_arm(ms_ticks)" in code(body("touched_lazy")) and "!playing && !ext_playing" in code(body("touched_lazy")),
      "4.1 the lazy save is armed only while stopped")
check("commit_pending()" in code(body("stop")), "4.1 STOP commits what waits")
check("save_lazy_due" in code(body("unsaved")), "4.1 the star shows during the lazy window")
loop = code(body("main"))
check("ctl_lazy_due(save_lazy_due, ms_ticks)" in loop, "4.1 the main loop promotes a due lazy save")
check("save_forced_run = save_force" in code(body("saver_start")), "4.1 a save remembers if it was forced")
check("if (save_forced_run) save_force = true" in code(body("saver_poll")), "4.1 a failed forced save retries forced")
check("if (save_forced_run) save_force = true" in code(body("cancel_save_of")), "4.1 a cancelled forced save re-arms forced")
check(re.search(r"if \(converted\) \{[^}]*save_force = true", code(body("load_state"))) is not None,
      "4.1 the boot format migration saves even with AUTOSAVE off")

hc = code(body("host_cmd"))
check("ctl_host_parse(p, n)" in hc, "4.4 host_cmd uses the tested parser")
check("HC_TEXT" not in code(SRC) and "HC_EFFECT" not in code(SRC) and "HC_SPEED" not in code(SRC), "v0.31r host 03 / 04 / 05 (speed, show, text) are gone")
check("ctl_sysex_payload(sx, sx_len, sizeof sx" in code(body("midi_in_byte")), "the SysEx collector uses ctl_sysex_payload")

check("ctl_idle" not in code(SRC) and "lt_input_ms" not in SRC and "lights_on(" not in loop,
      "v0.31r the idle light start is gone (the owner never wanted the screen saver)")
check("RTC->BKP14R = BUILD_ID" in code(body("crash_record")) and "ci->build = RTC->BKP14R" in code(body("crash_load"))
      and 'BUILD_TAG[] = "ORCBUILD=" BUILD_ID_STR' in SRC, "a crash record names the build that crashed (and the image carries its tag)")

check(re.search(r"mpu_ram_xn\(\);", loop) is not None and "MPU_RASR_XN_Msk" in code(body("mpu_ram_xn"))
      and "MEMFAULTENA" in code(body("mpu_ram_xn")), "v0.17c: main enables the MPU with RAM execute-never + MemManage")
check("snap_take(f)" in code(body("crash_from_fault")) and "snap_take(" in code(body("__stack_chk_fail")),
      "v0.17c: a fault and a stack smash keep the stack snapshot")
check(all(re.search(r"static void __attribute__\(\(noinline\)\) " + n + r"\(", SRC) for n in
          ("render", "host_cmd", "midi_in_byte", "midi_task", "handle_events", "paint", "draw_menu")),
      "v0.17c: main's buffer-owning callees are noinline (so their canaries are checked)")

HW = open(os.path.join(HERE, "fire_hw.c"), encoding="utf-8").read().replace("\r\n", "\n")
tim4 = code(body("tim4_body", HW))                  # v0.29d: TIM4_IRQHandler is a naked entry, the work is in tim4_body
_inner = tim4[tim4.find("{") + 1:tim4.rfind("}")].strip() if "{" in tim4 else ""
_pre = _inner[:_inner.find("canary_watch_arm(out, out2, in, CHAIN);")] if "canary_watch_arm(out, out2, in, CHAIN);" in _inner else _inner
_post = _inner[_inner.find("canary_watch_disarm();"):] if "canary_watch_disarm();" in _inner else _inner
check(_inner.startswith("uint8_t out[CHAIN], out2[CHAIN], in[CHAIN];") and all(w not in _pre for w in ("TIM4->SR", "memset", "SPI2", "led_level"))
      and all(w not in _post for w in ("encoder_step", "SPI2", "ev_push", "in_stable", "memset")),
      "TIM4 arms the canary watchpoint before any of its work and disarms it after all of it (only the slip checks outside)")
_aw = code(body("canary_watch_arm", HW))
check("DWT->FUNCTION0 = 6" in _aw and "w[i] == g" in _aw and "i < 3" in _aw and "__get_MSP" not in _aw,
      "the watchpoint is a data-WRITE watch on the guard word just above the arrays (never a first match from SP)")
check(re.search(r"dbgmon_init\(\);", loop) is not None and "MON_EN" in code(body("dbgmon_init")) and "DebugMonitor_IRQn, 0" in code(body("dbgmon_init")),
      "main enables the debug monitor at priority 0 (it must preempt TIM4)")
check("CRASH_VECTOR(DebugMon_Handler, 7)" in SRC, "DebugMon records a crash (cause 7, watchpoint)")
_he = code(body("handle_events"))
_lr = code(body("live_release"))
check(not re.search(r"surface_on|surf_send|surf_rgb|paint_surface|menu_surface_now|\.sx\b", code(SRC))
      and '"ORC pads"' not in SRC and "live[pad].on = false;" in _lr,
      "v0.36 the ORC pads page (the 0A / 0B pad surface) is gone: no state, no paint, no menu row, no 0C pad messages")


def case_text(label, fn="host_cmd"):
    """One `case LABEL:` of a switch in function fn, up to the next case / default (comments removed)."""
    b = code(body(fn))
    m = re.search(r"case " + re.escape(label) + r":(.*?)(?=\n\s*case \w+:|\n\s*default:)", b, re.S)
    return m.group(1) if m else ""


check(re.search(r"case HC_COLORS:\s*case HC_SURFACE:\s*break;", code(body("host_cmd"))) is not None,
      "v0.36 0A / 0B (an older ORC arming the pad surface) are accepted and ignored")
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
check(sorted(_subs) == [x for x in range(1, _npages) if x != 1]   # page 1 (Pi VU) retired in v0.36
      and all(_parents[pg] == par for pg, par in _subs.items()),
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

# ── 2026-10-15: drum groups on the MPC's panel pads (the owner's JJOS2XL map, CC on ch 14) ──
check("#define MPC_PANEL_CH 14" in SRC and "grid_mpc" not in code(SRC) and "MPC_PAD_CC" not in code(SRC)
      and "G.grid_layout[g] = 0;" in code(body("load_state")),
      "v0.29b: the groups' MPC-pads map is gone (the remote covers it); an old saved map loads as notes")
check(re.search(r"if \(live\[pad\]\.cc\) \{\s*live\[pad\]\.cc = false;\s*midi_send3\(\(uint8_t\)\(0xB0 \| live\[pad\]\.chan\), live\[pad\]\.note, live\[pad\]\.off\);", _lr) is not None,
      "MPC remote: a CC pad's release sends the same CC = its off value (the MPC's 0; v0.31: a UNIVERSAL CC hold pad's own), not a note off")

# ── 2026-10-19: several row buttons at once; PATTERN/SONG taps ──
check("int m = ctl_row_press(&held_row, &turned, &row_consumed, row);" in _ev
      and "ctl_row_release(&held_row, &turned, &row_consumed, row)" in _ev
      and "toggle_mute(row_trk[m])" in _ev and "toggle_mute(held_row_t)" not in _ev,
      "row buttons: the mute tap goes through ctl_row_press / ctl_row_release (several rows can be down at once; a row another lands on mutes then)")
check("if (held_row >= 0) for (int k = 0; k < 4; k++) if (fire_knob_pending(k)) held_row_turned = true;" in _ev
      and 0 <= _ev.find("fire_knob_pending(k)") < _ev.find("fire_event()")
      and "int fire_knob_pending(int k) { return knob_out[k] + rin_turn_pending(&rin, k); }" in open(os.path.join(HERE, "fire_hw.c"), encoding="utf-8").read().replace("\r\n", "\n"),
      "v0.29c: a knob turn pending while a row is held counts as 'turned' BEFORE the events (a release right after a turn no longer mutes)")
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
check("if (mode == MODE_STEP && !song_edit && !picker_on()) { trkvol = !trkvol; held_t = held_s = -1; held_pad = -1; dirty = true; }" in _ev
      and "else set_mode(MODE_STEP);" in _ev,
      "STEP pressed again (on the steps, no editor / surface / picker) flips the track volume faders; from any other mode it just goes to the steps")
check("trk_level_set(vtrack(view_top + pad / 16), ctl_trk_pad_level(pad % 16))" in _ev
      and "trk_level_set(vtrack(view_top + k), trk_level(vtrack(view_top + k)) + v)" in _ev,
      "track volume: a pad tap and the knobs set the level of the track on that row")
check("{ song_edit = false; trkvol = false; }" in code(body("set_mode")),
      "track volume: any other mode button leaves the faders")

# ── 2026-10-19: fader pads: bright up to a dim TIP pad at the level, dark above ──
_pf, _pt = code(body("paint_faders")), code(body("paint_trkvol"))
check("int pst = ctl_fader_pad(c, s.val[f]);" in _pf and "else if (pst == 1) { R = rgb[0] ? 1 : 0;" in _pf
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
# ── v0.35: BROWSER > About (version + the crash on record, for a tester's photo) ──
_about = [(_nm, _n) for _nm, _pg, _kd, _n in _items if _kd == "MK_ABOUT"]
check('{"About",       0, MK_SUB,    0, 17, 0, 0}' in SRC and all(_pg == "17" for _nm, _pg, _kd, _n in _items if _kd == "MK_ABOUT")
      and sorted(_n for _nm, _n in _about) == sorted(["ABOUT_VERSION", "ABOUT_BUILD", "ABOUT_CAUSE", "ABOUT_STAGE", "ABOUT_TOTAL",
                                                      "ABOUT_UPTIME", "ABOUT_PC", "ABOUT_LR", "ABOUT_MISREADS"])
      and _titles[17] == "About" and _parents[17] == 0,
      "About page: the top menu opens page 17, which holds exactly one row per ABOUT_* value; BROWSER goes back to the top")
check([_n for _nm, _n in _about][:6] == ["ABOUT_VERSION", "ABOUT_BUILD", "ABOUT_CAUSE", "ABOUT_STAGE", "ABOUT_PC", "ABOUT_LR"],
      "About page v0.35b: the first screenful (6 rows) = version, build, cause, stage, PC, LR: one photo is a whole crash report")
check("case MK_ABOUT:" in _mv
      and "crash_about_value(m->n, &crash_boot, t4_note.magic == T4_NOTE_MAGIC ? t4_note.count : 0u, FW_VERSION, BUILD_ID, o);" in _mv
      and "m->kind == MK_INFO || m->kind == MK_ROOM || m->kind == MK_ABOUT) return;" in SRC
      and re.search(r'#define FW_VERSION "v\d+\.\d+[a-z]?"', SRC) is not None
      and "crash_load(&crash_boot);" in SRC,
      "About rows: the boot-time crash record (crash_load), the live misread note, FW_VERSION + BUILD_ID; SELECT changes nothing")
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
check("if (mode == MODE_DRUM && !picker_on()) { mpcrem = !mpcrem; dirty = true; }" in _dr and "else set_mode(MODE_DRUM);" in _dr
      and "if (m != MODE_DRUM) mpcrem = false;" in code(body("set_mode")),
      "MPC remote: DRUM pressed again toggles it; any other mode leaves it")
check(re.search(r"if \(mode == MODE_DRUM && mpcrem\) \{\s*int ty = MPCREM\[pad\]\.type;\s*if \(ty == MR_NONE\) continue;\s*uint8_t ch = MPC_PANEL_CH - 1, v = MPCREM\[pad\]\.val;\s*"
                r"midi_send3\(\(uint8_t\)\(\(ty == MR_CC \? 0xB0 : 0x90\) \| ch\), v, 127\);\s*live\[pad\]\.note = v; live\[pad\]\.chan = ch;[^\n]*live\[pad\]\.cc = ty == MR_CC; live\[pad\]\.off = 0; live\[pad\]\.on = true;\s*dirty = true;\s*continue;", _he) is not None
      and _he.find("if (mode == MODE_DRUM && mpcrem)") < _he.find("if (mode == MODE_DRUM && fire_held(IN_ALT)) { grid_sel"),
      "MPC remote: a pad sends its note / CC 127 on channel 14 (before ALT or recording), its release goes through live[] (CC 0 / note off)")
check("mode == MODE_DRUM && !mpcrem && fire_held(IN_ALT) && k < 2" in _ev and "else if (mode == MODE_DRUM && mpcrem) paint_mpcrem();" in code(body("paint"))
      and code(body("render")).find("mode == MODE_DRUM && mpcrem") < code(body("render")).find("mode == MODE_DRUM && fire_held(IN_ALT)"),
      "MPC remote: its own pad colours and screen; the knobs don't edit the hidden groups")
check("if (live[q].on && !live[q].cc && live[q].note == live[pad].note && live[q].chan == live[pad].chan) other = true;" in _lr,
      "review fix: a held CC pad (MPC pad CC 52) never blocks a note-off of the same number (MAIN = note 52)")
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
check("lights_hit" not in code(SRC) and not os.path.exists(os.path.join(HERE, "lights.c")), "v0.31r the TRACKS show and the effects are gone from lights.c and play_step")
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
      and _ev.find("roll_shown()))) { roll_press") < _ev.find("if (pad >= 0 && picker_on())"),
      "roller: with KNOB MODE held, a pad press is a roller pad")
check("if (c >= 0 && !roll_c1[t]) roll_at[t] = roll_anchor();" in code(body("roll_update"))
      and "return (playing && pending >= 0) ? (uint32_t)pending : gstep;" in code(body("roll_anchor")),
      "roller: a roll starts at the next step to sound (a swung step still due counts); a new length keeps the start")
check("memset(roll_at, 0, sizeof roll_at)" in code(body("stop")) and "memset(roll_at, 0, sizeof roll_at)" in code(body("midi_in_byte")),
      "roller: a rewind (STOP, an external START) restarts a held roll from the top")
check("if (!down && idx == IN_KNOBMODE && roll_key) {" in _ev and "if (roll_key_used) {}" in _ev
      and "else { uint8_t *kb = kc_bank(kctx()); *kb = (uint8_t)((*kb + 1) & 3);" in _ev,
      "KNOB MODE let go: a tap without a roller pad steps the knob bank (of the section the pads are in)")
check("return mode == MODE_NOTE ? KC_NOTE : mode == MODE_DRUM ? (mpcrem ? KC_MPC : KC_DRUM) : KC_STEP;" in code(body("kctx"))
      and "int kb = (mode == MODE_UNI ? G.uni_bank : *kc_bank(kctx())) & 3;" in code(body("paint")) and "menu_kc = kctx();" in code(body("overlay_event")),
      "knob banks per section: STEP / NOTE / DRUM / MPC remote; the lights and BROWSER > Knobs follow the section the pads are in")
check("kc_init_copies();" in code(body("main")) and "if (G.kbx_init) return;" in code(body("kc_init_copies"))
      and "memcpy(G.kbx_fn[c], G.kb_fn, sizeof G.kb_fn);" in code(body("kc_init_copies")),
      "an older save: NOTE / DRUM / MPC banks start as copies of the STEP banks (nothing changes until edited)")
_dp = code(SRC)[code(SRC).find("if (mode == MODE_DRUM && fire_held(IN_ALT)) { grid_sel"):code(SRC).find("if (rec_arm) record_hit(note, (uint8_t)(ch + 1), lv, now);")]
check("if (grid_lvl(g)) {" in _dp and "vel = level16_vel(grid_padnum(r, c));" in _dp
      and "grid_lvl(g) ? lv16_note :" in code(body("grid_note")) and "return (uint8_t)(((p + 1) * 127 + 8) / 16);" in code(body("level16_vel")),
      "16 levels: every pad of the group plays the chosen note, at the pad's velocity (pad 1 = 8 .. pad 16 = 127)")
check("} else { last_g = g; last_note = note; }" in _dp and "int g = last_g, n = last_note;" in code(body("lv16_toggle"))
      and "lv16_on = true; lv16_g = g; lv16_note = (uint8_t)n;" in code(body("lv16_toggle")),
      "16 levels takes the LAST PAD HIT in DRUM mode (its group and note), not the group's first note")
check(re.search(r"if \(row == 0 && mode == MODE_DRUM && !mpcrem\) \{\s*if \(down\) lv16_toggle\(\);\s*continue;", _ev) is not None
      and 0 <= _ev.find("if (row == 0 && mode == MODE_DRUM") < _ev.find("int m = ctl_row_press("),
      "DRUM mode's top row button toggles 16 levels (instead of muting a track you can't see there)")
check("return lv16_on && g == lv16_g;" in code(body("grid_lvl")) and "led_set(LED_ROW_G[0], lv16_on ? 7 : 1);" in code(body("paint_drums")),
      "16 levels: only that group; the button lights while it is on")
check("lv = vel < 80 ? LV_SOFT : vel < 114 ? LV_NORMAL : LV_ACCENT;" in _dp,
      "16 levels: a recorded hit lands at the nearest step level")
check("mode == MODE_DRUM && !mpcrem && fire_held(IN_ALT) && k < 2" in _ev and "grid_layout[g] = v" not in _ev,
      "group edit: knob 1 channel, knob 2 pad 1's note, nothing else (no map knob)")
check("if (undo_open && held_pad < 0 && !(undo_rec && rec_arm && (playing || ext_playing))) undo_open = false;" in _ev
      and "undo_begin(P, true);" in code(body("record_step")),
      "review fix: only a REC take that recorded something stays one edit; plain edits while REC is armed are separate")
check("if (pad >= 0 && down && (roll_ready() || (roll_pads && roll_shown()))) { roll_press(pad); continue; }" in _ev,
      "review fix: while a roll is held the pads stay the roller (no hidden step edits after KNOB MODE is let go)")
check("&& (!is_x(i) || !track_unused(i) || G.x_note[x_of(i)])) t = i;" in code(body("record_hit")),
      "review fix: an empty T17-T24 never catches a recorded hit by its default note")
_t4 = code(body("tim4_body", HW))
check(r'__asm volatile("mov r0, sp\n\tmov r1, lr\n\tb tim4_body\n\t");' in HW
      and "t4_exc = exc; t4_sp0 = sp_read();" in _t4 and "t4_in = sp_in;" in _t4 and "tim4_slip(1, sp_in, t4_sp0, t4_sp0);" in _t4
      and _t4.find("SLIP_CHECK(2);") < _t4.find("SLIP_CHECK(3);") < _t4.find("SLIP_CHECK(4);")
      and "uint32_t seen = *(const volatile uint32_t *)(t4_in - 4), want = t4_exc;" in _t4
      and "if (seen != want) t4_recheck(seen, want, exc, sp_read());" in _t4,
      "v0.29d diagnostic: TIM4 checks the interrupted frame, its own SP three times and its saved return before returning")
_rc = code(body("t4_recheck", HW))
check("__DSB();" in _rc and "uint32_t slot = *(const volatile uint32_t *)(t4_in - 4), now = t4_exc;" in _rc
      and "int v = crash_t4_verdict(seen, want, slot, now, exc);" in _rc
      and "if (v == T4V_CHANGED) tim4_slip(5, t4_in, t4_sp0, body_sp);" in _rc
      and _rc.find("t4_note.count++;") < _rc.find("if (v == T4V_CHANGED)") < _rc.find("t4_exc = exc;")
      and re.search(r"^t4_note_t t4_note __attribute__\(\(section\(\"\.noinit\"\)\)\);", HW, re.M) is not None,
      "v0.31g: a return-slot mismatch is re-read and judged against the EXC_RETURN TIM4 was handed; noted (in .noinit) "
      "BEFORE a slot that really changed resets the Fire; a stale yardstick is repaired")
_hcr = code(body("host_cmd"))
check("bool tn = t4_note.magic == T4_NOTE_MAGIC;" in _hcr and "crash_boot.t4_exc = tn ? t4_note.exc : 0;" in _hcr
      and _hcr.find("crash_boot.t4_exc = tn") < _hcr.find("crash_reply(&crash_boot, msg + 1);")
      and "crash_snap.magic = 0; t4_note.magic = 0;" in _hcr,
      "v0.31g: 06 reports the LIVE TIM4 check note (v3 reply); 06 01 clears it with the record")
check("crash_record(CRASH_SLIP, f[6], f[5], f[7], kind, sp0, sp_in, ms_ticks, RTC->BKP2R, &dfu);" in code(body("tim4_slip"))
      and "snap_take((const uint32_t *)(sp_now < sp0 ? sp_now : sp0));" in code(body("tim4_slip")),
      "v0.29d diagnostic: a TIM4 slip is recorded (cause 8) with a stack snapshot, then the Fire restarts")
_hc = code(body("host_cmd"))
_hq = _hc[_hc.find("case HC_QUEUE:"):_hc.find("case HC_BARS:")]
check("int r = ctl_queue_pat(h.arg, song, song_len(), PATTERNS, playing || ext_playing, CURPAT, &q);" in _hq
      and "bool song = song_active();" in _hq and "if (r == CQ_QUEUE) queued_pat = q;" in _hq,
      "v0.30 0D: the host's queue follows the PATTERN buttons' rule (song mode = slot; running = either clock)")
check(re.search(r"else if \(r == CQ_NOW\) \{\s*if \(song\) song_goto\(h\.arg\);\s*else \{ CURPAT = \(uint8_t\)h\.arg; queued_pat = -1; \}", _hq) is not None,
      "v0.30 0D while stopped: at once, a song slot via song_goto (like pattern_step)")
check("(uint8_t)(r != CQ_REFUSE), (uint8_t)(CURPAT & 0x7F)," in _hq and "0x43, 0x0D," in _hq,
      "v0.30 0D answers: accepted, the current pattern, the queue")
_bs = code(SRC)
check(re.search(r"^static bool bar_msgs;", SRC, re.M) is not None and _bs.count("bar_msgs =") == 1
      and "bar_msgs = h.arg == 1;" in _hc,
      "v0.30 bar messages are OFF at power-on; only the host's 0E turns them on (nothing self-starts)")
_ps = code(body("play_step"))
check(0 < _ps.find("queued_pat = -1; touched_global(); }") < _ps.find("batch_flush();") < _ps.find("if (bar_msgs && tud_midi_mounted()) {")
      and "ctl_bar_msg(bm, g, PAGE, CURPAT, song_active() ? song_slot : CTL_NO_SLOT)" in _ps,
      "v0.30 0F: sent from play_step after the bar's pattern switch and after the notes, with the pattern now playing")
check("#define HOST_VERSION 9" in SRC, "v0.34 hello says version 9 (9: the short LED slot + 25; 8: 21-24 ORC mode; 7: 20 remote input, no 03-05; 6: 14-bit DAW levels; 5: the DAW page 1A-1E; 4 = the rolled-back v0.32, skipped; 3: UNIVERSAL 10-15; 2 = v0.30)")
# ── v0.31 UNIVERSAL mode (uni.c holds the rules; these prove main.c uses them) ──
_C = code(SRC)
_ev2 = code(body("handle_events"))
check("int to = uni_perform_next(mode, mode_before_faders, MODE_FADER, MODE_DAW, daw.n > 0);" in _ev2
      and "if (to == MODE_FADER) mode_before_faders = mode;" in _ev2 and "set_mode(to);" in _ev2,
      "v0.38 PERFORM tap: FADERS, again the DAW page if the host defined one, again back to the mode before FADERS")
_nt = _ev2[_ev2.find("case IN_NOTE:"):_ev2.find("case IN_DRUM:")]
check("set_mode(picker_on() ? MODE_NOTE : uni_note_next(mode, MODE_NOTE, MODE_UNI));" in _nt
      and "uni_note_next" not in _ev2.replace(_nt, "") and "MODE_UNI" not in code(body("uni_perform_next", open(os.path.join(HERE, "uni.c")).read())),
      "v0.38 NOTE again = UNIVERSAL (and back); UNIVERSAL is no longer in PERFORM's cycle")
check("led_set(LED_NOTE_Y, mode == MODE_NOTE ? 7 : mode == MODE_UNI ? (((ms_ticks / 300) & 1) ? 7 : 2) : 1);" in _C
      and "led_set(LED_PERFORM_Y, mode == MODE_FADER ? 7 : mode == MODE_DAW ? (((ms_ticks / 300) & 1) ? 7 : 2) : 1);" in _C,
      "v0.38 every mode button's second view blinks its light: NOTE in UNIVERSAL, PERFORM on the DAW page")
check("#define UNI_LAYER_BTN IN_SHIFT" in SRC and "#define UNI_EDIT_BTN IN_ALT" in SRC
      and _ev2.count("fire_held(UNI_LAYER_BTN)") >= 2 and "fire_held(UNI_EDIT_BTN)" in _ev2,
      "v0.31 the layer button (SHIFT) and the edit button (ALT) are one constant each")
_up = _ev2[_ev2.find("if (pad >= 0 && mode == MODE_UNI) {"):_ev2.find("if (pad >= 0 && fader_mode()) {")]
check(0 < _ev2.find("if (pad >= 0 && mode == MODE_UNI) {") < _ev2.find("if (pad >= 0 && fader_mode()) {") < _ev2.find("if (pad >= 0 && mode != MODE_STEP) {")
      and "int n, rel = uni_press(p, &tg, m, &n);" in _up and "midi_sendn(m, n);" in _up
      and "live[pad].cc = rel == UREL_CC; live[pad].off = p.off; live[pad].on = true;" in _up and "} else uni_held |= bit;" in _up,
      "v0.31 a UNIVERSAL pad plays by uni_press; a note / CC hold ends through live[] (its off value), the rest light while held")
check(re.search(r"uni_copy_src = pad;[^\n]*\n\s*\}\s*int ly = fire_held\(UNI_LAYER_BTN\)", _up) is not None
      and "if (src >= 0 && src != pad && fire_held(pad_of_input(src))) {" in _up and "uni_set(pg, uni_pad_key(pad, uni_ed_layer), e, !se)" in _up,
      "v0.31 ALT + tap picks the pad to edit AND plays it; ALT + a pad still held + another pad = copy (off copies as a clear)")
check("if (!down && idx == UNI_EDIT_BTN && mode == MODE_UNI) uni_edit_end();" in _ev2
      and "if (uni_ps[i].state == US_EDIT) { uni_ps[i].state = US_PEND; any = true; }" in code(body("uni_edit_end"))
      and "if (any) save_soon();" in code(body("uni_edit_end")),
      "v0.31 letting ALT go ends the edit: its changes are saved like any edit (3 s later, AUTOSAVE rules)")
_ku = _ev2[_ev2.find("if (uni_ed == 1) {"):_ev2.find("if (mode == MODE_DRUM && !mpcrem && fire_held(IN_ALT) && k < 2)")]
check("uni_edit_pad(&p, uni_ed_kpage, k, v, uni_ed_layer);" in _ku
      and re.search(r"if \(p\.type != UT_OFF\) uni_set\(pg, key, ne, false\);\s*else if \(e\) uni_set\(pg, key, ne, true\);", _ku) is not None,
      "v0.31 editing a pad: the knobs change it live; type off clears it")
check("if (uni_ed != 2) { uni_ed = 2; uni_ed_idx = (G.uni_bank & 3) * 4 + k; dirty = true; continue; }" in _ku
      and "uni_edit_knob(&kn, k, v);" in _ku
      and re.search(r"if \(kn\.type != UK_NONE\) uni_set\(pg, key, ne, false\);\s*else if \(e\) uni_set\(pg, key, ne, true\);", _ku) is not None and "midi_sendn(m, uni_knob_turn(kn, &uni_kv[pg][slot], v, m));" in _ku,
      "v0.31 ALT + turn: the first turn picks the knob (changes nothing), then the knobs edit it; plain turns play the bank's knob")
check("if (row >= 0 && mode == MODE_UNI) continue;" in _ev2 and _ev2.find("if (row >= 0 && mode == MODE_UNI) continue;") < _ev2.find("int m = ctl_row_press("),
      "v0.31 row buttons do nothing in UNIVERSAL (no mute you could not see)")
check("if (np >= 0 && np < UNI_PAGES) { if (uni_ed || uni_copy_src >= 0) uni_edit_end(); uni_latch_off(-1); G.uni_page = (uint8_t)np; touched_global(); }" in _ev2
      and "else if (mode == MODE_UNI && uni_ed == 1) uni_ed_kpage ^= 1;" in _ev2
      and "else if (mode == MODE_UNI) { G.uni_bank = (uint8_t)((G.uni_bank + 1) & 3); touched_global();" in _ev2,
      "v0.31 GRID = page (an edit ends first), KNOB MODE = bank (editing a pad: its knob page); moving around never starts a save")
check("if (m != MODE_UNI) { if (uni_ed || uni_copy_src >= 0 || uni_unsaved()) uni_edit_end(); uni_held = 0; uni_latch_off(-1); }" in code(body("set_mode")),
      "v0.31 leaving UNIVERSAL ends an edit, forgets held pads, and ends latched notes (review: they hung)")
_sp = code(body("saver_poll"))
check(_sp.find("if (uw.state) return;") == _sp.find("{") + 1 + _sp[_sp.find("{") + 1:].find("if (uw.state) return;")
      and _sp.find("if (uw.state) return;") < _sp.find("if (store2_busy()) {"),
      "v0.31 a host page write owns the flash writer: the saver waits")
check("IS_KIND_UNI(kind) ? store2_begin_src(kind, ulen, uni_save_reader, &UNI_ID[p])" in _sp
      and re.search(r"uni_slots_state\(p, US_PEND, US_SAVE\);\s*uni_cur_init\(&uni_wcur, b, bl, uni_ps, UNI_SLOTS, p, 1u << US_SAVE\);", _sp) is not None
      and "if (others + ulen <= UNI_BUDGET) { kind = KIND_UNI(p); break; }" in _sp,
      "v0.31 page edits are saved as the stored page + its edits, only if every page as stored still fits the budget")
check("static void uni_save_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { (void)ctx; uni_cur_read(&uni_wcur, off, dst, n); }" in _C
      and "uni_patched_read(" not in _C[_C.find("static void uni_save_reader("):_C.find("static void saver_poll(")],
      "review 1: the saver reads the page through ONE cursor set up at the start of the save (O(1) per word), never a walk per word")
_us = code(body("uni_size")) + code(body("uni_used_others")) + code(body("uni_set"))
check("uni_cached(page);" in code(body("uni_size")) and "uni_patched_len" not in _us and "uni_patched_sum" not in _us
      and "if (uni_cache[page].ok && uni_cache[page].ver == uni_pver[page]) return;" in code(body("uni_cached"))
      and "uni_cached(p);" in code(body("host_cmd")) and "uni_patched_sum" not in code(body("host_cmd")),
      "review 1: page sizes / sums are cached per page change: a knob detent or a host 14 never walks the pages")
check(re.search(r"if \(!del\) memcpy\(uni_ps\[s\]\.e, e, UNI_ENT\);\s*uni_touch\(page\);", code(body("uni_set"))) is not None
      and "uni_touch(page);" in code(body("uni_slots_state")) and "uni_touch(p);" in code(body("uni_poll"))
      and "for (int p = 0; p < UNI_PAGES; p++) uni_touch(p);" in code(body("compact_all")) and "uni_pver[page]++;" in _C,
      "review 1: every change to a page (an edit, a save, a host write, a clean-up) renews its cached numbers")
_ue = code(body("uni_entry"))
check("uni_index(r, len, uni_ix);" in _ue and "return uni_ix[key] == 255 ? 0 : r + UNI_HDR + UNI_ENT * (uint32_t)uni_ix[key];" in _ue
      and "uni_find(" not in _ue and "uni_find(" not in code(body("paint_uni")) and "uni_find(" not in code(body("uni_pad_get")),
      "review 2: every key (SHIFT too) is looked up by the index: the paint never scans a page per pad")
check(re.search(r"if \(p\.type == UT_LATCH\) \{[^}]*int nl = uni_latch_press\(uni_lat, uni_pad_key\(pad, ly\), p, m\);", _up) is not None
      and "if (page == G.uni_page && key < 2 * UNI_PADS) uni_latch_off(key >> 1);" in code(body("uni_set"))
      and code(body("uni_set")).find("uni_latch_off(key >> 1);") < code(body("uni_set")).find("uni_ps[s].page = (int8_t)page;")
      and "if (G.uni_page == p) uni_latch_off(-1);" in code(body("uni_poll")) and "uni_latch_off(-1);" in code(body("uni_drop_edits"))
      and "p.type == UT_LATCH && uni_latch_on(uni_lat, uni_pad_key(pad, ly))" in code(body("paint_uni"))
      and "int n = uni_latch_release(uni_lat, pad, m, sizeof m);" in code(body("uni_latch_off")) and "uni_latch_init(uni_lat);" in code(body("main")),
      "review 3: a latched note goes off with what it SENT: on its next press, before its pad changes, on a host write of its page, a page change, leaving")
_hb = _C[_C.find("static void uni_host_begin("):_C.find("static void uni_host_chunk(")]
_hl = _C[_C.find("static void uni_host_clear("):_C.find("static void uni_poll(")]
check(re.search(r"\{\s*if \(uni_host_busy\(\)\) \{ uni_ack\(0x10, UR_BUSY, page\); return; \}", _hb) is not None
      and 0 < _hb.find("uni_host_busy()") < _hb.find("store2_cancel();") < _hb.find("uw.done = 0;")
      and re.search(r"\{\s*if \(uni_host_busy\(\)\) \{ uni_ack\(0x15, UR_BUSY, page\); return; \}", _hl) is not None
      and 0 < _hl.find("uni_host_busy()") < _hl.find("uw.done = 0;")
      and "return uw.state == 2 || (uw.state == 0 && store2_busy());" in code(body("uni_host_busy")),
      "review 4: a busy begin / clear is answered BUSY before anything of the write under way is touched")
check("if (uw.state && uw.page == page && uw.len > s) s = uw.len;" in code(body("uni_size"))
      and "uni_drop_edits(udrop, \"edits dropped\");" in _sp and "if (udrop < 0) udrop = p;" in _sp
      and "uni_drop_edits(p, \"edits replaced\");" in code(body("uni_poll")),
      "review 5: a host write's size is reserved while it runs; edits that can never fit are dropped (said on screen), never stuck")
check("else if (!store2_busy() && IS_KIND_UNI(saving_kind)) { uni_slots_state(saving_kind - KIND_UNI(0), US_SAVE, US_FREE);" in _sp
      and "else if (IS_KIND_UNI(saving_kind)) uni_slots_state(saving_kind - KIND_UNI(0), US_SAVE, US_PEND);" in _sp
      and "else if (!store2_busy() && IS_KIND_PAT(saving_kind)) steps_saved[" in _sp and "else if (IS_KIND_PAT(saving_kind)) pat_dirty |=" in _sp,
      "v0.31 a saved page frees its edits, a failed one keeps them; the pattern branches never see a page kind (kinds 49-56)")
_ca = code(body("compact_all"))
check("int r = compact_run(&c, &kept);" in _ca and "&undo_buf[0][0], sizeof undo_buf, sizeof batch, batch, &paint_saved[0][0], paint_has," in _ca
      and 0 < _ca.find("if (r == CP_REFUSED) return false;") < _ca.find("if (kept) { undo_pat = -1; undo_len[0] = undo_len[1] = 0; undo_open = false; }")
      and "batch_flush();" in _ca and "store2_erase" not in _ca,
      "v0.31 the clean-up is compact.c's (tested on his real area): pages in the undo + batch buffers; refused = nothing touched, undo kept")
check("if (s >= 0 && uni_ps[s].state == US_SAVE) uni_save_cancel(page);" in code(body("uni_set"))
      and "if (!del && uni_size(page) + uni_used_others(page) > UNI_BUDGET) {" in code(body("uni_set"))
      and "store2_cancel(); saving_kind = -1; save_run = false;" in code(body("uni_save_cancel")),
      "v0.31 an edit to a page being saved cancels that save (it never counts); an edit that would overflow the budget is refused")
_hc2 = _C[_C.find("static void uni_host_commit("):_C.find("static void uni_host_clear(")]
check("r = !pl ? UR_FAIL : uni_sum(pl, uw.len) != sum ? UR_SUM : uni_check(pl, uw.len);" in _hc2
      and "if (r != UR_OK) { store2_cancel(); uw.state = 0; }" in _hc2 and "uw.state = 2;" in _hc2,
      "v0.31 a host page is checked IN FLASH (sum + every field) before its commit word; a bad one never counts")
_po = code(body("uni_poll"))
check("8u + (have == uw.len ? (have + 3u) / 4u : have / 4u)" in _po and "if (wd < target) store2_step(target - wd > 8 ? 8 : target - wd);" in _po
      and _po.find("if (!quiet) return;") < _po.find("store2_step(")
      and "if (uw.state == 1 && ms_ticks - uw.last_ms > 3000) { store2_cancel(); uw.state = 0; uni_touch(uw.page); return; }" in _po,
      "v0.31 a host write is programmed in quiet moments only, never past what has arrived, and dropped after 3 s of silence")
check("uni_drop_edits(p, \"edits replaced\");" in _po and "uni_values_reset(p);" in _po
      and "if (uni_ps[i].state != US_FREE && uni_ps[i].page == page) { uni_ps[i].state = US_FREE; any = true; }" in code(body("uni_drop_edits"))
      and "for (int p = 0; p < UNI_PAGES; p++) uni_values_reset(p);" in code(body("main")),
      "v0.31 knob values / toggles are not saved: at power-on and after the host rewrites a page they start fresh")
check("uint8_t uni_page, uni_bank;" in SRC and "sizeof(global_t) == 568" in SRC and "G.uni_page %= UNI_PAGES; G.uni_bank &= 3;" in code(body("main")),
      "v0.31 page + bank ride along in the settings record (appended; an older save = page 1, bank 1)")
check("uni_unsaved() ||" in code(body("unsaved")) and "|| uni_dirty()) && !save_due && !save_run) save_soon();" in code(body("commit_pending"))
      and "|| uni_dirty();" in code(body("menu_save_now")),
      "v0.31 page edits show the star, are saved at STOP and by SAVE NOW")
_ml = code(body("main"))
check("if (loop_last_us) { uint32_t dl = now - loop_last_us; if (dl > loop_max_us) loop_max_us = dl; }" in _ml
      and _ml.find("uint32_t now = us_now();") < _ml.find("loop_last_us = now;") < _ml.find("tud_task();")
      and "(uint8_t)(lmax >> 7 & 0x7F), (uint8_t)(lmax & 0x7F)," in code(body("host_cmd")) and "if (h.arg == 1) { loop_max_us = 0;" in code(body("host_cmd")),
      "v0.31 07 reports the longest main-loop pass (us), 07 01 starts it over: stalls are MEASURED on the chip")
_mt = code(body("midi_task"))
check("if ((pkt[0] & 0xF) == 0xB) fader_follow(pkt[1], pkt[2], pkt[3]);" in _mt
      and "for (uint8_t i = 0; i < n; i++) midi_in_byte(pkt[1 + i], now);" in _mt,
      "v0.31c a CC packet from the host goes to fader_follow; every byte still goes to midi_in_byte as before (clock / SysEx untouched)")
# ── v0.36: the DAW port (USB cable 1) ──
check("if (pkt[0] >> 4) { daw_in_packet(pkt); continue; }" in _mt
      and _mt.find("if (pkt[0] >> 4) { daw_in_packet(pkt); continue; }") < _mt.find("fader_follow(") < _mt.find("midi_in_byte("),
      "v0.36 a packet on cable 1 (the DAW port) goes to the DAW side BEFORE fader_follow / midi_in_byte: a DAW's clock, CCs and SysEx never reach the sequencer")
_ds = code(body("daw_send"))
check("tud_midi_packet_write(pkt);" in _ds and "stream_write" not in _ds and "DAW_CABLE << 4" in _ds
      and "(end ? 4 + k : 4)" in _ds,
      "v0.36 daw_send packs its own cable-1 packets (never stream_write: TinyUSB has one stream state per interface); SysEx CIN 4 / 5-7")
USBD = code(open(os.path.join(HERE, "usb_descriptors.c"), encoding="utf-8").read().replace("\r\n", "\n"))
check("#define MIDI_CABLES 2" in USBD and "TUD_MIDI_DESC_JACK_DESC(1, STR_JACK_SEQ)," in USBD and "TUD_MIDI_DESC_JACK_DESC(2, STR_JACK_DAW)," in USBD
      and "TUD_MIDI_JACKID_IN_EMB(1), TUD_MIDI_JACKID_IN_EMB(2)," in USBD and "TUD_MIDI_JACKID_OUT_EMB(1), TUD_MIDI_JACKID_OUT_EMB(2)," in USBD,
      "v0.36 the USB descriptor has two MIDI cables (seq + DAW), both endpoints list both jacks")
_ff = code(body("fader_follow"))
check("ctl_fader_follow(G.fad_val, G.fad_ch, G.fad_cc, G.fad_n, st, num, val)" in _ff
      and "if (m && mode == MODE_FADER && (m >> fad_top & ((1u << VIEW_ROWS) - 1u))) dirty = true;" in _ff,
      "v0.31c a host CC sets the faders by ctl_fader_follow (tested) and repaints only when a changed fader is on screen")
check(_ff != "" and not re.search(r"midi_send|fader_send|batch_add|touched_|edited_|save_|global_dirty", _ff),
      "v0.31c following the host sends nothing back and starts / marks no save")
# ── v0.31d: the DAW page = a second fader set the host defines, run by the SAME fader code ──
_fs = code(body("fset"))
check("fset_t s = {G.fad_val, G.fad_n, &fad_top, false};" in _fs
      and "if (mode == MODE_DAW) { s.val = daw.val; s.n = daw.n; s.top = &daw_top; s.daw = true; }" in _fs,
      "v0.31d fset(): the saved FADERS set unless the DAW page is on screen")
_pf = code(body("paint_faders"))
check("fset_t s = fset();" in _pf and "const uint8_t *rgb = s.daw ? daw.rgb[f] : FAD_RGB[G.fad_col[f] & 15];" in _pf
      and "bool add = !s.daw && f == s.n;" in _pf and "else if (add && c == 0) R = Gn = B = 1;" in _pf,
      "v0.31d the pads paint the set on screen: the DAW page in the host's colours, no + row there")
check("else if (fader_mode()) paint_faders();" in code(body("paint")),
      "v0.31d the DAW page paints through paint_faders (the same bright / tip / dark rule)")
_fset = code(body("fader_set"))
_dsf = code(body("daw_set_fine"))
check("if (s.daw) { daw_set_fine(f, ctl_daw_tap_fine(v)); return; }" in _fset and "if (!ctl_fset_set(s.val, f, v)) return;" in _fset
      and "fader_send(f); touched_global();" in _fset
      and "if (!ctl_daw_level(&daw, f, fine)) return;" in _dsf and "midi_sendn(m, ctl_daw_level_msg(m, f, daw.fine[f]));" in _dsf
      and not re.search(r"touched_|edited_|save_|global_dirty|fader_send", _dsf),
      "v0.31f a level moved on the Fire: FADERS sends its CC + arms the lazy save; the DAW page sends its 14-bit 1D and saves nothing")
_dk = code(body("daw_knob"))
check("int step = ctl_daw_knob_step(now - last[k & 3]);" in _dk and "daw_set_fine(row, daw.fine[row] + v * step);" in _dk,
      "v0.31f a DAW knob moves the 14-bit level by the accelerated step")
_sf = code(body("scroll_faders"))
check("if (s.daw && !dawm) { if (d) daw_page_bank(d > 0 ? 1 : -1, d < 0 ? -d : d); return; }" in _sf
      and "int edge = ctl_fset_scroll(s.top, d, fad_max_top());" in _sf
      and "midi_sendn(m, ctl_daw_bank_msg(m, dir, count));" in code(body("daw_page_bank"))
      and "return ctl_fset_max_top(s.n, !s.daw, MAXFADERS, VIEW_ROWS);" in code(body("fad_max_top")),
      "v0.31f the DAW page never scrolls itself: every SELECT turn sends 1E <dir> <detents>; FADERS scrolls exactly as before")
check("if (pad >= 0 && fader_mode()) {" in _ev2 and "int f = *s.top + pad / 16;" in _ev2 and "if (f < s.n) fader_set(f, ((pad % 16) * 127 + 7) / 15);" in _ev2,
      "v0.31d a tap on a column sets that row of the set on screen")
_kf = _ev2[_ev2.find("if (fader_mode()) {\n            fset_t s = fset();"):_ev2.find("if (mode == MODE_UNI) {                     ")]
check("if (!s.daw && held_row >= 0) {" in _kf and "if (s.daw) daw_knob(k, *s.top + k, v);" in _kf
      and "else fader_set(*s.top + k, s.val[*s.top + k] + v);" in _kf,
      "v0.31d knobs 1-4 fine-tune the visible rows of either set; the held-row set-up is FADERS only")
check(re.search(r"\} else if \(fader_mode\(\)\) \{\s*scroll_faders\(d\);", _ev2) is not None
      and "if (fader_mode()) { scroll_faders(d); return; }" in code(body("scroll_tracks")),
      "v0.31d SELECT turn and SHIFT + PATTERN scroll the DAW page too")
check(re.search(r"if \(row >= 0 && mode == MODE_DAW\) \{\s*if \(!down && held_row == row\) held_row = -1;\s*continue;\s*\}", _ev2) is not None
      and _ev2.find("if (row >= 0 && mode == MODE_DAW) {") < _ev2.find("if (row >= 0 && mode == MODE_FADER) {"),
      "v0.31d row buttons never add / remove / set up anything on the DAW page (the host owns its rows)")
_hc5 = code(body("host_cmd"))
check(re.search(r"case HC_DAWROW:\s*if \(dawm\) ctl_daw_row\(&pi_page, h\.arg, h\.data, h\.len\);\s*"
                r"else \{ ctl_daw_row\(&daw, h\.arg, h\.data, h\.len\); daw_dirty\(h\.arg\); \}\s*break;", _hc5) is not None
      and re.search(r"case HC_DAWLVL:\s*if \(dawm\) ctl_daw_level\(&pi_page, h\.arg, \(int\)h\.len\);\s*"
                    r"else if \(ctl_daw_level\(&daw, h\.arg, \(int\)h\.len\)\) daw_dirty\(h\.arg\);\s*break;", _hc5) is not None
      and "if (dawm) { ctl_daw_count(&pi_page, h.arg); break; }" in _hc5[_hc5.find("case HC_DAWN:"):_hc5.find("case HC_DAWROW:")],
      "v0.31d 1B / 1C store the row (ctl.c, tested) and only ask for a repaint; v0.37c in DAW mode 1A-1C go to the Pi's page kept aside")
_hd = _hc5[_hc5.find("case HC_DAWN:"):_hc5.find("case HC_DAWROW:")]
check(_hd != "" and not re.search(r"midi_send|fader_send|batch_add|touched_|edited_|save_|global_dirty|daw_bank|fader_set", _hd + _hc5[_hc5.find("case HC_DAWROW:"):_hc5.find("default: break;")]),
      "v0.31d a host DAW message sends nothing back (no echo) and starts / marks no save (RAM only)")
check("ctl_daw_count(&daw, h.arg);" in _hd and re.search(r"if \(!daw\.n\) \{\s*set_mode\(mode_before_faders\);", _hd) is not None
      and "daw_top = ctl_fset_max_top(daw.n, 0, CTL_DAW_ROWS, VIEW_ROWS);" in _hd,
      "v0.31d 1A 0 while the DAW page shows: back to where you were ; the view re-clamps")
check("if (mode == MODE_DAW && row >= daw_top && row < daw_top + VIEW_ROWS) dirty = true;" in code(body("daw_dirty")),
      "v0.31d a host change repaints only when the DAW page is on screen and the row is visible")
check("daw" not in code(body("fader_follow")) and "MODE_DAW" not in code(body("fader_follow")),
      "v0.31d faders-follow-CCs (v0.31c) still only touches the saved FADERS set")
check("menu_daw_now" not in code(SRC) and '"Pi DAW page"' not in SRC and '"DAW page"' not in SRC
      and "MODE_FADER, MODE_DAW, daw.n > 0);" in code(SRC),
      "v0.36b no BROWSER > DAW page: PERFORM's (v0.38) second tap is the one way to the Pi's DAW page (only once the Pi defined one)")
_gc = code(SRC); _ge = _gc.find("} global_t;"); _gs = _gc.rfind("typedef struct", 0, _ge)
check(re.search(r"^static ctl_daw_t daw;", SRC, re.M) is not None and "sizeof(global_t) == 568" in SRC and 0 <= _gs < _ge and "daw" not in _gc[_gs:_ge].replace("daw_proto", ""),
      "v0.31d the DAW page lives in RAM only: not in the saved settings (global_t unchanged)")
# ---- v0.31r remote input (remote.c rules, remote_test): the host's presses / turns take the SAME path as real ones ----
_fe = code(body("fire_event", HW))
check("if (evq_r == evq_w) return rin_event(&rin);" in _fe, "v0.31r fire_event: the host's events come after the real queue is empty")
check("|| rin_held(&rin, idx)" in code(body("fire_held", HW)), "v0.31r fire_held: held by the host counts as held (SHIFT / ALT combos)")
check("return v + rin_turn_take(&rin, RIN_SELECT);" in code(body("fire_encoder_take", HW)), "v0.31r SELECT: the host's detents are added")
check("return v + rin_turn_take(&rin, k);" in code(body("fire_knob_take", HW)), "v0.31r knobs: the host's detents are added")
check("rin" not in code(body("tim4_body", HW)) and "rin" not in code(body("encoder_step", HW)) and "rin" not in code(body("ev_push", HW)),
      "v0.31r TIM4 (the crash suspect) is untouched: nothing of the remote input runs in the interrupt")
_he = code(body("handle_events"))
check(0 <= _he.find("rin_tick(&rin, ms_ticks);") < _he.find("fire_event()"),
      "v0.31r the quiet timeout runs before the events, so its releases are handled in the same pass")
_hc = code(body("host_cmd"))
_hi = _hc[_hc.find("case HC_INPUT:"):]
check("case HC_INPUT:" in _hc and "rin_cmd(&rin, h.arg, h.data, h.len, ms_ticks)" in _hi[:400]
      and "0x20, (uint8_t)(h.arg & 0x7F), (uint8_t)res, rin.armed, 0xF7" in _hi[:400],
      "v0.31r host 20 goes to rin_cmd and is answered 20 <sub> <result> <armed>")
check(re.search(r"if \(held_t >= 0 && ctl_step_displace\(held_pad, held_pad_was_lit, held_pad_turned\)\) \{\s*steps_set\(CURPAT, held_t, held_s, 0, 0\);.*?"
                r"edited_pattern\(CURPAT\);\s*\}\s*held_t = t; held_s = s; held_pad = pad;", code(SRC), re.S) is not None,
      "2026-11-11 a step pad pressed while another LIT, untouched one is still down turns that one off first (only one hold is remembered)")
# ---- v0.33: 22 LED levels (ledpat.h, ledpat_test) with TIM4's timing untouched; ORC mode = host mode (hostm.c, hostm_test) ----
HWH = open(os.path.join(HERE, "fire_hw.h"), encoding="utf-8").read().replace("\r\n", "\n")
_t4 = code(body("tim4_body", HW))
check(0 < _t4.find("uint32_t lit = led_lit[frame < LED_FRAMES ? frame : 0];") < _t4.find("for (int b = 0; b < CHAIN; b++) out[b] = (uint8_t)~ledpat_byte(&led_level[b * 8], lit);")
      < _t4.find("frame = (uint8_t)((frame + 1) % LED_FRAMES);") < _t4.find("latch(0);") and "ledpat_bits" not in _t4 and "ledpat_lit" not in _t4 and _t4.count("led_lit[") == 1,
      "v0.33 / v0.34d TIM4 lights an LED from its level's frame pattern (one word of a RAM table per pass, bounded index), 21 frames a cycle; no pattern is computed in the interrupt")
_hi = code(body("fire_hw_init", HW))
check(0 < _hi.find("for (int f = 0; f < LED_FRAMES; f++) led_lit[f] = ledpat_lit(f, LED_FRAMES);") < _hi.find("TIM4->CR1 = TIM_CR1_CEN;")
      and "TIM4->PSC = 83;" in _hi and "TIM4->ARR = 499;" in _hi and "DMA" not in code(HW),
      "v0.33 the pattern table is filled before TIM4 starts, and TIM4 is still one interrupt every 500 us with no DMA (the 2026-10-13 driver's changes are NOT here)")
check("ledpat_fine(level);" in code(body("led_set", HWH)),
      "v0.33 every old 0-7 level is stored as its fine level (led_set)")
_mn = code(body("main"))
check("if (hostm) hostm_events(); else if (dawm) dawm_events(); else if (paintm) paintm_events(); else handle_events(now);" in _mn,
      "v0.33 in ORC mode the presses go to hostm_events, never to the sequencer's controls")
check("if (dirty && !hostm) paint();" in _mn and "if (hostm) { if (hostm_flush && oled_flush()) hostm_flush = false; }" in _mn
      and 0 < _mn.find("if (hostm) { if (hostm_flush") < _mn.find("render();"),
      "v0.33 in ORC mode the Fire neither paints the pads nor renders its screen: the ORC's bitmap is flushed as it arrived")
_mb = code(body("midi_in_byte"))
check(0 < _mb.find("if (b == 0xFC) { stop(false); return; }") < _mb.find("if (hostm && (hm_byte(b) & HM_OLED_DONE)) hostm_flush = true;") < _mb.find("if (b == 0xF0) { sx_on = true; sx_len = 0; }"),
      "v0.33 Akai-format colours / bitmaps reach hm_byte only in ORC mode, after the clock bytes (the sequencer still follows the clock)")
_he2 = code(body("hostm_events"))
check(0 < _he2.find('if (hm_exit(e, fire_held(IN_SHIFT))) { hostm_leave("ORC off"); return; }') < _he2.find("if (hm_input(e, m)) hostm_send(0x22, m[0], m[1]);")
      and "hostm_send(0x23, 4, hm_rel(d));" in _he2 and "hostm_send(0x23, (uint8_t)k, hm_rel(d));" in _he2,
      "v0.33 SHIFT + BROWSER leaves before anything is sent; pads / buttons go as 22, SELECT as 23 4, knobs as 23 0-3")
check('if (hm_quiet(ms_ticks, hostm_seen)) { hostm_leave("ORC gone"); return; }' in _he2,
      "v0.33 a host that stops saying 'still here' cannot strand the Fire in ORC mode")
_hm = case_text("HC_HOSTM")
check("if (h.arg == 1 && !hostm) { hostm_enter(); break; }" in _hm and 'if (h.arg == 0 && hostm) { hostm_leave("ORC off"); break; }' in _hm
      and "if (hostm) hostm_seen = ms_ticks;" in _hm and "hostm_say();" in _hm,
      "v0.33 host 21: on / off / still here, each answered with the state")
_en = code(body("hostm_enter"))
_lv = code(body("hostm_leave"))
check("live_release(pad);" in _en and "if (roll_pads >> pad & 1) roll_release(pad);" in _en and "uni_latch_off(-1);" in _en
      and 0 < _en.find("hostm = true; hostm_seen = ms_ticks;") < _en.find("leds_clear();") < _en.find("hm_buttons();") < _en.rfind("hostm_say();")
      and "if (!tud_midi_mounted())" in _en and "hm_reset();" in _en,
      "v0.33 entering ORC mode lets go of every held note / roll / latch first, starts the quiet timer, clears the pads and tells the host")
check("hostm = false;" in _lv and "leds_clear();" in _lv and "dirty = true;" in _lv and "hostm_say();" in _lv
      and "led_set" not in code(body("play_step")) and "draw_" not in code(body("play_step")),
      "v0.33 leaving repaints the sequencer and tells the host; play_step touches no light and no screen (so it can run under ORC mode)")
check("for (int i = 0; i < IN_COUNT; i++) if (fire_held(i)) hostm_eat[i >> 3] |= (uint8_t)(1u << (i & 7));" in _en
      and 0 < _he2.find("hostm_eat[ei >> 3] &= (uint8_t)~(1u << (ei & 7));") < _he2.find("if (!(e & 0x80)) continue;") < _he2.find("if (hm_input(e, m))"),
      "v0.33 an input already down when ORC mode begins (SELECT on the menu row) never sends the ORC a release it saw no press for")
check('{"ORC mode",    0, MK_ACT,    0, 0, 0, menu_host_now}' in SRC and "hostm_enter();" in code(body("menu_host_now"))
,
      "v0.33 BROWSER > ORC mode is the host-mode toggle; PI VU cannot take the pads while it is on")
# ---- v0.33b stability (the 2026-11-11 crashes = TIM4's entry instruction fetched with one bit wrong) ----
check("FLASH->ACR = FLASH_ACR_LATENCY_3WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;" in code(body("clock_init"))
      and "LATENCY_2WS" not in code(SRC),
      "v0.33b the flash is read with 3 wait states (2 has no margin at 84 MHz)")
_t4 = code(body("tim4_body", HW))
check(0 < _t4.find("bool entry_ok = crash_t4_entry_ok(sp_in, t4_sp0);") < _t4.find("if (entry_ok) {") < _t4.find("const volatile uint32_t *f = (const volatile uint32_t *)sp_in;")
      < _t4.find("} else {") < _t4.find("t4_in = t4_sp0;") < _t4.find("t4_entry_note(sp_in, exc);") < _t4.find("canary_watch_arm("),
      "v0.33b TIM4 looks at the entry address before it reads through it: an impossible one is noted, never dereferenced, never a reset")
check(re.search(r"if \(entry_ok\) \{\s*uint32_t seen = \*\(const volatile uint32_t \*\)\(t4_in - 4\), want = t4_exc;\s*"
                r"if \(seen != want\) t4_recheck\(seen, want, exc, sp_read\(\)\);\s*\}\s*\}\s*$", _t4) is not None,
      "v0.33b the return-slot check (found from the entry address) is skipped for a pass whose entry address was impossible")
_en2 = code(body("t4_entry_note", HW))
check("t4_note.count++;" in _en2 and "t4_note.kind = T4V_ENTRY;" in _en2 and "t4_note.seen = sp_in; t4_note.want = t4_sp0;" in _en2
      and "tim4_slip" not in _en2 and "T4_NOTE_MAGIC" in _en2,
      "v0.33b the entry misread is counted in the TIM4 note (kind 4, what it was handed, its own SP): that count is the misread counter")
_kp = code(body("fire_knobs_poll", HW))
check(0 < _kp.find("knob_init = true;") < _kp.find("ADC1->SQR3 = 17;") < _kp.find("if (r > vdd_stat.hi) vdd_stat.hi = r;")
      and "ADC1->SQR3 = 0;" in code(body("adc_read0", HW)) and "ADC_CCR_TSVREFE" in code(body("fire_hw_init", HW))
      and "ADC1->SMPR1 = 7u << 21;" in code(body("fire_hw_init", HW)) and "vdd" not in _t4,
      "v0.33b the supply is read (reference channel 17) after the knobs, in the main loop, never in TIM4; the knob read sets its own channel")
check("if (h.arg == 1) { loop_max_us = 0; fire_vdd_reset(); }" in code(body("host_cmd")), "v0.33b 07 01 also restarts the supply's low / high marks")
# ---- v0.34: a second, SHORT LED slot per chain pass (about 200 brightness steps); the pass and the scan are unchanged ----
_t4 = code(body("tim4_body", HW))
_l1 = _t4.find("latch(0);")
_x1 = _t4.find("*(volatile uint8_t *)&SPI2->DR = out[i];")
_r1 = _t4.find("in[i] = *(volatile uint8_t *)&SPI2->DR;")
_l2 = _t4.find("latch(0);", _l1 + 1)
_g = _t4.find("t4_gap_us = (uint8_t)(g > 255 ? 255 : g);")
_x2 = _t4.find("*(volatile uint8_t *)&SPI2->DR = out2[i];")
check(0 < _t4.find("uint32_t c0 = TIM4->CNT;") < _l1 < _x1 < _r1 < _l2 < _g < _x2 < _t4.find("SLIP_CHECK(3);")
      and _t4.count("latch(0);") == 2 and _t4.count("latch(1);") == 2,
      "v0.34 one pass = latch, shift the long frame in (the buttons come out), latch, shift the short frame in: two latches, in that order")
_s3 = _t4.find("SLIP_CHECK(3);")
check(_t4.count("in[i] = ") == 1 and _t4.count("uint8_t v = in[i];") == 1
      and "SPI_SR_RXNE" not in _t4[_x2:_s3] and _t4.count("while (!(SPI2->SR & SPI_SR_RXNE)) {}") == 1
      and re.search(r"SPI2->DR = out2\[i\];\s*\}\s*while \(!\(SPI2->SR & SPI_SR_TXE\)\) \{\}\s*while \(SPI2->SR & SPI_SR_BSY\) \{\}\s*"
                    r"\(void\)\*\(volatile uint8_t \*\)&SPI2->DR; \(void\)SPI2->SR;", _t4[_x2:_s3]) is not None,
      "v0.34 / v0.34e the buttons are read ONCE a pass, from the first shift (byte by byte, as before); the second shift goes out back to back, "
      "is waited out (buffer empty, then not busy), and its bytes are thrown away with the overrun flag cleared")
check(_x1 < _t4.find("if (dual) out2[i] = (uint8_t)~ledpat_byte(&led_fine[i * 8], lit);") < _t4.find("while (!(SPI2->SR & SPI_SR_RXNE)) {}", _x1) < _r1
      and _t4.count("out2[i] = ") == 1 and "TIM4->ARR" not in _t4 and "TIM4->PSC" not in _t4 and "DMA" not in code(HW),
      "v0.34 / v0.34d the short frame comes from led_fine through the same pattern word, built (only in ORC mode) while the long frame's byte is on the wire; TIM4 never touches its own period, and there is still no DMA")
check("led_level[idx] = led_fine[idx] = ledpat_fine(level);" in code(body("led_set", HWH))
      and "memset((void *)led_fine, 0, sizeof led_fine);" in code(body("leds_clear", HW)),
      "v0.34 every old 0-7 level lights BOTH slots alike (led_set), so the sequencer's lights keep their exact on-time; clearing clears both")
_fw = case_text("HC_FINEW")
check("if (h.arg) hm_set_weight(h.arg);" in _fw and "(uint8_t)hm_weight(), (uint8_t)(t4_gap_us & 0x7F), (uint8_t)(t4_len_us > 127 ? 127 : t4_len_us), 0xF7" in _fw
      and "hm_init(fb, led_level, led_fine, LED_FRAMES);" in code(body("main"))
      and "t4_len_us = (uint8_t)(l > 255 ? 255 : l);" in _t4
      and "if (t4_gap_us) { int w = (int)((t4_gap_us * 256u + 250u) / 500u); if (w != hm_weight()) hm_set_weight(w); }" in code(body("hostm_events")),
      "v0.34 host 25: the short slot's share can be set, and the answer carries the slot's MEASURED length and the interrupt's; ORC mode keeps the share on that measurement")
# ---- v0.34b: the short slot only while ORC mode needs it (v0.34 measured 208 us of every 500 in TIM4 with both always on) ----
_t4 = code(body("tim4_body", HW))
_d = _t4.find("if (dual) {")
check(0 < _t4.find("uint8_t dual = led_dual;") < _t4.find("in[i] = *(volatile uint8_t *)&SPI2->DR;") < _d < _t4.find("latch(0);", _t4.find("latch(0);") + 1)
      < _t4.find("*(volatile uint8_t *)&SPI2->DR = out2[i];") < _t4.find("SLIP_CHECK(3);")
      and _t4.count("led_dual") == 1,
      "v0.34b TIM4 reads led_dual once a pass; the second latch + shift happen only when it is set (one frame = the v0.33 pass)")
check("led_dual = 1;" in code(body("hostm_enter")) and "led_dual = 0;" in code(body("hostm_leave"))
      and code(SRC).count("led_dual = 1;") == 2 and "led_dual = 1;" in code(body("paintm_enter")) and "led_dual" not in code(body("handle_events")),
      "v0.34b only ORC mode (and v0.37 PAINT mode) switches the short slot on, and leaving switches it off: the sequencer runs the short pass")
check("_Static_assert(LED_COUNT == 232," in HWH and "#define CHAIN 29" in HW,
      "v0.34b the frame is built 8 LEDs per chain byte: 232 LEDs = 8 x 29, asserted")
_pt = code(body("paint"))
check("led_set(LED_CHANNEL, kb == 0 ? 5 : 0); led_set(LED_MIXER, kb == 1 ? 5 : 0);" in _pt and "led_set(LED_USER1, kb == 2 ? 5 : 0); led_set(LED_USER2, kb == 3 ? 5 : 0);" in _pt
      and "led_set(LED_CHANNEL, knob_page == 0 ? 5 : 0); led_set(LED_MIXER, knob_page == 1 ? 5 : 0);" in _pt
      and "led_set(LED_CHANNEL, uni_ed_kpage == 0 ? 5 : 0); led_set(LED_MIXER, uni_ed_kpage == 1 ? 5 : 0);" in _pt
      and not re.search(r"led_set\(LED_(CHANNEL|MIXER|USER1|USER2), [^;]*: 1\)", _pt)
      and "led_set(LED_NOTE_Y, mode == MODE_NOTE ? 7 : mode == MODE_UNI ? (((ms_ticks / 300) & 1) ? 7 : 2) : 1);" in _pt and "btn_set" not in code(SRC),
      "v0.34c of the four lights beside KNOB MODE only the selected one is lit (the rest OFF, not dim); the mode buttons keep their dim idle light")
# ---- v0.36 DAW mode (daw.c = the MCU / HUI translator, tested by daw_test.c) ----
_dp = code(body("daw_in_packet"))
_dv = code(body("dawm_event"))
check("if (!dawm) return;" not in _dp and "if (daw_byte(pkt[1 + i], &ev)) dawm_event(&ev);" in _dp
      and 0 <= _dv.find("dawm_lvl[ev->idx] = ev->val;") < _dv.find("if (!dawm) return;") < _dv.find("switch (ev->type)")
      and "daw_init(G.daw_proto % 3);" in code(body("main"))
      and "if (m->v == &G.daw_proto && *m->v != was) {" in code(body("menu_turn")) and "uint8_t was = *m->v;" in code(body("menu_turn")),
      "v0.36b the DAW port is decoded from boot, DAW mode or not (levels kept); the rows are only touched in DAW mode; v0.38b the protocol menu restarts the translator only on a real change")
check("if (daw_proto() != dawm_proto_seen) dawm_relock();" in _dp and _dp.find("dawm_event(&ev);") < _dp.find("dawm_relock();")
      and "dawm_rows(daw_proto() == DAW_HUI ? 8 : 9);" in code(body("dawm_relock")) and "case DE_LOCK:" not in _dv,
      "v0.38b the rows follow ANY lock (a ping or plain traffic too, not only a SysEx): HUI never keeps MCU's master row")
check("void daw_out(const uint8_t *m, int n) { daw_send(m, n); }" in code(SRC),
      "v0.36 daw.c's only way out is daw_send (cable 1 packets, never the sequencer's port)")
_sf2 = code(body("daw_set_fine"))
check(0 < _sf2.find("if (!ctl_daw_level(&daw, f, fine)) return;") < _sf2.find("if (dawm) { daw_fader(f, daw.fine[f], ms_ticks); dirty = true; return; }") < _sf2.find("ctl_daw_level_msg("),
      "v0.36 in DAW mode a row's new level goes to the DAW (touch + level), never as 1D to the Pi; unchanged = nothing sent")
check("if (s.daw && edge) daw_channel(edge);" in code(body("scroll_faders")),
      "v0.36 in DAW mode SELECT scrolls the rows and, past the first / last, moves the DAW one track")
_de = code(body("dawm_events"))
check(_de.find("if (hm_exit(e, fire_held(IN_SHIFT))) { dawm_leave(); return; }") >= 0
      and "if (dawm_view) dawm_press(idx, GRP[pad / 16] + (pad % 16) / 2);" in _de
      and "if (f < daw.n) fader_set(f, ((pad % 16) * 127 + 7) / 15);" in _de
      and "case IN_PLAY:   dawm_press(idx, DB_PLAY); break;" in _de and "case IN_STOP:   dawm_press(idx, DB_STOP); break;" in _de
      and "daw_tick(ms_ticks);" in _de and "midi_send" not in _de and "batch_add" not in _de,
      "v0.36 DAW mode's presses: SHIFT + BROWSER leaves; pads = buttons or fader taps; transport to the DAW; never a sequencer note")
check("daw_button(" not in _de and 0 < _de.find("if (!down) {") < _de.find("dawm_release(idx);") < _de.find("if (pad >= 0) {")
      and "if (daw_button(id, 1)) dawm_held[idx] = (uint8_t)(id + 1);" in code(SRC)
      and "if (dawm_held[idx]) { daw_button(dawm_held[idx] - 1, 0); dawm_held[idx] = 0; }" in code(SRC)
      and 0 <= code(body("dawm_leave")).find("for (int i = 0; i < IN_COUNT; i++) dawm_release(i);") < code(body("dawm_leave")).find("dawm = false;"),
      "v0.38b every DAW button's release goes to what its press sent (after a view change, a scroll, or leaving DAW mode)")
_dl = code(body("dawm_leave"))
check("daw_release_all();" in _dl and _dl.find("daw_release_all();") < _dl.find("dawm = false;") and "set_mode(mode_before_faders);" in _dl,
      "v0.36 leaving DAW mode lets every touched fader go first, then gives the Fire back")
_dn = code(body("dawm_enter"))
check("if (dawm || hostm || paintm) return;" in _dn and "live_release(pad);" in _dn and "daw_init(" not in _dn
      and 0 < _dn.find("dawm_names();") < _dn.find("ctl_daw_level(&daw, i, dawm_lvl[i]);") < _dn.find("dawm = true;") < _dn.find("daw_announce();"),
      "v0.36b DAW mode: never over ORC mode or PAINT; lets go of held notes; KEEPS what the DAW said (names, levels, lights), then introduces itself")
check(0 <= _dn.find("pi_page = daw; pi_top = daw_top;") < _dn.find("dawm_rows(")
      and 0 < _dl.find("dawm = false;") < _dl.find("daw = pi_page;") < _dl.find("set_mode(mode_before_faders);")
      and "ctl_daw_count(&daw, 0);" not in _dl and re.search(r"daw_top = pi_top > ctl_fset_max_top\(daw\.n", _dl) is not None,
      "v0.37c the Pi's DAW page survives DAW mode: set aside before DAW mode's rows, put back (scroll clamped) when leaving")
_bt = code(body("status_brief_tick"))
check('status_brief("DAW off");' in _dl and 'cat(status_line, "DAW off")' not in SRC
      and 'status_brief("paint off");' in code(body("paintm_leave")) and 'cat(status_line, "paint off")' not in SRC
      and "status_brief_tick();" in _mn and "if (!strcmp(status_line, brief_msg))" not in _bt
      and "if (brief_msg && !strcmp(status_line, brief_msg)) { status_line[0] = 0; dirty = true; }" in _bt
      and "(int32_t)(ms_ticks - brief_until) < 0) return;" in _bt,
      "v0.37d 'DAW off' / 'paint off' clear themselves after BRIEF_MS, and only if no other message replaced them")
check('{"MCU/HUI/DAW", 0, MK_ACT,    0, 0, 0, menu_dawm_now},' in SRC and '{"DAW protocol",0, MK_ENUM,   &G.daw_proto, 3, NM_DAWP, 0},' in SRC
      and "daw.c" in open(os.path.join(HERE, "Makefile")).read(),
      "v0.36 BROWSER > MCU/HUI/DAW / DAW protocol (auto, MCU, HUI; saved); daw.c is in the build")

# ---- v0.37 PAINT mode (paint.c = the rules, tested by paint_test.c) ----
_pa = code(body("paint"))
check(_pa.strip().split("\n")[2].strip() == "if (paintm) { pm_paint(); return; }",
      "v0.37 in PAINT mode paint() draws only the canvas / palette (nothing of the sequencer's lights)")
_re = code(body("render"))
check(0 < _re.find("if (paintm) { pm_screen(); return; }") < _re.find("pop_kind"),
      "v0.37 in PAINT mode the screen is PAINT mode's")
_pe = code(body("paintm_events"))
check(_pe.find("if (hm_exit(e, fire_held(IN_SHIFT))) { paintm_leave(); return; }") >= 0
      and "if (!down) continue;" in _pe
      and "if (fire_held(IN_SHIFT)) paint_cur = (uint8_t)paint_swatch(pad);" in _pe
      and "else paint_tap(paint_cv, pad, paint_cur);" in _pe
      and "midi_send" not in _pe and "batch_add" not in _pe,
      "v0.37 PAINT mode's pads: SHIFT + BROWSER leaves; SHIFT + pad picks, a pad paints / wipes (on the press); never a note")
check("if (idx == IN_ROWBTN[i]) r = i;" in _pe and "int a = paint_btn(&paint_b, r, down, ms_ticks, &s);" in _pe
      and "if (a == PB_LOAD) pm_load(s); else if (a == PB_SAVE) pm_save(s);" in _pe
      and "if (paint_btn_tick(&paint_b, ms_ticks, &s) == PB_SAVE) pm_save(s);" in _pe,
      "v0.37 the MUTE row buttons: tap = load, hold = save (also while still held)")
check("if (idx == IN_STOP && down && fire_held(IN_SHIFT)) { memset(paint_cv, 0, sizeof paint_cv);" in _pe,
      "v0.37 SHIFT + STOP clears the canvas")
check("if (t4_gap_us) { int w = (int)((t4_gap_us * 256u + 250u) / 500u); if (w != hm_weight()) { hm_set_weight(w); dirty = true; } }" in _pe,
      "v0.37 PAINT mode follows the measured short LED slot, as ORC mode")
_ps = code(body("pm_save"))
check(0 < _ps.find("store2_cancel(); saving_kind = -1;") < _ps.find("memcpy(paint_saved[s], paint_cv, PAINT_LEN);")
      and "paint_pend |= (uint8_t)(1u << s);" in _ps and "paint_has |= (uint8_t)(1u << s);" in _ps and "if (!save_run) { saver_start(); save_paint_only = G.autosave_off && !save_forced_run; }" in _ps,
      "v0.37 a save: a half-written copy of that slot is cancelled BEFORE its bytes change; then marked and the saver started")
_sp = code(body("saver_poll"))
check("save_paint_only = false;" in code(body("saver_start")) and "if (save_paint_only) p = PATTERNS;" in _sp
      and _sp.find("if (save_paint_only) p = PATTERNS;") < _sp.find("else if (global_dirty) kind = S2_KIND_GLOBAL;")
      and "if (kind < 0 && !save_paint_only)" in _sp
      and "if ((paint_pend || crash_pend) && !save_run) { saver_start(); save_paint_only = true; }" in code(body("main")),
      "v0.38b with AUTOSAVE off a painting saved by hand writes paintings only (never the sequencer's unsaved edits); a failed one retries alone")
_sv = code(body("saver_poll"))
check("for (p = 0; p < PAINT_SLOTS; p++) if (paint_pend >> p & 1) { kind = KIND_PAINT(p); break; }" in _sv
      and ": IS_KIND_PAINT(kind) ? store2_begin(kind, paint_saved[p], PAINT_LEN)" in _sv
      and "else if (IS_KIND_PAINT(kind)) paint_pend &= (uint8_t)~(1u << p);" in _sv
      and "else if (IS_KIND_PAINT(saving_kind)) paint_pend |= (uint8_t)(1u << (saving_kind - KIND_PAINT(0)));" in _sv,
      "v0.37 the saver writes a pending painting from its RAM copy; a failed write marks it pending again")
_ca = code(body("compact_all"))
_cp = code(open(os.path.join(HERE, "compact.c"), encoding="utf-8").read().replace("\r\n", "\n"))
check("batch, &paint_saved[0][0], paint_has," in _ca and "paint_pend = 0;" in _ca
      and "if (c->paint_has >> s & 1) ok = store2_append(CP_KIND_PAINT + s, c->paint + s * CP_PAINT_LEN, CP_PAINT_LEN);" in _cp,
      "v0.37 the save area's clean-up keeps the saved paintings (written from RAM)")
_ls = code(body("load_state"))
check("const uint8_t *r = store2_ptr(KIND_PAINT(ps), &len);" in _ls
      and "if (r && len == PAINT_LEN) { memcpy(paint_saved[ps], r, PAINT_LEN); paint_has |= (uint8_t)(1u << ps); }" in _ls,
      "v0.37 boot reads the saved paintings (only records of the right length)")
_pn = code(body("paintm_enter"))
_pl = code(body("paintm_leave"))
check("if (paintm || hostm || dawm) return;" in _pn and "live_release(pad);" in _pn and "led_dual = 1;" in _pn
      and "led_dual = 0;" in _pl and "leds_clear();" in _pl
      and "if (paintm || dawm) { hostm_say(); return; }" in code(body("hostm_enter"))
      and "if (dawm || hostm || paintm) return;" in code(body("dawm_enter")),
      "v0.37 PAINT mode: never over ORC / DAW mode (nor they over it); held notes let go; the short LED slot on, then off; v0.38b ORC mode never over DAW mode")
_pp = code(body("pm_paint"))
check("hm_duty(rgb[j], &m, &f);" in _pp and "led_level[i] = m; led_fine[i] = f;" in _pp
      and "paint_pad_rgb(paint_cv, pad, paint_cur, pal, rgb);" in _pp,
      "v0.37 the canvas goes through ORC mode's colour curve (both LED slots)")
check('{"Paint",       0, MK_ACT,    0, 0, 0, menu_paint_now},' in SRC and "paint.c" in open(os.path.join(HERE, "Makefile")).read(),
      "v0.37 BROWSER > Paint; paint.c is in the build")
# ---- v0.39 the last crash kept in FLASH (kind 61): it survives unplugging ----
_mb = code(body("main"))
check(0 < _mb.find("crash_load(&crash_boot);") < _mb.find("store2_ptr(KIND_CRASH, &len);")
      and "have = have && f.magic == CRASH_REC_MAGIC;" in _mb
      and "if (!have || memcmp(&f, &now, sizeof now)) { crash_rec = now; crash_pend = true; saver_start(); save_paint_only = true; }" in _mb
      and "} else if (have && f.w[0] == 1) memcpy(&crash_boot.valid, f.w, sizeof f.w);" in _mb,
      "v0.39 boot: a crash in the registers flash lacks is copied there (alone); none in the registers + one in flash = flash's is on record")
_sp2 = code(body("saver_poll"))
check(0 < _sp2.find("kind = KIND_PAINT(p); break; }") < _sp2.find("if (kind < 0 && crash_pend) kind = KIND_CRASH;")
      and "kind == KIND_CRASH ? store2_begin(kind, &crash_rec, sizeof crash_rec)" in _sp2
      and "else if (kind == KIND_CRASH) crash_pend = false;" in _sp2 and "else if (saving_kind == KIND_CRASH) crash_pend = true;" in _sp2
      and "if ((paint_pend || crash_pend) && !save_run) { saver_start(); save_paint_only = true; }" in code(body("main")),
      "v0.39 the saver writes the crash record (after paintings, also in a paintings-only run); a failed write retries")
_cc = case_text("HC_CRASH")
check(0 < _cc.find("memset(&crash_boot, 0, sizeof crash_boot);") < _cc.find("if (crash_rec.w[0]) {")
      and _cc.find("store2_cancel(); saving_kind = -1;") < _cc.find("memset(crash_rec.w, 0, sizeof crash_rec.w); crash_rec.magic = CRASH_REC_MAGIC; crash_pend = true;"),
      "v0.39 06 01 clears flash's copy too (a half-written one is cancelled before its bytes change)")
check("crash_rec.w[0] ? (const void *)&crash_rec : 0}" in code(body("compact_all"))
      and "crash_pend = false; }" in code(body("compact_all")),
      "v0.39 the clean-up keeps the crash record (only a real one, not a cleared one)")
print(f"wiring_test: {p} passed, {f} failed (expected {EXPECTED})")
sys.exit(1 if f or p + f != EXPECTED else 0)

