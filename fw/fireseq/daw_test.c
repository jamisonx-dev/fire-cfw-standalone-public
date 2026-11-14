/* daw_test: DAW mode's protocol translator (daw.c), on the Pi:
 *   gcc -O2 -Wall -Wextra -I. -fsanitize=bounds -fsanitize-undefined-trap-on-error -o daw_test daw_test.c daw.c
 * Every expected byte below is typed in from the DAW-mode research notes (kept outside this repo) (sections 1, 2, 5.6), not copied from
 * daw.c's tables, so a wrong table entry turns this red. dawp_mutate.py breaks each rule in turn. */
#include <stdio.h>
#include <string.h>
#include "daw.h"

#define EXPECTED 74
static int pass, fail;
static void check(const char *name, int ok)
{
    if (ok) pass++; else fail++;
    if (!ok) printf("  FAIL  %s\n", name);
}

/* ---- what the module sent: every message, back to back, and where each starts ---- */
static uint8_t out[4096];
static int on, nmsg, mstart[512], mlen[512];
void daw_out(const uint8_t *m, int n)
{
    if (nmsg < 512) { mstart[nmsg] = on; mlen[nmsg] = n; nmsg++; }
    for (int i = 0; i < n && on < (int)sizeof out; i++) out[on++] = m[i];
}
static void clr(void) { on = nmsg = 0; }
static int sent(const uint8_t *m, int n) { return on == n && !memcmp(out, m, (size_t)n); }

/* ---- feeding bytes: the events that came back ---- */
static daw_ev_t evs[256];
static int nev;
static void feed(const uint8_t *b, int n)
{
    nev = 0;
    for (int i = 0; i < n; i++) {
        daw_ev_t e;
        if (daw_byte(b[i], &e) && nev < 256) evs[nev++] = e;
    }
}
#define FEED(...) do { static const uint8_t _b[] = {__VA_ARGS__}; feed(_b, sizeof _b); } while (0)
#define SENT(...) ({ static const uint8_t _m[] = {__VA_ARGS__}; sent(_m, sizeof _m); })
static int ev1(int type, int idx, int val) { return nev == 1 && evs[0].type == type && evs[0].idx == idx && evs[0].val == val; }

