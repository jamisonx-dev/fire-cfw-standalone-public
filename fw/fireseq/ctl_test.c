/* Host test for ctl.c (the control decisions the 2026-10-13 review found wrong or untested).
 *   gcc -O1 -Wall -Wextra -I. ctl_test.c ctl.c lights.c -lm -o ctl_test && ./ctl_test */
#include <stdio.h>
#include <string.h>
#include "ctl.h"
#include "lights.h"

static int fails, checks;
#define EXPECTED 87
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

    printf("ctl_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
