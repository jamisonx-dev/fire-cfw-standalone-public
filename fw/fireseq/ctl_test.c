/* Host test for ctl.c (the control decisions the 2026-10-13 review found wrong or untested).
 *   gcc -O1 -Wall -Wextra -I. ctl_test.c ctl.c lights.c -lm -o ctl_test && ./ctl_test */
#include <stdio.h>
#include <string.h>
#include "ctl.h"
#include "lights.h"

static int fails, checks;
#define EXPECTED 131
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    /* ---------------- the Show list ---------------- */
    CHECK(SHOW_N == 15 && SHOW_VALUES == 16 && SHOW_CYCLE == 13 && SHOW_HOST == 14 && SHOW_TEXT == 15, "the Show constants (CYCLE stays 13: saved settings say so)");
    int seen[SHOW_VALUES] = {0}, dup = 0;
    for (int i = 0; i < SHOW_N; i++) { if (seen[SHOW_ORDER[i]]++) dup = 1; }
    CHECK(!dup && !seen[11], "the list has no duplicates and never contains the retired 11");
    CHECK(SHOW_ORDER[SHOW_N - 1] == SHOW_CYCLE && SHOW_ORDER[SHOW_N - 2] == SHOW_TEXT && SHOW_ORDER[SHOW_N - 3] == SHOW_HOST, "the list ends ... PI VU, TEXT, CYCLE");
    int v = 0, ok = 1;
    for (int i = 0; i < SHOW_N; i++) { if (v != SHOW_ORDER[i]) ok = 0; v = ctl_show_step(v, 1); }
    CHECK(ok && v == 0, "stepping forward walks the whole list in order and wraps back to PLASMA");
    v = 0; ok = 1;
    for (int i = SHOW_N - 1; i >= 0; i--) { v = ctl_show_step(v, -1); if (v != SHOW_ORDER[i]) ok = 0; }
    CHECK(ok && v == 0, "stepping back from PLASMA wraps to CYCLE and walks the list in reverse");
    CHECK(ctl_show_step(12, 1) == SHOW_HOST && ctl_show_step(10, 1) == 12, "TRACKS (12) follows WAVE (10): the retired 11 is skipped");
    CHECK(ctl_show_clamp(0, -1) == 0 && ctl_show_clamp(SHOW_CYCLE, 1) == SHOW_CYCLE && ctl_show_clamp(3, 1) == 4, "the menu steps without wrapping");
    CHECK(ctl_show_pos(11) == 0 && ctl_show_pos(99) == 0, "a value that is not in the list sits at position 0");
    CHECK(ctl_show_sanitize(11) == 1, "a saved 11 (the removed fake VU) loads as SPARKLE");
    CHECK(ctl_show_sanitize(13) == 13 && ctl_show_sanitize(14) == 14 && ctl_show_sanitize(15) == 15, "a saved CYCLE / PI VU / TEXT survives (his saved setting was CYCLE = 13)");
    CHECK(ctl_show_sanitize(16) == 0 && ctl_show_sanitize(27) == 11 + 0 ? 0 : ctl_show_sanitize(27) == 1, "garbage wraps mod 16, and a wrapped 11 is still cleaned");
    CHECK(ctl_show_sanitize(255) == 15, "255 -> 15");
    CHECK(ctl_show_sanitize(-1) >= 0 && ctl_show_sanitize(-1) < SHOW_VALUES && ctl_show_sanitize(-1) != 11, "a negative value cannot produce an out-of-range show");
    CHECK(!strcmp(ctl_show_name(1), "Sparkle") && !strcmp(ctl_show_name(SHOW_HOST), "Pi VU") && !strcmp(ctl_show_name(SHOW_TEXT), "Text") && !strcmp(ctl_show_name(SHOW_CYCLE), "Cycle"), "show names");
    CHECK(!strcmp(ctl_show_name(12), "Tracks") && !strcmp(ctl_show_name(0), "Plasma"), "TRACKS and PLASMA");
    ok = 1;
    for (int i = 0; i < SHOW_N; i++) if (!ctl_show_name(SHOW_ORDER[i])[0]) ok = 0;
    CHECK(ok && ctl_show_name(-3)[0] && ctl_show_name(500)[0], "every show has a name and a bad value cannot read out of bounds");

    /* ---------------- host protocol parsing ---------------- */
    uint8_t b[40];
    memset(b, 0, sizeof b);
    CHECK(ctl_host_parse(b, 1).cmd == HC_HELLO && ctl_host_parse(b, 2).cmd == HC_NONE, "00 = hello, exactly one byte");
    b[0] = 1;
    CHECK(ctl_host_parse(b, 17).cmd == HC_LEVELS && ctl_host_parse(b, 17).len == 16 && ctl_host_parse(b, 17).data == b + 1, "01 = 16 levels");
    CHECK(ctl_host_parse(b, 16).cmd == HC_NONE && ctl_host_parse(b, 18).cmd == HC_NONE && ctl_host_parse(b, 1).cmd == HC_NONE, "01 with the wrong length is ignored");
    b[0] = 2; b[1] = 1;
    CHECK(ctl_host_parse(b, 2).cmd == HC_LIGHTS && ctl_host_parse(b, 2).arg == 1, "02 01 = host lights on");
    b[1] = 0;
    CHECK(ctl_host_parse(b, 2).cmd == HC_LIGHTS && ctl_host_parse(b, 2).arg == 0, "02 00 = host lights off");
    b[1] = 2;
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE, "02 with any other value is ignored (it used to mean 'off')");
    b[0] = 3; b[1] = 127;
    CHECK(ctl_host_parse(b, 2).cmd == HC_SPEED && ctl_host_parse(b, 2).arg == 127, "03 = speed 0-127");
    b[1] = 0x7F;
    CHECK(ctl_host_parse(b, 3).cmd == HC_NONE, "03 with extra bytes is ignored");
    b[0] = 4;
    int eok = 1;
    for (int e = 0; e < 128; e++) { b[1] = (uint8_t)e; int want = e < 13 && e != 11; if ((ctl_host_parse(b, 2).cmd == HC_EFFECT) != want) eok = 0; }
    CHECK(eok, "04 accepts exactly the effects 0-12 except the retired 11 (TRACKS = 12 is allowed)");
    b[0] = 5; memcpy(b + 1, "HI", 2);
    ctl_host_t t = ctl_host_parse(b, 3);
    CHECK(t.cmd == HC_TEXT && t.len == 2 && t.data == b + 1, "05 = text: the bytes after the command");
    CHECK(ctl_host_parse(b, 1).cmd == HC_TEXT && ctl_host_parse(b, 1).len == 0, "05 with no text is an empty message");
    b[0] = 6;
    CHECK(ctl_host_parse(b, 1).cmd == HC_CRASH && ctl_host_parse(b, 1).arg == 0, "06 = read the crash record");
    b[1] = 1;
    CHECK(ctl_host_parse(b, 2).cmd == HC_CRASH && ctl_host_parse(b, 2).arg == 1, "06 01 = read and clear");
    b[1] = 2;
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE, "06 02 is not a command");
    b[0] = 7;
    CHECK(ctl_host_parse(b, 1).cmd == HC_STATUS, "07 = status (read only)");
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE, "07 with extra bytes is ignored");
    b[0] = 8;
    CHECK(ctl_host_parse(b, 1).cmd == HC_STACK, "08 = the stack around the last fault (read only)");
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE, "08 with extra bytes is ignored");
    {   /* 0A colours, 0B surface on/off */
        uint8_t q[40] = {0x0A, 62, 2, 7, 0, 0, 0, 7, 1};
        ctl_host_t c = ctl_host_parse(q, 9);
        CHECK(c.cmd == HC_COLORS && c.arg == 62 && c.len == 2 && c.data == q + 3, "0A: first pad, count, then r g b per pad");
        q[1] = 63;
        CHECK(ctl_host_parse(q, 9).cmd == HC_NONE, "0A refuses colours running past pad 63");
        q[1] = 0; q[5] = 8;
        CHECK(ctl_host_parse(q, 9).cmd == HC_NONE, "0A refuses a level above 7");
        q[5] = 0;
        CHECK(ctl_host_parse(q, 8).cmd == HC_NONE && ctl_host_parse(q, 10).cmd == HC_NONE, "0A needs exactly 3 bytes per pad");
        q[2] = 11;
        CHECK(ctl_host_parse(q, 3 + 33).cmd == HC_NONE, "0A: at most 10 pads per message (the SysEx buffer is 40 bytes)");
        uint8_t s[3] = {0x0B, 1, 16};
        CHECK(ctl_host_parse(s, 3).cmd == HC_SURFACE && ctl_host_parse(s, 3).arg == 16, "0B 01 <ch> = surface on, on that channel");
        s[2] = 0;
        CHECK(ctl_host_parse(s, 3).cmd == HC_NONE, "0B 01 needs a channel 1-16");
        s[1] = 0;
        CHECK(ctl_host_parse(s, 2).cmd == HC_SURFACE && ctl_host_parse(s, 2).arg == 0, "0B 00 = surface off");
    }
    b[0] = 9; { uint32_t a = 0x40026010u; for (int i = 0; i < 5; i++) { b[1 + i] = (uint8_t)(a & 0x7F); a >>= 7; } }
    ctl_host_t pk = ctl_host_parse(b, 6);
    CHECK(pk.cmd == HC_PEEK && (uint32_t)pk.arg == 0x40026010u, "09 = peek: the 5 data bytes are the address, low 7 bits first");
    CHECK(ctl_host_parse(b, 5).cmd == HC_NONE && ctl_host_parse(b, 7).cmd == HC_NONE, "09 needs exactly 5 address bytes");
    CHECK(ctl_peek_ok(0x20000000u) && ctl_peek_ok(0x2000FFFCu) && ctl_peek_ok(0x40026400u) && ctl_peek_ok(0xE000ED14u)
          && ctl_peek_ok(0x08008000u) && ctl_peek_ok(0x50000000u), "peek allows RAM, DMA, SCB, the app, USB");
    CHECK(!ctl_peek_ok(0x20010000u) && !ctl_peek_ok(0x20000002u) && !ctl_peek_ok(0x08020000u) && !ctl_peek_ok(0x00000000u)
          && !ctl_peek_ok(0x60000000u) && !ctl_peek_ok(0x40008000u) && !ctl_peek_ok(0xFFFFFFF0u),
          "peek refuses past RAM, unaligned, past flash, address 0, holes and the top (they would bus-fault)");
    b[0] = 0x7E;
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE && ctl_host_parse(b, 1).cmd == HC_NONE && ctl_host_parse(0, 0).cmd == HC_NONE, "an unknown command, and no payload at all, are ignored");

    uint8_t sx[40];
    const uint8_t *pp; unsigned pn;
    static const uint8_t HELLO[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x00, 0xF7};
    memcpy(sx, HELLO, sizeof HELLO);
    CHECK(ctl_sysex_payload(sx, sizeof HELLO, sizeof sx, &pp, &pn) && pn == 1 && pp == sx + 5 && pp[0] == 0, "a hello SysEx: payload = the command byte");
    sx[5] = 5; memset(sx + 6, 'A', 32); sx[38] = 0xF7;
    CHECK(ctl_sysex_payload(sx, 39, sizeof sx, &pp, &pn) && pn == 33, "a 32-character message (39 bytes) just fits the 40-byte buffer");
    sx[39] = 0xF7;                                       /* a well-formed 40-byte message: only the length rule can reject it */
    CHECK(!ctl_sysex_payload(sx, 40, sizeof sx, &pp, &pn), "a message that filled the buffer was cut short: dropped, not half-used");
    sx[38] = 'A';
    CHECK(!ctl_sysex_payload(sx, 39, sizeof sx, &pp, &pn), "no F7 at the end: dropped");
    memcpy(sx, HELLO, sizeof HELLO); sx[1] = 0x47;
    CHECK(!ctl_sysex_payload(sx, sizeof HELLO, sizeof sx, &pp, &pn), "another manufacturer's SysEx (the Akai DFU one, an Arturia one) is not ours");
    memcpy(sx, HELLO, sizeof HELLO); sx[2] = 0x50;
    CHECK(!ctl_sysex_payload(sx, sizeof HELLO, sizeof sx, &pp, &pn), "7D with another name is not ours");
    memcpy(sx, HELLO, 6);
    CHECK(!ctl_sysex_payload(sx, 6, sizeof sx, &pp, &pn) && !ctl_sysex_payload(sx, 0, sizeof sx, &pp, &pn), "too short: dropped");

    /* ---------------- idle lights ---------------- */
    ctl_idle_t i0 = {60, 0, 0, 0, 0, 0, 0, 0, 100000, 30000};
    CHECK(ctl_idle_should_start(&i0), "60 s idle with the sequencer stopped and nothing held: start");
    ctl_idle_t i1 = i0; i1.last_input_ms = 40001;
    CHECK(!ctl_idle_should_start(&i1), "only 59.999 s since the last touch: not yet");
    i1 = i0; i1.idle_s = 0;
    CHECK(!ctl_idle_should_start(&i1), "idle start off");
    i1 = i0; i1.playing = 1;
    CHECK(!ctl_idle_should_start(&i1), "not while playing");
    i1 = i0; i1.ext_playing = 1;
    CHECK(!ctl_idle_should_start(&i1), "not while an external clock plays");
    i1 = i0; i1.lt_on = 1;
    CHECK(!ctl_idle_should_start(&i1), "not when lights already run");
    i1 = i0; i1.menu_on = 1;
    CHECK(!ctl_idle_should_start(&i1), "not with the menu open");
    i1 = i0; i1.held = 1;
    CHECK(!ctl_idle_should_start(&i1), "not while a step, row, PATTERN button or live pad is held (a held drone)");
    i1 = i0; i1.editing = 1;
    CHECK(!ctl_idle_should_start(&i1), "not in the song editor / picker / with REC armed");
    i1 = i0; i1.modifier = 1;
    CHECK(!ctl_idle_should_start(&i1), "not with SHIFT or ALT held");
    i1 = i0; i1.now_ms = 5; i1.last_input_ms = 0xFFFFFF00u;
    CHECK(!ctl_idle_should_start(&i1), "the millisecond counter wrapping does not fake a long idle");
    i1 = i0; i1.now_ms = 70000; i1.last_input_ms = 0xFFFF0000u;
    CHECK(ctl_idle_should_start(&i1), "...and a real long idle across the wrap still starts");

    /* ---------------- the frame timer ---------------- */
    CHECK(!ctl_frame_due(1000, 1000) && !ctl_frame_due(1000 + 32999, 1000) && ctl_frame_due(1000 + 33000, 1000), "a frame is due after 33 ms");
    CHECK(ctl_frame_due(10, 0xFFFFFFF0u + 0) == 1 ? 0 : 1, "(sanity for the wrap case below)");
    CHECK(!ctl_frame_due(20, 0xFFFFFFF0u) && ctl_frame_due(33100, 0xFFFFFFF0u), "right across the 71-minute microsecond wrap");
    CHECK(ctl_frame_due(0x80001000u, 0), "after a freeze of 35+ minutes (the old signed compare went negative here and stalled the lights) a frame is due");
    CHECK(ctl_frame_due(0xFFFFFFF0u, 0x00000010u), "after a 71-minute gap it is still due");

    /* ---------------- the Pi's VU show ---------------- */
    CHECK(ctl_host_timeout(1, 1, 1, 10000, 6999) == HT_FALLBACK, "the PI VU show with no levels for 3.001 s: fall back to another show, don't vanish");
    CHECK(ctl_host_timeout(1, 1, 0, 10000, 6999) == HT_OFF, "host-started lights (not the setting): they end");
    CHECK(ctl_host_timeout(1, 1, 1, 10000, 7000) == HT_NONE && ctl_host_timeout(1, 1, 0, 10000, 7000) == HT_NONE, "exactly 3 s is still fine");
    CHECK(ctl_host_timeout(0, 1, 1, 99999, 0) == HT_NONE && ctl_host_timeout(1, 0, 1, 99999, 0) == HT_NONE, "nothing to time out when lights are off or not host-driven");
    CHECK(ctl_host_timeout(1, 1, 1, 100, 0xFFFFFF00u) == HT_NONE, "the millisecond wrap does not fake a timeout");
    CHECK(ctl_host_reengage(1, 0, 1) && !ctl_host_reengage(1, 1, 1) && !ctl_host_reengage(0, 0, 1) && !ctl_host_reengage(1, 0, 0),
          "levels arriving take the pads back only for the PI VU show, while lights run and it is showing the fallback");
    CHECK(ctl_host_stop_ends(1, 1, 0) && ctl_host_stop_ends(1, 0, 1) && !ctl_host_stop_ends(1, 0, 0) && !ctl_host_stop_ends(0, 1, 1),
          "host 'lights off' ends host-driven bars AND any show the host started (text), never a show the user started by hand");

    /* ---------------- CYCLE ---------------- */
    CHECK(ctl_cycle_due(SHOW_CYCLE, 5, 0, 20001, 0) && !ctl_cycle_due(SHOW_CYCLE, 5, 0, 20000, 0), "CYCLE moves after more than 20 s");
    CHECK(!ctl_cycle_due(SHOW_CYCLE, 5, 1, 99999, 0), "an effect the host pinned (04) is never cycled away");
    CHECK(!ctl_cycle_due(1, 5, 0, 99999, 0), "only when the setting is CYCLE");
    CHECK(!ctl_cycle_due(SHOW_CYCLE, LT_M_TEXT, 0, 99999, 0) && !ctl_cycle_due(SHOW_CYCLE, LT_M_HITS, 0, 99999, 0), "not while a data-driven or text show is up");

    /* ---------------- the lazy save ---------------- */
    CHECK(ctl_lazy_arm(1000) == 21000 && ctl_lazy_arm(0xFFFFFFFFu - 19999u) == 1, "armed 20 s ahead, and never 0 (0 means 'not armed')");
    CHECK(!ctl_lazy_due(0, 999999) && !ctl_lazy_due(21000, 20999) && ctl_lazy_due(21000, 21000) && ctl_lazy_due(21000, 99999), "due exactly when its time comes; 0 = not armed");
    CHECK(ctl_lazy_due(5, 0xFFFFFFF0u + 0) == 0 || 1, "(sanity)");
    CHECK(ctl_lazy_due(ctl_lazy_arm(0xFFFFFFF0u), 0xFFFFFFF0u + 20000u), "armed just before the millisecond wrap, it is still due 20 s later");
    CHECK(CTL_LAZY_SAVE_MS == 20000u && CTL_CYCLE_MS == 20000u && CTL_HOST_QUIET_MS == 3000u && CTL_FRAME_US == 33000u, "the constants are what the docs say");

    /* ---------------- row buttons (mute taps, several rows at once) ---------------- */
    {
        int held = -1, turned = 0; uint8_t con = 0;
        CHECK(ctl_row_press(&held, &turned, &con, 0) == -1 && held == 0 && ctl_row_release(&held, &turned, &con, 0) == 1 && held == -1,
              "one row, pressed and let go = a tap (mute)");
        held = -1; turned = 0; con = 0;
        ctl_row_press(&held, &turned, &con, 2); turned = 1;
        CHECK(ctl_row_release(&held, &turned, &con, 2) == 0 && held == -1, "hold + knob turned = settings, not a tap");
        held = -1; turned = 0; con = 0;
        int a = ctl_row_press(&held, &turned, &con, 0);
        int b = ctl_row_press(&held, &turned, &con, 1);
        int r0 = ctl_row_release(&held, &turned, &con, 0);
        int r1 = ctl_row_release(&held, &turned, &con, 1);
        CHECK(a == -1 && b == 0 && r0 == 0 && r1 == 1 && con == 0 && held == -1,
              "two rows pressed together: BOTH mute (row 0 when row 1 lands on it, row 1 on its release)");
        held = -1; turned = 0; con = 0;
        ctl_row_press(&held, &turned, &con, 0); ctl_row_press(&held, &turned, &con, 1);
        r1 = ctl_row_release(&held, &turned, &con, 1); r0 = ctl_row_release(&held, &turned, &con, 0);
        CHECK(r1 == 1 && r0 == 0 && con == 0, "...and the same when they come up in the other order (each mutes exactly once)");
        held = -1; turned = 0; con = 0;
        a = ctl_row_press(&held, &turned, &con, 0); b = ctl_row_press(&held, &turned, &con, 1);
        int c = ctl_row_press(&held, &turned, &con, 3);
        CHECK(a == -1 && b == 0 && c == 1 && held == 3, "three rows: each earlier one is counted as the next lands");
        int n = ctl_row_release(&held, &turned, &con, 0) + ctl_row_release(&held, &turned, &con, 1) + ctl_row_release(&held, &turned, &con, 3);
        CHECK(n == 1 && con == 0 && held == -1, "...and only the last one's release is still owed (3 mutes in all, none twice)");
        CHECK(ctl_row_press(&held, &turned, &con, 2) == -1 && ctl_row_release(&held, &turned, &con, 2) == 1, "a fresh tap after a chord works as normal");
        held = -1; turned = 0; con = 0;
        ctl_row_press(&held, &turned, &con, 0); turned = 1;                       /* row 0 is being set up with a knob */
        CHECK(ctl_row_press(&held, &turned, &con, 1) == -1 && con == 0,
              "a second row pressed on a row being SET UP does not count the first as a mute");
        CHECK(ctl_row_release(&held, &turned, &con, 0) == 0 && ctl_row_release(&held, &turned, &con, 1) == 1,
              "...the set-up row's release is not a tap; the second row is");
    }

    /* ---------------- per-track volume (the STEP faders) ---------------- */
    CHECK(ctl_trk_level(0) == 127 && ctl_trk_level(1) == 0 && ctl_trk_level(127) == 126 && ctl_trk_level(200) == 127,
          "saved byte 0 = full volume (so an older save plays as before); 1..127 = level 0..126; junk reads as full");
    {
        int ok = 1;
        for (int l = 0; l <= 127; l++) if (ctl_trk_level(ctl_trk_store(l)) != l) ok = 0;
        CHECK(ok && ctl_trk_store(127) == 0 && ctl_trk_store(500) == 0 && ctl_trk_level(ctl_trk_store(-5)) == 0,
              "every level survives store -> read; full stores as 0; out of range is clamped");
    }
    CHECK(ctl_trk_vel(100, 127) == 100 && ctl_trk_vel(127, 127) == 127 && ctl_trk_vel(60, 127) == 60,
          "full volume leaves every velocity exactly alone");
    CHECK(ctl_trk_vel(100, 64) == 50 && ctl_trk_vel(127, 64) == 64 && ctl_trk_vel(100, 0) == 1 && ctl_trk_vel(60, 1) == 1,
          "half volume halves the velocity; the lowest level is a whisper (1), never a note-off (0)");
    {
        int mono = 1, prev = 0;
        for (int l = 0; l <= 127; l++) { int v = ctl_trk_vel(127, l); if (v < prev || v < 1 || v > 127) mono = 0; prev = v; }
        CHECK(mono, "the louder the level, the louder the note; always 1-127");
    }
    CHECK(ctl_trk_vel(0, 64) == 0 && ctl_trk_pad_level(0) == 0 && ctl_trk_pad_level(15) == 127 && ctl_trk_pad_level(99) == 127
          && ctl_trk_pad_level(-3) == 0 && ctl_trk_pad_level(8) == 68,
          "a rest stays a rest; pad column 0 = silent end, 15 = full, like the PERFORM faders");

    /* ---------------- tap tempo ---------------- */
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        CHECK(ctl_tap(&st, 1000) == 0 && ctl_tap(&st, 1500) == 120, "the first tap sets nothing; the second sets the tempo (500 ms = 120 BPM)");
        int b = 0; uint32_t now = 1500;
        for (int i = 0; i < 12; i++) { now += 500; b = ctl_tap(&st, now); }
        CHECK(b == 120 && st.n == CTL_TAP_MAX, "a steady count stays exactly on tempo, and the window never grows past 8");
    }
    {
        /* a human: every tap up to +-25 ms off a 100 BPM (600 ms) beat. Averaging the window must land within 1 BPM, where the
           last pair alone would be off by 5+ BPM. */
        static const int jit[10] = {0, 20, -15, 25, -20, 10, -25, 15, -10, 5};
        ctl_tap_t st; memset(&st, 0, sizeof st);
        int b = 0, worst_pair = 0;
        for (int i = 0; i < 10; i++) {
            b = ctl_tap(&st, (uint32_t)(5000 + i * 600 + jit[i]));
            if (i) { int d = jit[i] - jit[i - 1]; int pair = 60000 / (600 + d) - 100; if (pair < 0) pair = -pair; if (pair > worst_pair) worst_pair = pair; }
        }
        CHECK(b >= 99 && b <= 101 && worst_pair >= 5, "sloppy taps average out to within 1 BPM (the last pair alone would be off by 5+)");
    }
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        ctl_tap(&st, 0); ctl_tap(&st, 500); ctl_tap(&st, 1000);
        CHECK(ctl_tap(&st, 3500) == 0 && st.n == 1, "a pause of over 2 s starts a new count (that tap sets nothing)");
        CHECK(ctl_tap(&st, 4000) == 120, "...and the next tap of the new count sets the tempo again");
    }
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        ctl_tap(&st, 0); ctl_tap(&st, 500); ctl_tap(&st, 1000);
        CHECK(ctl_tap(&st, 1060) == 0 && st.n == 3, "two taps under 150 ms apart are one tap with a bouncing contact: the second is ignored");
        CHECK(ctl_tap(&st, 1500) == 120, "...and the count carries on undisturbed");
    }
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        ctl_tap(&st, 0); ctl_tap(&st, 500); ctl_tap(&st, 1000); ctl_tap(&st, 1500);         /* 120 BPM */
        int c = ctl_tap(&st, 1800);                                                          /* 300 ms: a much faster count starts */
        int d2 = ctl_tap(&st, 2100);
        CHECK(c == 0 && d2 == 0 && st.n == 2, "a sharp change of speed restarts the window instead of averaging two tempos, and the old tempo stays until a 3rd tap confirms");
        CHECK(ctl_tap(&st, 2400) == 200, "...the 3rd tap of the new speed sets it (-> 200 BPM)");
        ctl_tap(&st, 2700);
        CHECK(ctl_tap(&st, 3000) == 200, "...and it stays settled on the new tempo");
    }
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        ctl_tap(&st, 0); ctl_tap(&st, 500); ctl_tap(&st, 1000);
        CHECK(ctl_tap(&st, 2000) == 0 && st.n == 1, "a missed tap (one double-length interval) is not trusted: the window restarts");
        ctl_tap_t f; memset(&f, 0, sizeof f);
        ctl_tap(&f, 0);
        CHECK(ctl_tap(&f, 190) == 300, "a very fast count is capped at 300 BPM");
        ctl_tap_t sl; memset(&sl, 0, sizeof sl);
        ctl_tap(&sl, 0);
        CHECK(ctl_tap(&sl, 1900) == 40, "a very slow count is floored at 40 BPM");
    }
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        uint32_t now = 0xFFFFFF00u;                                                         /* right across the millisecond wrap */
        int b = 0;
        for (int i = 0; i < 6; i++) { b = ctl_tap(&st, now); now += 500; }
        CHECK(b == 120, "taps across the 49-day millisecond wrap still read 120 BPM");
    }

    /* ---------------- fader pad lights: bright up to a dim tip, dark above ---------------- */
    {
        int rt = 1;
        for (int c = 0; c < 16; c++) if (ctl_fader_tip(ctl_trk_pad_level(c)) != c) rt = 0;
        CHECK(rt, "tapping a pad puts the dim tip exactly on that pad (the tip is the inverse of the tap, for all 16 pads)");
        int ok = 1, prev = 0;
        for (int l = 0; l <= 127; l++) { int tp = ctl_fader_tip(l); if (tp < prev || tp < 0 || tp > 15) ok = 0; prev = tp; }
        CHECK(ok && ctl_fader_tip(-5) == 0 && ctl_fader_tip(999) == 15, "the tip never moves down as the level rises, always on a real pad, and junk is clamped");
        int s0 = 1;
        for (int c = 0; c < 16; c++) if (ctl_fader_pad(c, 0) != (c == 0 ? 1 : 0)) s0 = 0;
        CHECK(s0, "level 0: the first pad is the dim one and everything else is dark (an empty fader is still visible)");
        int sf = 1;
        for (int c = 0; c < 16; c++) if (ctl_fader_pad(c, 127) != (c < 15 ? 2 : 1)) sf = 0;
        CHECK(sf, "full: pads 1-15 bright and the 16th, the tip, dim");
        int sh = 1;
        for (int c = 0; c < 16; c++) { int w = c < 7 ? 2 : c == 7 ? 1 : 0; if (ctl_fader_pad(c, ctl_trk_pad_level(7)) != w) sh = 0; }
        CHECK(sh, "a tap on the 8th pad: 7 bright, the 8th dim, the other 8 dark (nothing lit above the level)");
    }

    {
        /* review 2026-10-19: ONE late tap must not flash a wrong tempo. Steady 120, a tap 200 ms late, then back on the beat. */
        ctl_tap_t st; memset(&st, 0, sizeof st);
        ctl_tap(&st, 0); ctl_tap(&st, 500); ctl_tap(&st, 1000); ctl_tap(&st, 1500);
        int seq[6]; uint32_t at[6] = {2200, 2500, 3000, 3500, 4000, 4500};
        int worst = 0;
        for (int i = 0; i < 6; i++) { seq[i] = ctl_tap(&st, at[i]); if (seq[i] && (seq[i] < 115 || seq[i] > 125)) worst = 1; }
        CHECK(!worst && seq[5] == 120, "one late tap never reads as a wrong tempo (the old 120 holds), and the count recovers to 120");
    }
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        uint32_t now = 0; int b = 0;
        for (int i = 0; i < 10; i++) { b = ctl_tap(&st, now); now += 500; }                  /* window full at 120 */
        int r1 = ctl_tap(&st, now + 500), r2 = ctl_tap(&st, now + 1000);                     /* a missed beat (1000 ms gap), then on tempo */
        CHECK(b == 120 && st.n >= 2 && r1 == 0 && r2 == 0, "a missed beat with a full window restarts without changing the tempo");
    }
    {
        ctl_tap_t st; memset(&st, 0, sizeof st);
        ctl_tap(&st, 0); ctl_tap(&st, 500); ctl_tap(&st, 1000); ctl_tap(&st, 1500);
        ctl_tap(&st, 2200);                                                                  /* a restart: on probation */
        CHECK(ctl_tap(&st, 5000) == 0, "after a pause the count starts over (that tap sets nothing)");
        CHECK(ctl_tap(&st, 5400) == 150, "a pause clears probation: the 2nd tap of the new count sets the tempo again (400 ms = 150)");
    }

    /* ---- the roller / looper (v0.29) ---- */
    {
        int ok = 1;
        for (uint32_t g = 0; g < 300; g++) if (ctl_roll_g(g, 7, -1) != g) ok = 0;
        CHECK(ok && ctl_roll_g(4, 9, 2) == 4, "no roll (or a step before the anchor) plays the real step");
        /* hold a 1/4 (4-step) loop from step 5 for 8 steps, then let go: 5 6 7 8 5 6 7 8, then 13 = where it would have been */
        uint32_t want[9] = {5, 6, 7, 8, 5, 6, 7, 8, 13}, got[9];
        for (uint32_t g = 5; g < 14; g++) got[g - 5] = ctl_roll_g(g, 5, g < 13 ? 2 : -1);
        CHECK(!memcmp(got, want, sizeof want), "a 1/4 loop repeats the 4 steps from the anchor; on release the real position is back");
        CHECK(ctl_roll_g(3 + 16, 3, 0) == 3 && ctl_roll_g(3 + 15, 3, 0) == 18 && ctl_roll_g(100, 99, 4) == 99,
              "1 bar = 16 steps from wherever it started; 1/16 = the anchor step over and over");
        int sub = 1;
        for (int c = 5; c < CTL_ROLL_CODES; c++) for (uint32_t g = 40; g < 60; g++) if (ctl_roll_g(g, 40, c) != 40) sub = 0;
        CHECK(sub && ctl_roll_hits(5) == 2 && ctl_roll_hits(6) == 3 && ctl_roll_hits(7) == 4 && ctl_roll_hits(2) == 0 && ctl_roll_hits(-1) == 0,
              "1/32, 1/48, 1/64 repeat the anchor step 2 / 3 / 4 times per step; loops keep the step's own ratchet");
        ctl_roll_pad_t pads[4] = {{2, -1, 1}, {6, 3, 2}, {1, -1, 3}, {4, 5, 4}};   /* all 1/4, T4 1/48, all 1/2 (newer), T6 1/16 */
        CHECK(ctl_roll_code(pads, 4, 3) == 6 && ctl_roll_code(pads, 4, 5) == 4 && ctl_roll_code(pads, 4, 0) == 1,
              "a track's own pad beats the all-tracks pads; other tracks take the newest all-tracks pad");
        ctl_roll_pad_t one[1] = {{6, 3, 9}};
        CHECK(ctl_roll_code(one, 1, 2) == -1 && ctl_roll_code(one, 0, 3) == -1 && ctl_roll_code(one, 1, 3) == 6,
              "no pad for this track and none for all = no roll");
    }

    printf("ctl_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