int main(void)
{
    int ok;
    printf("daw_state_t = %u bytes\n", (unsigned)sizeof(daw_state_t));
    check("RAM: the whole state is under 200 bytes", sizeof(daw_state_t) <= 200);

    /* ================= Auto: lock to whichever host speaks first ================= */
    daw_init(DAW_AUTO); clr();
    FEED(0xB0, 0x50, 0x05, 0xD0, 0x10, 0xA0, 0x05, 0x10, 0x80, 0x00, 0x00, 0x90, 0x00, 0x00);
    ok = nev == 0 && daw_proto() == DAW_AUTO;
    ok &= !daw_fader(0, 1000, 0) && !daw_button(DB_PLAY, 1) && !daw_vpot(0, 1) && !daw_jog(1) && !daw_bank(1)
          && !daw_announce() && on == 0;
    check("Auto, only messages either protocol could send (and ONE 90 00 00): nothing decoded, nothing sent", ok);

    clr(); FEED(0x90, 0x00, 0x00);
    check("Auto: a SECOND 90 00 00 in a row = a HUI ping: locks HUI, answered with exactly 90 00 7F",
          daw_proto() == DAW_HUI && SENT(0x90, 0x00, 0x7F) && nmsg == 1 && ev1(DE_PING, 0, 0));

    daw_init(DAW_AUTO); clr(); FEED(0x90, 0x00, 0x00, 0xD0, 0x10, 0xB0, 0x50, 0x01, 0x90, 0x00, 0x00);
    check("Auto: two 90 00 00 with other (unclaimed) messages between are NOT in a row: no HUI lock, no answer",
          daw_proto() == DAW_AUTO && on == 0);
    /* v0.36b (REAPER, 2026-11-13): a DAW that is already running only sends changes; any MCU-only message locks */
    daw_init(DAW_AUTO); clr(); FEED(0xE2, 0x00, 0x40);
    check("Auto: a pitch bend locks MCU and is decoded as that strip's level",
          daw_proto() == DAW_MCU && nev == 1 && evs[0].type == DE_LEVEL && evs[0].idx == 2 && evs[0].val == 0x2000 && on == 0);
    daw_init(DAW_AUTO); FEED(0x90, 0x10, 0x7F);
    check("Auto: an MCU light (note 10 = MUTE 1 on) locks MCU and lights it", daw_proto() == DAW_MCU && daw_led(DB_MUTE) == 2);
    daw_init(DAW_AUTO); FEED(0x90, 0x00, 0x00, 0x90, 0x01, 0x00, 0x90, 0x00, 0x00);
    check("Auto: MCU's 'REC 1 off' among other MCU lights locks MCU, never HUI (no ping answer)",
          daw_proto() == DAW_MCU);
    daw_init(DAW_AUTO); clr(); FEED(0x90, 0x00, 0x7F, 0x90, 0x00, 0x7F);
    check("Auto: 90 00 7F (MCU's REC 1 on) twice is never a HUI ping: locks MCU, no answer",
          daw_proto() == DAW_MCU && on == 0 && daw_led(DB_REC) == 2);
    daw_init(DAW_AUTO); FEED(0xB0, 0x40, 0x31);
    check("Auto: an MCU time-display CC locks MCU", daw_proto() == DAW_MCU);
    daw_init(DAW_AUTO); FEED(0xB0, 0x0C, 0x00, 0xB0, 0x2C, 0x47);
    check("Auto: a HUI light (CC 0C zone, 2C port) locks HUI and lights it", daw_proto() == DAW_HUI && daw_led(DB_REC) == 2);
    daw_init(DAW_AUTO); FEED(0xB0, 0x03, 0x40, 0xB0, 0x23, 0x00);
    check("Auto: a HUI fader (CC 03 + 23) locks HUI", daw_proto() == DAW_HUI);
    daw_init(DAW_AUTO); FEED(0xB0, 0x0C, 0x00, 0xB0, 0x2C, 0x47);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 'A', 0xF7);
    check("Auto: locked HUI, an MCU SysEx re-locks MCU and forgets HUI's lights",
          daw_proto() == DAW_MCU && daw_led(DB_REC) == 0 && daw_text()[0] == 'A');
    daw_init(DAW_HUI); FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 'A', 0xF7, 0xE0, 0x00, 0x40);
    check("forced HUI: an MCU SysEx or pitch bend changes nothing", daw_proto() == DAW_HUI && daw_text()[0] == ' ');

    daw_init(DAW_AUTO); clr();
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14);
    check("Auto: F0 00 00 66 14 (MCU) locks MCU as soon as the device id arrives",
          daw_proto() == DAW_MCU && ev1(DE_LOCK, DAW_MCU, 0) && on == 0);

    daw_init(DAW_AUTO); clr();
    FEED(0xF0, 0x00, 0x00, 0x66, 0x10, 0x13, 0x00, 0xF7);
    check("Auto: Logic Control's id 10 locks MCU too, and the reply carries 10",
          daw_proto() == DAW_MCU && SENT(0xF0, 0x00, 0x00, 0x66, 0x10, 0x14, 'V', '1', '.', '0', '0', 0xF7));

    daw_init(DAW_AUTO); clr();
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x00, 'K', 'i', 'c', 'k', 0xF7);
    check("Auto: a HUI SysEx (device 05) locks HUI", daw_proto() == DAW_HUI && on == 0);

    daw_init(DAW_AUTO); clr();
    FEED(0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7, 0xF0, 0x47, 0x7F, 0x43, 0x65, 0x00, 0x00, 0xF7,
         0xF0, 0x00, 0x00, 0x67, 0x14, 0x00, 0xF7, 0xF0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xF7);
    check("Auto: other SysEx (identity request, Akai, another maker, an MCU extender's id 15) does not lock",
          daw_proto() == DAW_AUTO && on == 0 && nev == 0);

    daw_init(DAW_AUTO); FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0xF7); clr();
    FEED(0x90, 0x00, 0x7F); FEED(0x90, 0x00, 0x00);
    check("MCU locked: 90 00 00 is REC 1's light going off, not a ping (no answer)",
          daw_proto() == DAW_MCU && on == 0 && ev1(DE_LED, DB_REC, 0));

    daw_init(DAW_HUI); clr();   /* v0.36b: in Auto an MCU SysEx re-locks (tested above); a FORCED HUI never answers MCU */
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7, 0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7);
    check("forced HUI: an MCU device query / version request gets no answer and HUI stays",
          daw_proto() == DAW_HUI && on == 0 && nev == 0);

    daw_init(DAW_MCU); clr(); FEED(0x90, 0x00, 0x00);
    ok = daw_proto() == DAW_MCU && on == 0;
    daw_init(DAW_HUI); clr(); FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7);
    ok &= daw_proto() == DAW_HUI && on == 0;
    check("forced MCU ignores a HUI ping; forced HUI ignores an MCU query", ok);

    daw_init(DAW_MCU); FEED(0x90, 0x10, 0x7F); clr();
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x63, 0xF7, 0xF0, 0x00, 0x00, 0x66, 0x14, 0x62, 0xF7,
         0xF0, 0x00, 0x00, 0x66, 0x14, 0x61, 0xF7, 0xF0, 0x00, 0x00, 0x66, 0x14, 0x0B, 0x0F, 0xF7,
         0xF0, 0x00, 0x00, 0x66, 0x14, 0x20, 0x00, 0x03, 0xF7, 0xF0, 0x00, 0x00, 0x66, 0x14, 0x21, 0x01, 0xF7);
    check("MCU: reset / all-lights-off / faders-down / backlight / meter-mode SysEx are ignored (nothing sent, nothing changed)",
          on == 0 && nev == 0 && daw_led(DB_MUTE) == 2);

    /* ================= MCU handshake ================= */
    daw_init(DAW_AUTO); clr();
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7);
    check("MCU device query (00) -> 01 connection query: serial FLAME01 + a 4-byte challenge",
          SENT(0xF0, 0x00, 0x00, 0x66, 0x14, 0x01, 'F', 'L', 'A', 'M', 'E', '0', '1', 0x01, 0x02, 0x03, 0x04, 0xF7)
          && nmsg == 1 && nev == 2 && evs[1].type == DE_HELLO && evs[1].idx == 0x00);
    clr();
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x02, 'F', 'L', 'A', 'M', 'E', '0', '1', 0x11, 0x22, 0x33, 0x44, 0xF7);
    check("MCU host reply (02) -> 03 confirmation with the serial",
          SENT(0xF0, 0x00, 0x00, 0x66, 0x14, 0x03, 'F', 'L', 'A', 'M', 'E', '0', '1', 0xF7) && ev1(DE_HELLO, 0x02, 0));
    clr(); FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xF7);
    check("MCU version request (13) -> 14 + 5 ASCII",
          SENT(0xF0, 0x00, 0x00, 0x66, 0x14, 0x14, 'V', '1', '.', '0', '0', 0xF7) && ev1(DE_HELLO, 0x13, 0));
    clr(); FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0x90, 0xF7);
    check("a status byte inside a SysEx drops it: no answer", on == 0);
    clr(); FEED(0xF0, 0x00, 0xF8, 0x00, 0x66, 0x14, 0xFE, 0x13, 0x00, 0xF8, 0xF7);
    check("clock / active-sensing bytes inside a SysEx are skipped: still answered",
          SENT(0xF0, 0x00, 0x00, 0x66, 0x14, 0x14, 'V', '1', '.', '0', '0', 0xF7));

    daw_init(DAW_MCU); clr();
    ok = daw_announce() && SENT(0xF0, 0x00, 0x00, 0x66, 0x14, 0x01, 'F', 'L', 'A', 'M', 'E', '0', '1', 1, 2, 3, 4, 0xF7);
    daw_init(DAW_HUI); clr();
    ok &= !daw_announce() && on == 0;
    check("announce: MCU sends its 01 connection query; HUI sends nothing", ok);

    /* ================= MCU in ================= */
    daw_init(DAW_MCU);
    ok = 1;
    for (int s = 0; s < 9; s++) {
        uint8_t m[3] = {(uint8_t)(0xE0 | s), (uint8_t)(s * 7 + 3), (uint8_t)(100 - s)};
        feed(m, 3);
        ok &= ev1(DE_LEVEL, s, (100 - s) << 7 | (s * 7 + 3));
    }
    FEED(0xE9, 0x10, 0x10);
    check("MCU faders: pitch bend channel 1-9 = strips 1-8 + master, 14-bit (LSB first); channel 10 ignored",
          ok && nev == 0);
    FEED(0xE2, 0x7F, 0x7F, 0x00, 0x00, 0x01, 0x40);
    check("MCU: running status (one E2, three levels) and the ends 0 / 16383",
          nev == 3 && evs[0].val == 16383 && evs[1].val == 0 && evs[2].val == (0x40 << 7 | 1) && evs[2].idx == 2);
    FEED(0xE3, 0x01, 0xF8, 0x02);
    check("a clock byte inside a pitch bend is skipped", ev1(DE_LEVEL, 3, 2 << 7 | 1));

    ok = 1;
    for (int n = 0; n < 40; n++) {
        uint8_t m[3] = {0x90, (uint8_t)n, 0x7F};
        feed(m, 3);
        ok &= ev1(DE_LED, n, 2) && daw_led(n) == 2;
    }
    check("MCU lights: notes 0-39 = REC / SOLO / MUTE / SELECT / V-pot push of strips 1-8 (id = note)",
          ok && DB_REC == 0 && DB_SOLO == 8 && DB_MUTE == 16 && DB_SELECT == 24 && DB_VSEL == 32);
    FEED(0x90, 0x08, 0x01); ok = ev1(DE_LED, DB_SOLO, 1) && daw_led(DB_SOLO) == 1;
    FEED(0x90, 0x08, 0x00); ok &= ev1(DE_LED, DB_SOLO, 0) && daw_led(DB_SOLO) == 0;
    FEED(0x90, 0x09, 0x01, 0x80, 0x09, 0x40); ok &= nev == 2 && daw_led(DB_SOLO + 1) == 0;
    FEED(0x91, 0x0A, 0x7F); ok &= nev == 0 && daw_led(DB_SOLO + 2) == 2;    /* (on from the loop above) */
    check("MCU lights: velocity 127 on, 1 blink, 0 off, a note-off = off, channel 2 ignored", ok);

    {   /* the research's MCU note table, typed in again (section 1.4) */
        struct { int id, note; } T[] = {
            {DB_BANK_L, 46}, {DB_BANK_R, 47}, {DB_CH_L, 48}, {DB_CH_R, 49}, {DB_REW, 91}, {DB_FF, 92}, {DB_STOP, 93},
            {DB_PLAY, 94}, {DB_RECORD, 95}, {DB_UP, 96}, {DB_DOWN, 97}, {DB_LEFT, 98}, {DB_RIGHT, 99}, {DB_ZOOM, 100},
            {DB_SCRUB, 101}, {DB_LOOP, 86}, {DB_SHIFT, 70}, {DB_OPTION, 71}, {DB_CTRL, 72}, {DB_ALT, 73},
            {DB_SAVE, 80}, {DB_UNDO, 81}, {DB_AREAD, 74}, {DB_AWRITE, 75}, {DB_ATRIM, 76}, {DB_ATOUCH, 77},
            {DB_ALATCH, 78}, {DB_CLICK, 89}, {DB_FLIP, 50}, {DB_NAMEVAL, 52}, {DB_SMPTEBEATS, 53}, {DB_MARKER, 84},
            {DB_F1, 54}, {DB_F1 + 1, 55}, {DB_F1 + 7, 61}, {DB_LED_SMPTE, 113}, {DB_LED_BEATS, 114},
            {DB_LED_RUDE, 115}, {DB_A_TRACK, 40}, {DB_A_SEND, 41}, {DB_A_PAN, 42}, {DB_A_PLUGIN, 43}, {DB_A_EQ, 44},
        };
        ok = 1;
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            uint8_t m[3] = {0x90, (uint8_t)T[i].note, 0x7F};
            feed(m, 3);
            ok &= ev1(DE_LED, T[i].id, 2);
        }
        check("MCU lights: every non-strip note in research 1.4 lands on its button", ok);
        int dup = 0;
        for (int i = 0; i < DB_COUNT - DB_GLOBAL; i++)
            for (int j = i + 1; j < DB_COUNT - DB_GLOBAL; j++)
                dup |= (DAW_MCU_NOTE[i] != 0xFF && DAW_MCU_NOTE[i] == DAW_MCU_NOTE[j])
                     | (DAW_HUI_ZP[i] != 0xFF && DAW_HUI_ZP[i] == DAW_HUI_ZP[j]);
        for (int i = 0; i < DB_COUNT - DB_GLOBAL; i++) dup |= DAW_MCU_NOTE[i] < 40 || (DAW_HUI_ZP[i] != 0xFF && DAW_HUI_ZP[i] >> 3 < 8);
        check("tables: no MCU note or HUI zone/port is used twice, none overlaps the strips", !dup);
        clr();
        ok = 1;
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            int lit_only = T[i].id >= DB_LED_SMPTE && T[i].id <= DB_LED_RUDE;
            clr();
            int r = daw_button(T[i].id, 1);
            ok &= lit_only ? r == 0 && on == 0 : r == 1 && on == 3 && out[0] == 0x90 && out[1] == T[i].note && out[2] == 0x7F;
        }
        check("MCU buttons out: every non-strip button = 90 <its note> 7F; the three lights-only ids send nothing", ok);
    }
    clr();
    ok = daw_button(DB_MUTE + 3, 1) && SENT(0x90, 19, 0x7F);
    clr(); ok &= daw_button(DB_MUTE + 3, 0) && SENT(0x90, 19, 0x00);
    clr(); ok &= daw_button(DB_VSEL + 7, 1) && SENT(0x90, 39, 0x7F);
    clr(); ok &= !daw_button(DB_AUTO, 1) && !daw_button(DB_INSERT + 7, 1) && !daw_button(DB_COUNT, 1) && on == 0;
    check("MCU buttons out: strip button = 90 <note> 7F press / 00 release; HUI-only AUTO / INSERT send nothing", ok);

    memset(daw_st.text, '.', DAW_TEXT);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 'K', 'i', 'c', 'k', 0xF7);
    ok = !memcmp(daw_text(), "Kick...", 7) && ev1(DE_TEXT, 0, 4);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 56 + 7, '-', '6', '.', '0', 0x01, 0xF7);
    ok &= !memcmp(daw_text() + 63, "-6.0 ", 5) && daw_text()[62] == '.' && ev1(DE_TEXT, 63, 68);
    check("MCU LCD: 12 <offset> <text> lands at the offset (56 = bottom line); a control code shows as a space", ok);
    {
        uint8_t m[7 + 20 + 1] = {0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 105};
        for (int i = 0; i < 20; i++) m[7 + i] = 'Z';
        m[27] = 0xF7;
        memset(daw_st.text, '.', DAW_TEXT);
        feed(m, sizeof m);
        int z = 0;
        for (int i = 0; i < DAW_TEXT; i++) z += daw_text()[i] == 'Z';
        check("MCU LCD: text running past position 111 stops there (7 written, nothing beyond)",
              z == 7 && daw_text()[111] == 'Z' && daw_text()[112] == '.' && ev1(DE_TEXT, 105, 112));
    }
    FEED(0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00, 0xF7);
    check("MCU LCD: a message with no characters is no event", nev == 0);

    memset(daw_st.digit, ' ', DAW_DIGITS);
    FEED(0xB0, 64, 0x30, 0xB0, 65, 0x39 | 0x40, 0xBF, 73, 0x01, 0xB0, 75, 0x20, 0xB0, 76, 0x31);
    ok = nev == 4 && daw_st.digit[0] == '0' && daw_st.digit[1] == ('9' | 0x80) && daw_st.digit[9] == 'A'
         && daw_st.digit[11] == ' ' && evs[0].type == DE_DIGIT && evs[0].idx == 0 && evs[0].val == 1 && evs[2].idx == 9;
    check("MCU time display: CC 64-75 = digits right to left, bit 6 = the dot, 01 = 'A', channel 16 too, CC 76 not", ok);

    FEED(0xB0, 48, 0x46, 0xB0, 55, 0x31, 0xB0, 56, 0x11);
    check("MCU V-pot rings: CC 48-55 = pots 1-8, the byte kept as sent; CC 56 not",
          nev == 2 && evs[0].type == DE_RING && evs[0].idx == 0 && evs[0].val == 0x46 && daw_ring(7) == 0x31);

    /* ================= MCU out ================= */
    daw_init(DAW_MCU); clr();
    ok = daw_fader(2, 0x1234, 1000) && nmsg == 2 && SENT(0x90, 106, 0x7F, 0xE2, 0x34, 0x24);
    clr(); ok &= daw_fader(2, 0x0100, 1200) && SENT(0xE2, 0x00, 0x02);
    check("MCU fader: touch on (note 104 + strip, 7F) BEFORE the first level, pitch bend LSB first, no second touch", ok);
    clr(); daw_tick(1699);
    ok = on == 0;
    daw_tick(1700);
    check("MCU fader: let go (note 106, 00) only once 500 ms pass with no new level", ok && SENT(0x90, 106, 0x00));
    clr(); daw_tick(5000);
    check("... and only once", on == 0);
    clr(); daw_fader(8, 99999, 0);
    ok = SENT(0x90, 112, 0x7F, 0xE8, 0x7F, 0x7F);
    clr(); daw_fader(8, -5, 1);
    ok &= SENT(0xE8, 0x00, 0x00);
    clr(); ok &= !daw_fader(9, 100, 1) && !daw_fader(-1, 100, 1) && on == 0;
    check("MCU master fader = touch note 112 + pitch bend channel 9; levels clamp to 0-16383; strip 9 does not exist", ok);

    daw_release_all(); daw_fader(0, 10, 0); daw_fader(5, 10, 0); clr();
    ok = daw_bank(1) && SENT(0x90, 104, 0x00, 0x90, 109, 0x00, 0x90, 47, 0x7F, 0x90, 47, 0x00);
    clr(); ok &= daw_channel(-1) && SENT(0x90, 48, 0x7F, 0x90, 48, 0x00);
    clr(); ok &= !daw_bank(0) && on == 0;
    check("MCU bank right = touched faders let go first, then note 47 press + release; channel left = note 48", ok);

    clr();
    ok = daw_vpot(0, 1) && SENT(0xB0, 16, 0x01);
    clr(); ok &= daw_vpot(7, -1) && SENT(0xB0, 23, 0x41);
    clr(); ok &= daw_vpot(3, 100) && SENT(0xB0, 19, 63);
    clr(); ok &= daw_vpot(3, -100) && SENT(0xB0, 19, 0x40 | 63);
    clr(); ok &= !daw_vpot(3, 0) && !daw_vpot(8, 1) && on == 0;
    check("MCU V-pots: CC 16-23, clockwise = n, counter = 40 + n, at most 63 a message", ok);
    clr(); ok = daw_jog(1) && SENT(0xB0, 60, 0x01);
    clr(); ok &= daw_jog(-1) && SENT(0xB0, 60, 0x41);
    check("MCU jog wheel: CC 60, 01 = +1, 41 = -1", ok);

    /* ================= HUI in ================= */
    daw_init(DAW_HUI); clr();
    FEED(0x90, 0x00, 0x00, 0x90, 0x00, 0x00);
    check("HUI: every ping is answered (two pings, two 90 00 7F)", SENT(0x90, 0, 0x7F, 0x90, 0, 0x7F) && nev == 2);

    FEED(0xB0, 0x03, 0x55, 0xB0, 0x23, 0x2A);
    ok = nev == 2 && evs[0].type == DE_LEVEL && evs[0].idx == 3 && evs[0].val == 0x55 << 7
         && evs[1].idx == 3 && evs[1].val == (0x55 << 7 | 0x2A);
    FEED(0xB0, 0x04, 0x10, 0x25, 0x01);
    ok &= nev == 1 && evs[0].idx == 4;
    FEED(0xB0, 0x07, 0x7F, 0x27, 0x7F, 0x00, 0x01, 0x20, 0x02);
    ok &= nev == 4 && evs[1].val == 16383 && evs[2].idx == 0 && evs[3].val == (1 << 7 | 2);
    check("HUI faders: CC 0-7 = top 7 bits, then CC 20-27 = bottom 7; a bottom half for another strip is ignored", ok);
    FEED(0xB0, 0x08, 0x10, 0xB0, 0x28, 0x10);
    check("HUI: there is no fader 9 (CC 08 / 28 are not a level)", nev == 0);

    ok = 1;
    for (int z = 0; z < 8; z++) {
        static const int G[7][2] = {{DB_REC, 7}, {DB_SOLO, 3}, {DB_MUTE, 2}, {DB_SELECT, 1}, {DB_VSEL, 5},
                                    {DB_AUTO, 4}, {DB_INSERT, 6}};
        for (int g = 0; g < 7; g++) {
            uint8_t m[6] = {0xB0, 0x0C, (uint8_t)z, 0xB0, 0x2C, (uint8_t)(0x40 | G[g][1])};
            feed(m, 6);
            ok &= ev1(DE_LED, G[g][0] + z, 2) && daw_led(G[g][0] + z) == 2;
            m[5] = (uint8_t)G[g][1];
            feed(m + 3, 3);
            ok &= ev1(DE_LED, G[g][0] + z, 0);
        }
    }
    check("HUI strip lights: zone = strip, port 7 rec, 3 solo, 2 mute, 1 select, 5 v-sel, 4 auto, 6 insert; 40 = on", ok);
    FEED(0xB0, 0x0C, 0x02, 0xB0, 0x2C, 0x40);
    check("HUI: port 0 (fader touch) has no light", nev == 0);
    FEED(0xB0, 0x0C, 0x1E, 0xB0, 0x2C, 0x41, 0xB0, 0x0C, 0x2A, 0xB0, 0x2C, 0x43);
    check("HUI: a zone past 1D lights nothing (2A would wrap onto bank right's 0A)", nev == 0);

    {   /* the research's HUI zone / port table, typed in again (sections 2.1, 2.7) */
        struct { int id, zone, port; } T[] = {
            {DB_CH_L, 0x0A, 0}, {DB_BANK_L, 0x0A, 1}, {DB_CH_R, 0x0A, 2}, {DB_BANK_R, 0x0A, 3},
            {DB_REW, 0x0E, 1}, {DB_FF, 0x0E, 2}, {DB_STOP, 0x0E, 3}, {DB_PLAY, 0x0E, 4}, {DB_RECORD, 0x0E, 5},
            {DB_DOWN, 0x0D, 0}, {DB_LEFT, 0x0D, 1}, {DB_RIGHT, 0x0D, 3}, {DB_UP, 0x0D, 4}, {DB_SCRUB, 0x0D, 5},
            {DB_LOOP, 0x0F, 3}, {DB_CTRL, 0x08, 0}, {DB_SHIFT, 0x08, 1}, {DB_UNDO, 0x08, 3}, {DB_ALT, 0x08, 4},
            {DB_OPTION, 0x08, 5}, {DB_SAVE, 0x08, 7}, {DB_ATRIM, 0x18, 0}, {DB_ALATCH, 0x18, 1},
            {DB_AREAD, 0x18, 2}, {DB_AWRITE, 0x18, 4}, {DB_ATOUCH, 0x18, 5}, {DB_CLICK, 0x1D, 2},
            {DB_F1, 0x1B, 0}, {DB_F1 + 3, 0x1B, 3}, {DB_F1 + 7, 0x1B, 7}, {DB_LED_SMPTE, 0x16, 0},
            {DB_LED_BEATS, 0x16, 2}, {DB_LED_RUDE, 0x16, 3}, {DB_A_PAN, 0x0B, 2}, {DB_A_SEND, 0x0B, 7},
        };
        ok = 1;
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            uint8_t m[6] = {0xB0, 0x0C, (uint8_t)T[i].zone, 0xB0, 0x2C, (uint8_t)(0x40 | T[i].port)};
            feed(m, 6);
            ok &= ev1(DE_LED, T[i].id, 2);
        }
        check("HUI lights: every zone / port in research 2.1 lands on its button", ok);
        ok = 1;
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            int lit_only = T[i].id >= DB_LED_SMPTE && T[i].id <= DB_LED_RUDE;
            clr();
            int r = daw_button(T[i].id, 1);
            uint8_t want[6] = {0xB0, 0x0F, (uint8_t)T[i].zone, 0xB0, 0x2F, (uint8_t)(0x40 | T[i].port)};
            ok &= lit_only ? r == 0 && on == 0 : r == 1 && nmsg == 2 && sent(want, 6);
        }
        check("HUI buttons out: B0 0F <zone> then B0 2F 4<port>, for every non-strip button; lights-only send nothing", ok);
        clr();
        ok = !daw_button(DB_ZOOM, 1) && !daw_button(DB_FLIP, 1) && !daw_button(DB_A_TRACK, 1) && on == 0;
        check("HUI: buttons HUI does not have (zoom, flip, assign track) send nothing", ok);
    }

    FEED(0xB0, 0x10, 0x45, 0xB0, 0x17, 0x2B, 0xB0, 0x18, 0x01);
    check("HUI V-pot rings: CC 10-17 = pots 1-8 (18 and up not kept)",
          nev == 2 && evs[0].type == DE_RING && evs[0].idx == 0 && daw_ring(0) == 0x45 && daw_ring(7) == 0x2B);

    memset(daw_st.text, '.', DAW_TEXT);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x02, 'B', 'a', 's', 's', 0xF7);
    ok = !memcmp(daw_text() + 80 + 8, "Bass", 4) && daw_text()[87] == '.' && daw_text()[92] == '.' && ev1(DE_TEXT, 88, 92);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x08, 'P', 'a', 'n', ' ', 0xF7);
    ok &= !memcmp(daw_text() + 112, "Pan ", 4);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x09, 'X', 'X', 'X', 'X', 0xF7,
         0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x7F, 'X', 'X', 'X', 'X', 0xF7);
    ok &= nev == 0 && daw_text()[111] == '.' && daw_text()[76] == '.';
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x01, 'L', 'o', 'n', 'g', 'E', 'r', 0xF7);
    ok &= !memcmp(daw_text() + 84, "Long", 4) && daw_text()[88] == 'B';
    check("HUI scribble strips: 10 <ch> <4 chars> at 80 + ch x 4; ch 8 = the assign display; ch 9 and a 5th char rejected", ok);

    memset(daw_st.text, '.', DAW_TEXT);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x12,
         0x00, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
         0x07, 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 0xF7);
    ok = !memcmp(daw_text(), "ABCDEFGHIJ.", 11) && !memcmp(daw_text() + 70, "klmnopqrst", 10) && daw_text()[69] == '.'
         && ev1(DE_TEXT, 0, 80);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x12, 0x05, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 0xF7);
    ok &= !memcmp(daw_text() + 50, "0123456789", 10) && ev1(DE_TEXT, 50, 60);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x12, 0x08, 'n', 'o', 0xF7);
    ok &= nev == 0;
    check("HUI main display: 12 { <zone> <10 chars> }..., zone 0-7 = row zone/4, column (zone%4) x 10; zone 8 rejected", ok);

    memset(daw_st.digit, ' ', DAW_DIGITS);
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x11, 0x04, 0x12, 0x09, 0x05, 0x00, 0x00, 0x01, 0x0F, 0xF7);
    ok = !memcmp(daw_st.digit, "4\xb2" "95" "001 ", 8) && daw_st.digit[8] == ' ' && ev1(DE_DIGIT, 0, 8);
    check("HUI timecode: 11 <8 digits, frames units first>; 10 = the decimal point; a value past 9 is blank", ok);
    clr();
    FEED(0xF0, 0x00, 0x00, 0x66, 0x05, 0x01, 0x10, 0x00, 'n', 'o', 'p', 'e', 0xF7,
         0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x30, 0x01, 0xF7);
    check("HUI: a SysEx without the 00 after the id, or a command it does not know, is ignored",
          nev == 0 && daw_text()[80] != 'n' && on == 0);

    /* ================= HUI out ================= */
    daw_init(DAW_HUI); clr();
    ok = daw_button(DB_REC + 3, 1) && SENT(0xB0, 0x0F, 0x03, 0xB0, 0x2F, 0x47) && nmsg == 2;
    clr(); ok &= daw_button(DB_REC + 3, 0) && SENT(0xB0, 0x0F, 0x03, 0xB0, 0x2F, 0x07);
    clr(); ok &= daw_button(DB_SOLO + 0, 1) && SENT(0xB0, 0x0F, 0x00, 0xB0, 0x2F, 0x43);
    clr(); ok &= daw_button(DB_MUTE + 7, 1) && SENT(0xB0, 0x0F, 0x07, 0xB0, 0x2F, 0x42);
    clr(); ok &= daw_button(DB_SELECT + 5, 1) && SENT(0xB0, 0x0F, 0x05, 0xB0, 0x2F, 0x41);
    clr(); ok &= daw_button(DB_VSEL + 1, 1) && SENT(0xB0, 0x0F, 0x01, 0xB0, 0x2F, 0x45);
    clr(); ok &= daw_button(DB_INSERT + 2, 1) && SENT(0xB0, 0x0F, 0x02, 0xB0, 0x2F, 0x46);
    clr(); ok &= daw_button(DB_PLAY, 1) && SENT(0xB0, 0x0F, 0x0E, 0xB0, 0x2F, 0x44);
    check("HUI strip buttons out: zone = strip; press = 2F 40 + port, release = 2F port (REC 7, SOLO 3, MUTE 2, SEL 1)", ok);

    clr();
    ok = daw_fader(4, 0x1234, 50) && nmsg == 4
         && SENT(0xB0, 0x0F, 0x04, 0xB0, 0x2F, 0x40, 0xB0, 0x04, 0x24, 0xB0, 0x24, 0x34);
    clr(); ok &= daw_fader(4, 0x3FFF, 60) && SENT(0xB0, 0x04, 0x7F, 0xB0, 0x24, 0x7F);
    clr(); daw_tick(559); ok &= on == 0;
    daw_tick(560); ok &= SENT(0xB0, 0x0F, 0x04, 0xB0, 0x2F, 0x00);
    check("HUI fader: touch (zone strip, port 0 on) BEFORE the level, CC strip = top, CC 20+strip = bottom; let go after 500 ms", ok);
    clr();
    check("HUI: no master fader (strip 8 sends nothing)", !daw_fader(8, 100, 0) && on == 0);

    clr();
    ok = daw_vpot(0, 1) && SENT(0xB0, 0x40, 0x41);
    clr(); ok &= daw_vpot(7, -1) && SENT(0xB0, 0x47, 0x01);
    clr(); ok &= daw_vpot(2, 5) && SENT(0xB0, 0x42, 0x45);
    clr(); ok &= daw_vpot(2, -200) && SENT(0xB0, 0x42, 63);
    check("HUI V-pots: CC 40-47, clockwise = 40 + n, counter = n (the other way round from MCU)", ok);
    clr(); ok = daw_jog(2) && SENT(0xB0, 0x0D, 0x42);
    clr(); ok &= daw_jog(-3) && SENT(0xB0, 0x0D, 0x03);
    check("HUI jog: CC 0D with the same direction bit", ok);

    clr(); daw_fader(1, 5, 0); clr();
    ok = daw_bank(-1) && SENT(0xB0, 0x0F, 0x01, 0xB0, 0x2F, 0x00,
                              0xB0, 0x0F, 0x0A, 0xB0, 0x2F, 0x41, 0xB0, 0x0F, 0x0A, 0xB0, 0x2F, 0x01);
    clr(); ok &= daw_channel(1) && SENT(0xB0, 0x0F, 0x0A, 0xB0, 0x2F, 0x42, 0xB0, 0x0F, 0x0A, 0xB0, 0x2F, 0x02);
    check("HUI bank left = touch released first, then zone 0A port 1 press + release; channel right = port 2", ok);

    /* ================= touch timing, reset ================= */
    daw_init(DAW_MCU); clr();
    daw_fader(0, 1, 0xFFFFFF00u); clr();
    daw_tick(0xFFFFFF00u + 499);
    ok = on == 0;
    daw_tick(0x000000F4u);                          /* 500 ms later, across the 32-bit wrap */
    check("touch timer: right across the millisecond counter's wrap", ok && SENT(0x90, 104, 0x00));
    daw_fader(0, 1, 100); daw_fader(1, 1, 300); clr();
    daw_tick(600);
    ok = SENT(0x90, 104, 0x00);
    clr(); daw_tick(799); ok &= on == 0;
    daw_tick(800); ok &= SENT(0x90, 105, 0x00);
    check("touch timer: each strip has its own (strip 1 let go at 600, strip 2 at 800)", ok);
    daw_fader(3, 1, 0); daw_fader(8, 1, 0); clr();
    daw_release_all();
    check("release all: every touched strip, once", SENT(0x90, 107, 0x00, 0x90, 112, 0x00) && (clr(), daw_release_all(), on == 0));

    daw_init(DAW_AUTO); FEED(0x90, 0x00, 0x00, 0xB0, 0x0C, 0x00, 0xB0, 0x2C, 0x47, 0xB0, 0x10, 0x33,
                             0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, 0x00, 'D', 'r', 'u', 'm', 0xF7);
    ok = daw_led(DB_REC) == 2 && daw_ring(0) == 0x33 && daw_text()[80] == 'D';
    daw_init(DAW_AUTO);
    ok &= daw_proto() == DAW_AUTO && daw_led(DB_REC) == 0 && daw_ring(0) == 0 && daw_text()[80] == ' '
          && daw_st.digit[0] == ' ' && daw_st.touched == 0;
    check("init: unlocks Auto, clears lights, rings, text, digits", ok);

    daw_init(DAW_HUI); clr();
    FEED(0xB0, 0x2C, 0x41);
    check("HUI: a port before any zone was selected lights nothing", nev == 0);
    daw_init(DAW_HUI); clr();
    FEED(0x05, 0x06, 0xB0, 0x0C);
    check("data bytes with no status, and a message cut short, do nothing", nev == 0 && on == 0);

    printf("daw_test: %d passed, %d failed (expected %d)\n", pass, fail, EXPECTED);
    return (fail || pass + fail != EXPECTED) ? 1 : 0;
}
