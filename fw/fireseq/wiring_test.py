"""main.c is bare-metal and cannot run on the host, so ctl_test.c proves the RULES and this proves main.c USES them
(each check names the review finding it guards). fire_live_test.py then proves the behaviour on the real Fire.
    python3 wiring_test.py        (any OS; reads main.c / lights.c next to it)"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(HERE, "main.c"), encoding="utf-8").read().replace("\r\n", "\n")
LIGHTS = open(os.path.join(HERE, "lights.c"), encoding="utf-8").read().replace("\r\n", "\n")
EXPECTED = 47
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
check(0 < _he.find("if (pad >= 0 && surface_on)") < _he.find("if (pad >= 0 && picker_on())")
      and _he.find("live[pad].on = false;") < _he.find("if (pad >= 0 && surface_on)"),
      "v0.19: surface pads are handled before any other pad meaning, and their release goes through live[]")
_sp = _he[_he.find("if (pad >= 0 && surface_on)"):_he.find("if (pad >= 0 && picker_on())")]
check("surf_send(pad, vel);" in _sp and "0x90" not in _sp and "live[pad].sx = true" in _sp
      and "0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x0C" in body("surf_send"),
      "v0.21: a surface pad press is the 0C SysEx, never a note on a channel (no channel taken from anyone)")
_rl = _he[_he.find("if (pad >= 0 && !down && live[pad].on)"):_he.find("if (pad >= 0 && surface_on)")]
check(0 < _rl.find("if (live[pad].sx)") < _rl.find("surf_send(pad, 0);") < _rl.find("notes_held[")
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
check(re.search(r"case IN_KNOBMODE:\s*if \(held_t >= 0 && held_row < 0\) \{ knob_page \^= 1; held_pad_turned = true; \}\s*else \{ G\.knob_bank =", _ev) is not None,
      "KNOB MODE: a held step flips its knob page (and counts as an edit, so releasing keeps the step); otherwise the next knob bank")
check("int b = G.knob_bank & 3, f = kb_fn(b, k);" in _ev and "midi_send3((uint8_t)(0xB0 | ch), (uint8_t)f" in _ev,
      "the free knobs send what their bank says (CC + channel from kb_fn / kb_chan)")
_kf = code(body("kb_fn"))
check("return k == 0 ? KB_SWING : 16 + k;" in _kf and "return 16 + 4 * b + k;" in _kf,
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
check(re.search(r"if \(grid_mpc\(g\)\) \{\s*midi_send3\(\(uint8_t\)\(0xB0 \| ch\), note, 127\);[^}]*live\[pad\]\.cc = true;[^}]*continue;\s*\}\s*if \(rec_arm\) record_hit", _ev) is not None,
      "MPC pads: a press sends its CC = 127 and is never recorded as a note")
check(re.search(r"if \(live\[pad\]\.cc\) \{\s*live\[pad\]\.cc = false;\s*midi_send3\(\(uint8_t\)\(0xB0 \| live\[pad\]\.chan\), live\[pad\]\.note, 0\);", _ev) is not None,
      "MPC pads: the release sends the same CC = 0 (not a note off)")

print(f"wiring_test: {p} passed, {f} failed (expected {EXPECTED})")
sys.exit(1 if f or p + f != EXPECTED else 0)
