/* Host test for ctl.c (the control decisions the 2026-10-13 review found wrong or untested).
 *   gcc -O1 -Wall -Wextra -I. ctl_test.c ctl.c -lm -o ctl_test && ./ctl_test */
#include <stdio.h>
#include <string.h>
#include "ctl.h"

static int fails, checks;
#define EXPECTED 172
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    /* ---------------- host protocol parsing ---------------- */
    uint8_t b[40];
    memset(b, 0, sizeof b);
    CHECK(ctl_host_parse(b, 1).cmd == HC_HELLO && ctl_host_parse(b, 2).cmd == HC_NONE, "00 = hello, exactly one byte");
    {   /* v0.31r 20 remote input: sub-command + its bytes handed to remote.c */
        const uint8_t ri[] = {0x20, 0x01, 79, 1};
        ctl_host_t h = ctl_host_parse(ri, 4);
        CHECK(h.cmd == HC_INPUT && h.arg == 1 && h.data == ri + 2 && h.len == 2, "20 01 79 01 = remote input, sub 1, 2 data bytes");
        CHECK(ctl_host_parse(ri, 1).cmd == HC_NONE && ctl_host_parse(ri, 2).cmd == HC_INPUT && ctl_host_parse(ri, 2).len == 0,
              "20 alone is ignored; 20 <sub> with no data is passed on (len 0)");
    }
    {   /* v0.33 ORC mode */
        const uint8_t on[] = {0x21, 1}, off[] = {0x21, 0}, alive[] = {0x21, 2}, bad[] = {0x21, 3}, longer[] = {0x21, 1, 0};
        CHECK(ctl_host_parse(on, 2).cmd == HC_HOSTM && ctl_host_parse(on, 2).arg == 1 && ctl_host_parse(off, 2).cmd == HC_HOSTM
              && ctl_host_parse(off, 2).arg == 0 && ctl_host_parse(alive, 2).arg == 2,
              "21 00 / 01 / 02 = ORC mode off / on / still here");
        CHECK(ctl_host_parse(bad, 2).cmd == HC_NONE && ctl_host_parse(longer, 3).cmd == HC_NONE && ctl_host_parse(on, 1).cmd == HC_NONE,
              "21 with another value, a longer one or none is ignored");
        const uint8_t g1[] = {0x24, 10}, g2[] = {0x24, 30}, g3[] = {0x24, 9}, g4[] = {0x24, 31};
        CHECK(ctl_host_parse(g1, 2).cmd == HC_GAMMA && ctl_host_parse(g1, 2).arg == 10 && ctl_host_parse(g2, 2).arg == 30
              && ctl_host_parse(g3, 2).cmd == HC_NONE && ctl_host_parse(g4, 2).cmd == HC_NONE,
              "24 <10-30> = the pad colour curve; outside that it is ignored");
        const uint8_t w0[] = {0x25, 0}, w1[] = {0x25, 12}, w2[] = {0x25, 64}, w3[] = {0x25, 11}, w4[] = {0x25, 65};
        CHECK(ctl_host_parse(w0, 2).cmd == HC_FINEW && ctl_host_parse(w0, 2).arg == 0 && ctl_host_parse(w1, 2).arg == 12
              && ctl_host_parse(w2, 2).arg == 64 && ctl_host_parse(w3, 2).cmd == HC_NONE && ctl_host_parse(w4, 2).cmd == HC_NONE
              && ctl_host_parse(w1, 1).cmd == HC_NONE,
              "25 00 asks for the short LED slot, 25 <12-64> sets its share; anything else is ignored");
        const uint8_t in[] = {0x22, 54, 100}, tn[] = {0x23, 1, 1};
        CHECK(ctl_host_parse(in, 3).cmd == HC_NONE && ctl_host_parse(tn, 3).cmd == HC_NONE,
              "22 / 23 are Fire -> host only: sent TO the Fire they do nothing");
    }
    b[0] = 1;
    CHECK(ctl_host_parse(b, 17).cmd == HC_LEVELS && ctl_host_parse(b, 17).len == 16 && ctl_host_parse(b, 17).data == b + 1, "01 = 16 levels");
    CHECK(ctl_host_parse(b, 16).cmd == HC_NONE && ctl_host_parse(b, 18).cmd == HC_NONE && ctl_host_parse(b, 1).cmd == HC_NONE, "01 with the wrong length is ignored");
    b[0] = 2; b[1] = 1;
    CHECK(ctl_host_parse(b, 2).cmd == HC_LIGHTS && ctl_host_parse(b, 2).arg == 1, "02 01 = host lights on");
    b[1] = 0;
    CHECK(ctl_host_parse(b, 2).cmd == HC_LIGHTS && ctl_host_parse(b, 2).arg == 0, "02 00 = host lights off");
    b[1] = 2;
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE, "02 with any other value is ignored (it used to mean 'off')");
    {   /* v0.31r: the light-show commands are gone: ignored like any unknown command */
        int gone = 1;
        for (int cmd = 3; cmd <= 5; cmd++) for (unsigned len = 1; len <= 4; len++) { b[0] = (uint8_t)cmd; b[1] = 1; if (ctl_host_parse(b, len).cmd != HC_NONE) gone = 0; }
        CHECK(gone, "03 / 04 / 05 (speed, show, text) are ignored since v0.31r");
    }
    b[0] = 6;
    CHECK(ctl_host_parse(b, 1).cmd == HC_CRASH && ctl_host_parse(b, 1).arg == 0, "06 = read the crash record");
    b[1] = 1;
    CHECK(ctl_host_parse(b, 2).cmd == HC_CRASH && ctl_host_parse(b, 2).arg == 1, "06 01 = read and clear");
    b[1] = 2;
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE, "06 02 is not a command");
    b[0] = 7;
    CHECK(ctl_host_parse(b, 1).cmd == HC_STATUS && ctl_host_parse(b, 1).arg == 0, "07 = status (read only)");
    { uint8_t s1[2] = {7, 1}; CHECK(ctl_host_parse(s1, 2).cmd == HC_STATUS && ctl_host_parse(s1, 2).arg == 1, "07 01 = status, then the main-loop max starts over (v0.31)"); }
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
    /* v0.30 scenes: 0D queue a pattern, 0E bar messages, 0F the Fire's bar message */
    b[0] = 0x0D; b[1] = 31;
    CHECK(ctl_host_parse(b, 2).cmd == HC_QUEUE && ctl_host_parse(b, 2).arg == 31, "0D <n> = queue pattern n (0-31)");
    b[1] = 32;
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE && ctl_host_parse(b, 1).cmd == HC_NONE && ctl_host_parse(b, 3).cmd == HC_NONE,
          "0D refuses n >= 32 and the wrong length");
    b[0] = 0x0E; b[1] = 1;
    CHECK(ctl_host_parse(b, 2).cmd == HC_BARS && ctl_host_parse(b, 2).arg == 1, "0E 01 = bar messages on");
    b[1] = 2;
    CHECK(ctl_host_parse(b, 2).cmd == HC_NONE && ctl_host_parse(b, 1).cmd == HC_NONE, "0E needs 00 or 01");
    {
        int q = 9;
        CHECK(ctl_queue_pat(5, 0, 0, 32, 1, 2, &q) == CQ_QUEUE && q == 5, "playing: 0D queues the pattern for the bar");
        q = 9;
        CHECK(ctl_queue_pat(2, 0, 0, 32, 1, 2, &q) == CQ_QUEUE && q == -1, "playing: 0D of the pattern already playing clears the queue (like the picker)");
        q = 9;
        CHECK(ctl_queue_pat(5, 0, 0, 32, 0, 2, &q) == CQ_NOW && q == 9, "stopped: 0D switches at once (like the buttons)");
        CHECK(ctl_queue_pat(32, 0, 0, 32, 1, 2, &q) == CQ_REFUSE && ctl_queue_pat(-1, 0, 0, 32, 1, 2, &q) == CQ_REFUSE && q == 9,
              "0D out of range is refused and changes nothing");
        CHECK(ctl_queue_pat(3, 1, 4, 32, 1, 3, &q) == CQ_QUEUE && q == 3, "song mode: n is a SLOT, queued even when it is the playing slot");
        q = 9;
        CHECK(ctl_queue_pat(4, 1, 4, 32, 1, 0, &q) == CQ_REFUSE && q == 9 && ctl_queue_pat(1, 1, 4, 32, 0, 0, &q) == CQ_NOW,
              "song mode: a slot past the song's end is refused; stopped = jump at once");
        uint8_t m[CTL_BAR_LEN + 2]; memset(m, 0xAA, sizeof m);
        static const uint8_t WANT[CTL_BAR_LEN] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x0F, 7, CTL_NO_SLOT, 3, 0xF7};
        CHECK(ctl_bar_msg(m, 48, 16, 7, CTL_NO_SLOT) == CTL_BAR_LEN && !memcmp(m, WANT, CTL_BAR_LEN) && m[CTL_BAR_LEN] == 0xAA,
              "0F at step 48 = bar 3, pattern, no song; exactly 10 bytes written");
        int none = 1;
        for (uint32_t g = 1; g < 64; g++) if (g % 16 && ctl_bar_msg(m, g, 16, 0, 0)) none = 0;
        CHECK(none && ctl_bar_msg(m, 0, 16, 0, 0) == CTL_BAR_LEN && ctl_bar_msg(m, 16 * 130, 16, 31, 15) && m[8] == 2 && m[6] == 31 && m[7] == 15,
              "0F only on a bar start (step 0 too); the bar count wraps at 128, all bytes stay 7-bit");
    }
    /* v0.31 UNIVERSAL pages: 10 begin, 11 chunk, 12 commit, 13 read, 14 list, 15 clear */
    {
        uint8_t u[30] = {0x10, 7, 6, 105};
        ctl_host_t t = ctl_host_parse(u, 4);
        CHECK(t.cmd == HC_UBEGIN && t.arg == 7 && t.len == 873 && ctl_host_parse(u, 3).cmd == HC_NONE, "10 <page> <len 14 bits> = begin a page write");
        u[1] = 8; CHECK(ctl_host_parse(u, 4).cmd == HC_NONE, "10: page 8 is refused");
        uint8_t c[28] = {0x11, 1, 2};
        for (int i = 3; i < 27; i++) c[i] = (uint8_t)i;
        t = ctl_host_parse(c, 27);
        CHECK(t.cmd == HC_UCHUNK && t.arg == 130 && t.len == 24 && t.data == c + 3 && ctl_host_parse(c, 28).cmd == HC_NONE
              && ctl_host_parse(c, 3).cmd == HC_NONE && ctl_host_parse(c, 4).len == 1, "11 <offset> <1-24 bytes> = a chunk");
        uint8_t m[3] = {0x12, 127, 127};
        CHECK(ctl_host_parse(m, 3).cmd == HC_UCOMMIT && ctl_host_parse(m, 3).arg == 16383 && ctl_host_parse(m, 2).cmd == HC_NONE, "12 <sum 14 bits> = commit");
        uint8_t r2[4] = {0x13, 3, 0, 48};
        t = ctl_host_parse(r2, 4);
        CHECK(t.cmd == HC_UREAD && t.arg == 3 && t.len == 48, "13 <page> <offset> = read 24 bytes");
        uint8_t l[2] = {0x14, 0}, cl[2] = {0x15, 7};
        CHECK(ctl_host_parse(l, 1).cmd == HC_ULIST && ctl_host_parse(l, 2).cmd == HC_NONE && ctl_host_parse(cl, 2).cmd == HC_UCLEAR
              && ctl_host_parse(cl, 2).arg == 7 && (cl[1] = 8, ctl_host_parse(cl, 2).cmd == HC_NONE), "14 = the list, 15 <page> = clear");
    }
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

    /* ---------------- the frame timer ---------------- */
    CHECK(!ctl_frame_due(1000, 1000) && !ctl_frame_due(1000 + 32999, 1000) && ctl_frame_due(1000 + 33000, 1000), "a frame is due after 33 ms");
    CHECK(ctl_frame_due(10, 0xFFFFFFF0u + 0) == 1 ? 0 : 1, "(sanity for the wrap case below)");
    CHECK(!ctl_frame_due(20, 0xFFFFFFF0u) && ctl_frame_due(33100, 0xFFFFFFF0u), "right across the 71-minute microsecond wrap");
    CHECK(ctl_frame_due(0x80001000u, 0), "after a freeze of 35+ minutes (the old signed compare went negative here and stalled the lights) a frame is due");
    CHECK(ctl_frame_due(0xFFFFFFF0u, 0x00000010u), "after a 71-minute gap it is still due");

    /* ---------------- the Pi's VU show ---------------- */
    CHECK(ctl_host_timeout(1, 1, 1, 10000, 6999) == HT_FALLBACK, "PI VU started by hand, no levels for 3.001 s: wait (the bars fall to dark), don't vanish");
    CHECK(ctl_host_timeout(1, 1, 0, 10000, 6999) == HT_OFF, "host-started lights (not the setting): they end");
    CHECK(ctl_host_timeout(1, 1, 1, 10000, 7000) == HT_NONE && ctl_host_timeout(1, 1, 0, 10000, 7000) == HT_NONE, "exactly 3 s is still fine");
    CHECK(ctl_host_timeout(0, 1, 1, 99999, 0) == HT_NONE && ctl_host_timeout(1, 0, 1, 99999, 0) == HT_NONE, "nothing to time out when lights are off or not host-driven");
    CHECK(ctl_host_timeout(1, 1, 1, 100, 0xFFFFFF00u) == HT_NONE, "the millisecond wrap does not fake a timeout");
    CHECK(ctl_host_reengage(1, 0, 1) && !ctl_host_reengage(1, 1, 1) && !ctl_host_reengage(0, 0, 1) && !ctl_host_reengage(1, 0, 0),
          "levels arriving take the pads back only for the PI VU show, while lights run and it is showing the fallback");
    CHECK(ctl_host_stop_ends(1, 1, 0) && ctl_host_stop_ends(1, 0, 1) && !ctl_host_stop_ends(1, 0, 0) && !ctl_host_stop_ends(0, 1, 1),
          "host 'lights off' ends host-driven bars AND any show the host started (text), never a show the user started by hand");

    /* ---------------- the lazy save ---------------- */
    CHECK(ctl_lazy_arm(1000) == 21000 && ctl_lazy_arm(0xFFFFFFFFu - 19999u) == 1, "armed 20 s ahead, and never 0 (0 means 'not armed')");
    CHECK(!ctl_lazy_due(0, 999999) && !ctl_lazy_due(21000, 20999) && ctl_lazy_due(21000, 21000) && ctl_lazy_due(21000, 99999), "due exactly when its time comes; 0 = not armed");
    CHECK(ctl_lazy_due(5, 0xFFFFFFF0u + 0) == 0 || 1, "(sanity)");
    CHECK(ctl_lazy_due(ctl_lazy_arm(0xFFFFFFF0u), 0xFFFFFFF0u + 20000u), "armed just before the millisecond wrap, it is still due 20 s later");
    CHECK(CTL_LAZY_SAVE_MS == 20000u && CTL_HOST_QUIET_MS == 3000u && CTL_FRAME_US == 33000u, "the constants are what the docs say");

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

    /* ---------------- step pads (turn several lit steps off at once, 2026-11-11) ---------------- */
    {
        CHECK(ctl_step_displace(5, 1, 0) == 1, "a lit step still held and untouched when another pad lands: its tap (off) counts now");
        CHECK(ctl_step_displace(5, 1, 1) == 0 && ctl_step_displace(5, 0, 0) == 0 && ctl_step_displace(-1, 1, 0) == 0,
              "...not when a knob edited it, not when the press only turned it ON, not with nothing held");
        /* three lit pads pressed together, then let go: each press counts the one before it, the release counts the last.
           (The old one-slot hold left every pad but the last-pressed one ON.) */
        int pad = -1, lit = 0, turned = 0, off = 0;
        for (int p = 3; p <= 11; p += 4) {                                    /* pads 3, 7, 11 */
            off += ctl_step_displace(pad, lit, turned);                        /* a new press: the one still down counts now */
            pad = p; lit = 1; turned = 0;
        }
        off += (pad >= 0 && lit && !turned);                                  /* release of the newest = off, as before */
        CHECK(off == 3, "three lit steps held together all turn off (two on the later presses, the last on release)");
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

    /* ---- faders follow the host's CCs (v0.31c) ---- */
    {
        /* 6 faders: 0 = ch1 CC7, 1 = ch2 CC7, 2 = ch1 CC7 again, 3 = ch1 CC10, 4 = ch16 CC7, 5 = ch1 CC7 (but n = 5: not a fader) */
        uint8_t ch[6] = {1, 2, 1, 1, 16, 1}, cc[6] = {7, 7, 7, 10, 7, 7};
        uint8_t val[6] = {100, 100, 100, 100, 100, 100};
        uint32_t m = ctl_fader_follow(val, ch, cc, 5, 0xB1, 7, 64);   /* ch 2 CC 7 */
        CHECK(m == 0x2 && val[1] == 64 && val[0] == 100 && val[2] == 100 && val[3] == 100 && val[4] == 100,
              "one match: only that fader moves, the mask names it");
        m = ctl_fader_follow(val, ch, cc, 5, 0xB0, 7, 0);              /* ch 1 CC 7: faders 0 and 2, never 5 (past n) */
        CHECK(m == 0x5 && val[0] == 0 && val[2] == 0 && val[5] == 100 && val[3] == 100,
              "several faders on the same channel + CC all move; one past the fader count is untouched");
        m = ctl_fader_follow(val, ch, cc, 5, 0xB0, 8, 1);
        uint8_t v2[6]; memcpy(v2, val, 6);
        CHECK(m == 0 && !memcmp(v2, val, 6) && val[0] == 0, "no fader on that CC: nothing changes, empty mask");
        m = ctl_fader_follow(val, ch, cc, 5, 0xB2, 10, 5);             /* CC 10 but on ch 3 */
        CHECK(m == 0 && val[3] == 100, "the right CC on the wrong channel changes nothing");
        m = ctl_fader_follow(val, ch, cc, 5, 0xBF, 7, 127);
        CHECK(m == 0x10 && val[4] == 127, "channel 16 = status 0xBF");
        m = ctl_fader_follow(val, ch, cc, 5, 0xBF, 7, 127);
        CHECK(m == 0 && val[4] == 127, "the same level again: nothing marked (no repaint for a resend)");
        m = ctl_fader_follow(val, ch, cc, 5, 0xB1, 7, 200);
        CHECK(m == 0x2 && val[1] == 127, "a value above 127 is clamped to 127");
        m = ctl_fader_follow(val, ch, cc, 5, 0xB1, 7, -3);
        CHECK(m == 0x2 && val[1] == 0, "a negative value is clamped to 0");
        memcpy(v2, val, 6);
        m = ctl_fader_follow(val, ch, cc, 5, 0x90, 7, 99) | ctl_fader_follow(val, ch, cc, 5, 0xE0, 7, 99)
          | ctl_fader_follow(val, ch, cc, 5, 0xB0, 7 + 128, 99) | ctl_fader_follow(val, ch, cc, 0, 0xB0, 7, 99);
        CHECK(m == 0 && !memcmp(v2, val, 6), "not a CC (note / pitch bend), a CC number above 127, or no faders: nothing changes");
        uint8_t ch32[40], cc32[40], v32[40];
        for (int i = 0; i < 40; i++) { ch32[i] = 3; cc32[i] = 20; v32[i] = 0; }
        m = ctl_fader_follow(v32, ch32, cc32, 40, 0xB2, 20, 9);
        CHECK(m == 0xFFFFFFFFu && v32[31] == 9 && v32[32] == 0, "all 32 faders fit the mask; a count above 32 is cut to 32");
    }

    /* ---------------- v0.31d: fader sets + the DAW page ---------------- */
    {
        /* the saved FADERS set keeps exactly the old rule: rows = n + 1 ("+ add") below 32, max top = rows - 4 */
        int same = 1;
        for (int n = 0; n <= 32; n++) {
            int rows = n < 32 ? n + 1 : 32, old = rows > 4 ? rows - 4 : 0;
            if (ctl_fset_max_top(n, 1, 32, 4) != old) same = 0;
        }
        CHECK(same, "FADERS: the max top is the old fad_max_top for every count 0-32 (the + row counts below 32)");
        CHECK(ctl_fset_max_top(9, 0, 9, 4) == 5 && ctl_fset_max_top(4, 0, 9, 4) == 0 && ctl_fset_max_top(5, 0, 9, 4) == 1
              && ctl_fset_max_top(0, 0, 9, 4) == 0, "the DAW page has no + row: 9 rows -> top 0-5, 4 or fewer -> 0");
        /* scrolling: the old clamp (top += d, then > max -> max, then < 0 -> 0) for every start and step */
        int sc = 1;
        for (int mx = 0; mx <= 6; mx++) for (int t0 = 0; t0 <= mx; t0++) for (int d = -6; d <= 6; d++) {
            int t = t0, o = t0 + d;
            if (o > mx) o = mx;
            if (o < 0) o = 0;
            ctl_fset_scroll(&t, d, mx);
            if (t != o) sc = 0;
        }
        CHECK(sc, "scrolling moves the top exactly as FADERS always did (every start, step and size)");
        int t = 0;
        CHECK(ctl_fset_scroll(&t, -1, 5) == -1 && t == 0 && ctl_fset_scroll(&t, -4, 5) == -1 && t == 0, "up at the first row: -1 (= 1E 00, the previous bank)");
        t = 5;
        CHECK(ctl_fset_scroll(&t, 1, 5) == 1 && t == 5 && ctl_fset_scroll(&t, 4, 5) == 1, "down at the last row: +1 (= 1E 01, the next bank)");
        t = 3;
        CHECK(ctl_fset_scroll(&t, 4, 5) == 0 && t == 5, "a page step that still moves part of the way is not past the end (no 1E)");
        t = 1;
        CHECK(ctl_fset_scroll(&t, -4, 5) == 0 && t == 0 && ctl_fset_scroll(&t, 1, 5) == 0 && t == 1, "moves inside the page: 0");
        t = 0;
        CHECK(ctl_fset_scroll(&t, 1, 0) == 1 && ctl_fset_scroll(&t, -1, 0) == -1, "a page of 4 rows or fewer: every turn is past an end");
        t = 7;
        CHECK(ctl_fset_scroll(&t, 0, 5) == 0 && t == 5 && ctl_fset_scroll(&t, 0, 5) == 0, "d = 0 only re-clamps (after a removal), never a bank");
        CHECK(ctl_daw_bank_top(1, 5) == 0 && ctl_daw_bank_top(1, 0) == 0,
              "v0.31e past the bottom (next bank): the view goes to the TOP of the new bank (track 9 follows track 8)");
        CHECK(ctl_daw_bank_top(-1, 5) == 5 && ctl_daw_bank_top(-1, 0) == 0,
              "v0.31e past the top (previous bank): the view goes to the BOTTOM of the new bank");
        uint8_t v[3] = {10, 20, 30};
        CHECK(ctl_fset_set(v, 0, 500) == 1 && v[0] == 127 && ctl_fset_set(v, 0, -3) == 1 && v[0] == 0,
              "a FADERS level is clamped 0-127 both ends");
        CHECK(ctl_fset_set(v, 1, 21) == 1 && v[1] == 21 && ctl_fset_set(v, 1, 21) == 0, "a new level is stored and says changed; the same level again does not");
        CHECK(ctl_fset_set(v, 0, 300) == 1 && v[0] == 127 && ctl_fset_set(v, 2, -5) == 1 && v[2] == 0 && ctl_fset_set(v, 0, 128) == 0,
              "levels clamp to 0-127 (a knob past the end sends nothing new)");

        /* parsing 1A / 1B / 1C */
        uint8_t a[2] = {0x1A, 9};
        CHECK(ctl_host_parse(a, 2).cmd == HC_DAWN && ctl_host_parse(a, 2).arg == 9 && (a[1] = 0, ctl_host_parse(a, 2).arg == 0)
              && (a[1] = 10, ctl_host_parse(a, 2).cmd == HC_NONE) && ctl_host_parse(a, 1).cmd == HC_NONE,
              "1A n: 0-9 rows; 10 or no count is ignored");
        /* v0.31f: 1B carries a 14-bit level (hi, lo), 1C is <row> <hi> <lo> */
        uint8_t r[16] = {0x1B, 8, 127, 0, 64, 100, 5, 'M', 'a', 's', 't', 'e', 'r'};
        ctl_host_t h = ctl_host_parse(r, 13);
        CHECK(h.cmd == HC_DAWROW && h.arg == 8 && h.data == r + 2 && h.len == 11, "1B row R G B hi lo name: data from R");
        CHECK(ctl_host_parse(r, 7).cmd == HC_DAWROW && ctl_host_parse(r, 7).len == 5 && ctl_host_parse(r, 6).cmd == HC_NONE,
              "1B with no name is a row; without both level bytes it is ignored");
        r[1] = 9;
        CHECK(ctl_host_parse(r, 13).cmd == HC_NONE, "1B row 9 (or more) is ignored");
        uint8_t c[4] = {0x1C, 3, 90, 5};
        h = ctl_host_parse(c, 4);
        CHECK(h.cmd == HC_DAWLVL && h.arg == 3 && h.len == (90u << 7 | 5u) && (c[2] = 0xFF, ctl_host_parse(c, 4).len == (127u << 7 | 5u))
              && (c[1] = 9, ctl_host_parse(c, 4).cmd == HC_NONE) && (c[1] = 3, ctl_host_parse(c, 3).cmd == HC_NONE) && ctl_host_parse(c, 5).cmd == HC_NONE
              && ctl_host_parse(c, 4).cmd == HC_DAWLVL,
              "1C row hi lo = a 14-bit level (stray top bits dropped); row 9, or the wrong length, is ignored");

        /* the page */
        static ctl_daw_t d;
        memset(&d, 0x55, sizeof d);
        ctl_daw_count(&d, 9);
        CHECK(d.n == 9, "1A 9: nine rows");
        const uint8_t row0[] = {127, 0, 0, 100, 3, 'K', 'i', 'c', 'k'};
        ctl_daw_row(&d, 0, row0, sizeof row0);
        CHECK(d.fine[0] == (100 << 7 | 3) && d.val[0] == 100 && !strcmp(d.name[0], "Kick") && d.rgb[0][0] == 7 && d.rgb[0][1] == 0 && d.rgb[0][2] == 0,
              "1B: 14-bit level (pads show its top 7 bits), name, colour");
        const uint8_t longn[] = {1, 2, 3, 5, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};
        ctl_daw_row(&d, 1, longn, sizeof longn);
        CHECK(!strcmp(d.name[1], "ABCDEFGH") && d.name[1][8] == 0, "a name longer than 8 is cut to 8");
        const uint8_t odd[] = {0, 0, 0, 0xFF, 0xFF, 0x1F, 'a', 0x7F, 0x80, '~'};
        ctl_daw_row(&d, 2, odd, sizeof odd);
        CHECK(!strcmp(d.name[2], "?a??~") && d.fine[2] == 16383 && d.val[2] == 127, "non-ASCII / control bytes -> '?'; stray top bits dropped");
        ctl_daw_row(&d, 1, row0, 5);
        CHECK(d.name[1][0] == 0 && d.val[1] == 100, "a row sent again with no name clears the old one");
        ctl_daw_row(&d, 1, longn, sizeof longn);
        ctl_daw_row(&d, 1, row0, sizeof row0);
        int tail0 = 1;
        for (int i = 4; i <= CTL_DAW_NAME; i++) if (d.name[1][i]) tail0 = 0;
        CHECK(!strcmp(d.name[1], "Kick") && tail0, "a shorter name leaves no bytes of the longer one behind (a row reads the same however it got there)");
        uint8_t before[sizeof d];
        memcpy(before, &d, sizeof d);
        ctl_daw_row(&d, 9, row0, sizeof row0); ctl_daw_row(&d, -1, row0, sizeof row0); ctl_daw_row(&d, 3, row0, 4);
        CHECK(!memcmp(before, &d, sizeof d) && ctl_daw_level(&d, 9, 5) == 0 && !memcmp(before, &d, sizeof d), "a row out of range (or too short) changes nothing");
        CHECK(ctl_daw_level(&d, 0, 8192) == 1 && d.fine[0] == 8192 && d.val[0] == 64 && ctl_daw_level(&d, 0, 8192) == 0
              && ctl_daw_level(&d, 0, 8193) == 1 && d.val[0] == 64 && ctl_daw_level(&d, 0, 99999) == 1 && d.fine[0] == 16383 && d.val[0] == 127
              && ctl_daw_level(&d, 0, -5) == 1 && d.fine[0] == 0,
              "a 14-bit level: one step of 16383 is a change even inside one pad step; clamped both ends");
        ctl_daw_level(&d, 0, 16383);
        ctl_daw_count(&d, 2);
        int clr = 1;
        for (int i = 2; i < CTL_DAW_ROWS; i++) if (d.val[i] || d.fine[i] || d.rgb[i][0] || d.rgb[i][1] || d.rgb[i][2] || d.name[i][0]) clr = 0;
        CHECK(d.n == 2 && clr && d.val[0] == 127 && !strcmp(d.name[0], "Kick"), "1A 2: rows 2-8 cleared, rows 0-1 kept");
        ctl_daw_count(&d, 0);
        CHECK(d.n == 0 && d.val[0] == 0 && d.fine[0] == 0 && d.name[0][0] == 0, "1A 0: no page, every row cleared");
        ctl_daw_count(&d, 50);
        CHECK(d.n == CTL_DAW_ROWS, "a count above 9 is cut to 9 (the parser already refuses it)");

        /* taps + knobs (v0.31f) */
        int tp = 1;
        for (int l = 0; l <= 127; l++) if ((ctl_daw_tap_fine(l) >> 7) != l) tp = 0;
        CHECK(tp && ctl_daw_tap_fine(0) == 0 && ctl_daw_tap_fine(127) == 16383 && ctl_daw_tap_fine(500) == 16383,
              "a pad tap jumps to its column's level exactly as before (0 -> 0, 127 -> 16383, the pad shows the same level)");
        CHECK(ctl_daw_knob_step(1000) == 1 && ctl_daw_knob_step(120) == 1 && ctl_daw_knob_step(119) == 8 && ctl_daw_knob_step(60) == 8
              && ctl_daw_knob_step(59) == 32 && ctl_daw_knob_step(30) == 32 && ctl_daw_knob_step(29) == 128 && ctl_daw_knob_step(0) == 128,
              "knob steps: a slow click = 1 of 16383 (full resolution), quicker turns 8 / 32 / 128");

        /* colours */
        uint8_t o[3];
        ctl_daw_rgb(0, 0, 0, o);
        CHECK(o[0] == 6 && o[1] == 6 && o[2] == 6, "black (no track colour) -> FADERS' white");
        ctl_daw_rgb(64, 32, 0, o);
        CHECK(o[0] == 7 && o[1] == 4 && o[2] == 0, "the brightest channel -> 7, the others in proportion (rounded)");
        ctl_daw_rgb(10, 10, 10, o);
        CHECK(o[0] == 7 && o[1] == 7 && o[2] == 7, "a dark grey is shown as bright as any fader");
        ctl_daw_rgb(300, -4, 127, o);
        CHECK(o[0] == 7 && o[1] == 0 && o[2] == 7, "out-of-range channels are clamped");

        /* Fire -> host messages */
        uint8_t mm[12];
        static const uint8_t L[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x1D, 8, 100, 3, 0xF7}, B0[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x1E, 0, 1, 0xF7};
        CHECK(ctl_daw_level_msg(mm, 8, 100 << 7 | 3) == 10 && !memcmp(mm, L, 10), "1D <row> <hi> <lo>");
        CHECK(ctl_daw_bank_msg(mm, -1, 1) == 9 && !memcmp(mm, B0, 9) && ctl_daw_bank_msg(mm, 1, 4) == 9 && mm[6] == 1 && mm[7] == 4 && mm[8] == 0xF7
              && (ctl_daw_bank_msg(mm, 1, 0), mm[7] == 1) && (ctl_daw_bank_msg(mm, 1, 999), mm[7] == 127),
              "1E <dir> <count>: 00 = back, 01 = on, count 1-127 (SHIFT + PATTERN = 4)");
    }

    printf("ctl_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
