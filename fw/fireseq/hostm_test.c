/* hostm_test: ORC mode's protocol (hostm.c), on the Pi:
 *   gcc -O2 -Wall -fsanitize=bounds -fsanitize-undefined-trap-on-error -I. -o hostm_test hostm_test.c hostm.c -lm
 * The screen frames are packed by an independent copy of the ENCODER the ORC uses (akai_fire_handler
 * protocol.plot), so the decoder is checked against the real format, not against itself. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hostm.h"
#include "fire_map.h"
#include "ledpat.h"

#define EXPECTED 31
static int pass, fail;
static void check(const char *name, int ok)
{
    if (ok) pass++; else fail++;
    if (!ok) printf("  FAIL  %s\n", name);
}

static uint8_t fb[1024];
static volatile uint8_t leds[LED_COUNT], fine[LED_COUNT];
static int flags;

static void feed(const uint8_t *b, int n) { for (int i = 0; i < n; i++) flags |= hm_byte(b[i]); }

/* ---- the ORC's encoder (akai_fire_handler/protocol.py: BITMUTATE + plot), full panel ---- */
static const int BITMUTATE[8][7] = {
    {13, 19, 25, 31, 37, 43, 49}, {0, 20, 26, 32, 38, 44, 50}, {1, 7, 27, 33, 39, 45, 51},
    {2, 8, 14, 34, 40, 46, 52},   {3, 9, 15, 21, 41, 47, 53},  {4, 10, 16, 22, 28, 48, 54},
    {5, 11, 17, 23, 29, 35, 55},  {6, 12, 18, 24, 30, 36, 42},
};
static uint8_t msg[1300];
static int msg_len;

static void build_oled(const uint8_t img[64][128], int realtime)
{
    uint8_t buf[1175];
    memset(buf, 0, sizeof buf);
    buf[0] = 0; buf[1] = 7; buf[2] = 0; buf[3] = 127;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 128; x++)
            if (img[y][x]) {
                int X = x + 128 * (y / 8), Y = y % 8, rb = BITMUTATE[Y][X % 7];
                buf[4 + (X / 7) * 8 + rb / 7] |= (uint8_t)(1 << (rb % 7));
            }
    int n = 0;
    uint8_t hdr[] = {0xF0, 0x47, 0x7F, 0x43, 0x0E, (1175 >> 7) & 0x7F, 1175 & 0x7F};
    memcpy(msg, hdr, sizeof hdr); n = sizeof hdr;
    for (int i = 0; i < 1175; i++) {
        if (realtime && i % 97 == 5 && n < (int)sizeof msg - 2) msg[n++] = 0xF8;   /* clock inside a SysEx */
        msg[n++] = buf[i];
    }
    msg[n++] = 0xF7;
    msg_len = n;
}

static int fb_matches(const uint8_t img[64][128])
{
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 128; x++)
            if (((fb[(y / 8) * 128 + x] >> (y % 8)) & 1) != (img[y][x] != 0)) return 0;
    return 1;
}

static uint8_t img[64][128];

int main(void)
{
    hm_init(fb, leds, fine, LED_FRAMES);
    srand(1234);

    /* ---- the screen ---- */
    int ok = 1;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 7; x++) ok &= HM_INV[BITMUTATE[y][x]] == (y << 3 | x);
    check("the flash copy of the turned-round packing table matches the ORC's table", ok);
    ok = 1;
    for (int t = 0; t < 200 && ok; t++) {
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 128; x++) img[y][x] = (uint8_t)(rand() % (t % 4 + 2) == 0);
        memset(fb, t & 1 ? 0xFF : 0x00, sizeof fb);   /* every pixel must be SET or CLEARED */
        build_oled(img, t & 2);
        flags = 0;
        feed(msg, msg_len);
        ok = fb_matches(img) && flags == HM_OLED_DONE;
    }
    check("screen: 200 random frames decode exactly (over a lit and a dark screen, clock bytes inside)", ok);

    memset(img, 0, sizeof img);
    img[0][0] = img[63][127] = img[7][6] = img[8][7] = 1;
    memset(fb, 0, sizeof fb); build_oled(img, 0); feed(msg, msg_len);
    check("screen: corners and the 7-column / 8-row group edges land on the right pixels",
          fb[0] == 1 && (fb[7 * 128 + 127] >> 7 & 1) && (fb[6] >> 7 & 1) && (fb[128 + 7] & 1)
          && fb_matches(img));

    memset(fb, 0xAA, sizeof fb);
    build_oled(img, 0);
    msg[10] = 0x90;                                 /* a note-on status byte mid-frame */
    flags = 0; feed(msg, msg_len);
    check("screen: a status byte aborts the frame: no flush is asked for", !(flags & HM_OLED_DONE));

    {   /* a small window: bands 2-3, columns 10-40; nothing outside it may change */
        uint8_t m[] = {0xF0, 0x47, 0x7F, 0x43, 0x0E, 0, 4 + 16, 2, 3, 10, 40,
                       0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
                       0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0xF7};
        memset(fb, 0, sizeof fb); flags = 0; feed(m, sizeof m);
        int outside = 0, inside = 0;
        for (int p = 0; p < 8; p++)
            for (int x = 0; x < 128; x++) {
                int in = p >= 2 && p <= 3 && x >= 10 && x <= 40;
                if (!in && fb[p * 128 + x]) outside++;
                if (in && fb[p * 128 + x]) inside++;
            }
        check("screen window: pixels land only inside bands 2-3 x columns 10-40", outside == 0 && inside > 0);
        check("... and it asks for a flush", flags == HM_OLED_DONE);
    }
    {   /* a window that makes no sense is ignored */
        uint8_t m[] = {0xF0, 0x47, 0x7F, 0x43, 0x0E, 0, 4 + 8, 0, 7, 50, 10,
                       0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0xF7};
        memset(fb, 0, sizeof fb); flags = 0; feed(m, sizeof m);
        int any = 0;
        for (int i = 0; i < 1024; i++) any |= fb[i];
        check("screen: a backwards window (columns 50..10) draws nothing and asks no flush", !any && !flags);
    }
    {   /* a frame cut off before its end, then reset: the next frame decodes from a clean start */
        memset(img, 0, sizeof img); img[5][5] = 1;
        build_oled(img, 0);
        feed(msg, 300);
        hm_reset();
        memset(fb, 0, sizeof fb); flags = 0; feed(msg, msg_len);
        check("hm_reset forgets a half-received frame", fb_matches(img) && flags == HM_OLED_DONE);
    }

    /* ---- pads: a colour value -> the two slots' levels (v0.34) ---- */
    hm_set_gamma(20);
    {
        uint8_t m, f;
        hm_duty(0, &m, &f);
        int z = m == 0 && f == 0;
        hm_duty(127, &m, &f);
        check("0 = dark in both slots, 127 = full in both", z && m == LED_FRAMES && f == LED_FRAMES);
        hm_duty(1, &m, &f);
        int tiny = m == 0 && f == 0;                /* (1 / 127) squared is far under half the smallest step */
        hm_duty(8, &m, &f);
        check("nothing is forced on: 1 is dark, and the first values that reach a step light the SHORT slot only (8 -> 0 long, 1 short)",
              tiny && m == 0 && f == 1);
    }
    ok = 1;
    int steps_max = 0;
    for (int w = 12; w <= 64; w += 4)
        for (int g = 10; g <= 30; g++) {
            hm_set_weight(w); hm_set_gamma(g);
            long prev = -1;
            int distinct = 0;
            for (int v = 0; v < 128; v++) {
                uint8_t m, f;
                hm_duty((uint8_t)v, &m, &f);
                long got = (long)m * (256 - w) + (long)f * w;          /* units per cycle the two levels really give */
                double x = v / 127.0, want = 0;
                if (g <= 20) want = x * (1 - (g - 10) / 10.0) + x * x * ((g - 10) / 10.0);
                else want = x * x * (1 - (g - 20) / 10.0) + x * x * x * ((g - 20) / 10.0);
                want *= LED_FRAMES * 256.0;
                ok &= m <= LED_FRAMES && f <= LED_FRAMES;
                ok &= got >= prev;                                      /* brighter in = never dimmer out */
                ok &= got - want <= w / 2.0 + 2.5 && want - got <= w + 2.5;   /* never more than half a short step over, one under (+ the 15-bit rounding) */
                if (got != prev) distinct++;
                prev = got;
            }
        }
    check("every curve 1.0-3.0 x every slot share 12-64: never above 21, never down as the value goes up, within one short step of the brightness wanted", ok);
    hm_set_weight(HM_W_DEFAULT); hm_set_gamma(20);
    {
        long prev = -1;
        for (int v = 0; v < 128; v++) {
            uint8_t m, f;
            hm_duty((uint8_t)v, &m, &f);
            long got = (long)m * (256 - HM_W_DEFAULT) + (long)f * HM_W_DEFAULT;
            if (got != prev) steps_max++;
            prev = got;
        }
    }
    check("at the default curve the 128 values land on over 90 different brightnesses (the 22-level driver gave them 22)", steps_max > 90);
    {
        uint8_t m1, f1, m2, f2;
        hm_set_gamma(1); hm_duty(64, &m1, &f1);
        hm_set_gamma(10); hm_duty(64, &m2, &f2);
        ok = m1 == m2 && f1 == f2;
        hm_set_gamma(99); hm_duty(64, &m1, &f1);
        hm_set_gamma(30); hm_duty(64, &m2, &f2);
        ok &= m1 == m2 && f1 == f2;
        check("a curve outside 10-30 is clamped", ok);
        hm_set_weight(1); ok = hm_weight() == 12;
        hm_set_weight(200); ok &= hm_weight() == 64;
        hm_set_weight(HM_W_DEFAULT); ok &= hm_weight() == HM_W_DEFAULT;
        check("the short slot's share is clamped to 12-64", ok);
    }
    hm_set_gamma(20);

    memset((void *)leds, 0, sizeof leds);
    {
        uint8_t m[] = {0xF0, 0x47, 0x7F, 0x43, 0x65, 0, 8,
                       0, 127, 0, 0,                /* pad 0 = top-left: red */
                       63, 0, 64, 127,              /* pad 63 = bottom-right: half green, full blue */
                       0xF7};
        flags = 0; feed(m, sizeof m);
        int others = 0;
        for (int i = 0; i < LED_COUNT; i++) {
            if (i == led_pad(0, 0, 0) || i == led_pad(3, 15, 1) || i == led_pad(3, 15, 2)) continue;
            others += leds[i] != 0;
        }
        uint8_t gm, gf;
        hm_duty(64, &gm, &gf);
        check("pads: top-left red and bottom-right green/blue land on their LEDs, in BOTH slots (green 64 = 6 long + 1 short at the measured share, 34 / 256)",
              leds[led_pad(0, 0, 0)] == LED_FRAMES && fine[led_pad(0, 0, 0)] == LED_FRAMES
              && leds[led_pad(3, 15, 1)] == gm && fine[led_pad(3, 15, 1)] == gf && gm == 6 && gf == 1
              && leds[led_pad(3, 15, 2)] == LED_FRAMES && fine[led_pad(3, 15, 2)] == LED_FRAMES);
        for (int i = 0; i < LED_COUNT; i++) {
            if (i == led_pad(0, 0, 0) || i == led_pad(3, 15, 1) || i == led_pad(3, 15, 2)) continue;
            others += fine[i] != 0;
        }
        check("pads: nothing else lights, and no flush is asked for", others == 0 && flags == 0);
    }
    {
        uint8_t m[] = {0xF0, 0x47, 0x7F, 0x43, 0x65, 0, 4, 17, 0, 127, 0, 5, 127, 127, 127, 0xF7};
        feed(m, sizeof m);
        check("pads: bytes beyond the stated length are ignored (pad 5 untouched)",
              leds[led_pad(1, 1, 1)] == LED_FRAMES && leds[led_pad(0, 5, 0)] == 0);
    }
    {   /* every pad index reaches a distinct LED triple, in row-major order from the top-left */
        int seen[LED_COUNT] = {0}, dup = 0;
        for (int p = 0; p < 64; p++)
            for (int j = 0; j < 3; j++) dup += seen[led_pad(p / 16, p % 16, j)]++;
        check("pads: the 64 x 3 colour LEDs are all distinct", dup == 0);
    }
    {
        uint8_t m[] = {0xF0, 0x00, 0x20, 0x29, 0x65, 0, 4, 0, 0, 0, 127, 0xF7};   /* not Akai's header */
        uint8_t o[] = {0xF0, 0x7D, 0x4F, 0x52, 0x43, 0x65, 0, 4, 0, 0, 0, 127, 0xF7};   /* ours: handled elsewhere */
        memset((void *)leds, 0, sizeof leds); flags = 0; feed(m, sizeof m); feed(o, sizeof o);
        int any = 0;
        for (int i = 0; i < LED_COUNT; i++) any |= leds[i];
        check("another maker's SysEx (or our own 7D) with the same command byte changes nothing", !any && !flags);
    }
    {   /* the reboot-to-DFU message belongs to the main collector, not to this parser */
        uint8_t dfu[] = {0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7};
        flags = 0; feed(dfu, sizeof dfu);
        check("Akai's other commands (72 = DFU) are skipped here without a flag", flags == 0);
    }

    /* ---- buttons: Akai's resting look ---- */
    memset((void *)leds, 0, sizeof leds); memset((void *)fine, 0, sizeof fine);
    hm_buttons();
    {
        int same = 1;
        for (int i = 0; i < LED_COUNT; i++) same &= leds[i] == fine[i];
        check("buttons: both slots carry the same level (so a button is exactly as bright as before v0.34)", same);
    }
    check("buttons: ALT, PATTERN, BROWSER, GRID and CHANNEL bright; STEP..REC soft; MUTE rings soft green",
          leds[LED_ALT_Y] == LED_FRAMES && leds[LED_PAT_UP] == LED_FRAMES && leds[LED_BROWSER] == LED_FRAMES
          && leds[LED_GRID_R] == LED_FRAMES && leds[LED_CHANNEL] == LED_FRAMES && leds[LED_STEP_Y] == HM_BTN_SOFT
          && leds[LED_REC_Y] == HM_BTN_SOFT && leds[LED_ROW_G[3]] == HM_BTN_SOFT && leds[LED_MIXER] == 0 && leds[LED_STEP_R] == 0);
    {
        int padlit = 0;
        for (int i = 0; i < 192; i++) padlit |= leds[i];
        check("buttons: the resting look lights no pad", !padlit);
    }

    /* ---- inputs ---- */
    {
        int seen[128] = {0}, ok2 = 1;
        uint8_t m[2];
        for (int idx = 0; idx < 64; idx++) {
            ok2 &= hm_input(idx | 0x80, m) == 1 && m[1] == 100;
            seen[m[0]]++;
        }
        for (int n = 54; n <= 117; n++) ok2 &= seen[n] == 1;
        check("pads: the 64 pads are notes 54-117, each exactly once, velocity 100", ok2);
        hm_input(in_pad(0, 0) | 0x80, m); int tl = m[0];
        hm_input(in_pad(3, 15) | 0x80, m); int br = m[0];
        hm_input(in_pad(1, 2) | 0x80, m); int mid = m[0];
        check("pads: top-left = 54, bottom-right = 117, row 1 col 2 = 72 (the ORC's layout)",
              tl == 54 && br == 117 && mid == 54 + 18);
        hm_input(in_pad(1, 2), m);
        check("pads: let go = the same note, velocity 0", m[0] == 72 && m[1] == 0);
    }
    {   /* the ORC's BUTTON_NOTE table (orc1_fire.py, measured on Akai's firmware) */
        struct { int in, note; } T[] = {
            {IN_STEP, 44}, {IN_NOTE, 45}, {IN_DRUM, 46}, {IN_PERFORM, 47}, {IN_SHIFT, 48}, {IN_ALT, 49},
            {IN_PATSONG, 50}, {IN_PLAY, 51}, {IN_STOP, 52}, {IN_REC, 53}, {IN_PAT_UP, 31},
            {IN_PAT_DN, 32}, {IN_BROWSER, 33}, {IN_GRID_L, 34}, {IN_GRID_R, 35}, {IN_SELECT, 25},
            {IN_KNOBMODE, 26}, {IN_ROW1, 36}, {IN_ROW2, 37}, {IN_ROW3, 38}, {IN_ROW4, 39},
        };
        int ok2 = 1;
        uint8_t m[2];
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            ok2 &= hm_input(T[i].in | 0x80, m) == 1 && m[0] == T[i].note && m[1] == 127;
            ok2 &= hm_input(T[i].in, m) == 1 && m[0] == T[i].note && m[1] == 0;
        }
        check("buttons: all 21 are Akai's note numbers (rows 1-4 = 36-39, KNOB MODE = 26), 127 down / 0 up", ok2);
        check("inputs with no meaning (69-71) send nothing",
              hm_input(69 | 0x80, m) == 0 && hm_input(70 | 0x80, m) == 0 && hm_input(71 | 0x80, m) == 0);
    }
    check("relative turns: +1 -> 1, -1 -> 127, +-100 clamp to 63 / 65",
          hm_rel(1) == 1 && hm_rel(-1) == 127 && hm_rel(100) == 63 && hm_rel(-100) == 65);

    /* ---- the way out, and the keep-alive ---- */
    check("SHIFT + BROWSER pressed = leave; BROWSER alone, its release, or another button with SHIFT = not",
          hm_exit(IN_BROWSER | 0x80, 1) && !hm_exit(IN_BROWSER | 0x80, 0) && !hm_exit(IN_BROWSER, 1)
          && !hm_exit(IN_STEP | 0x80, 1) && !hm_exit(in_pad(0, 0) | 0x80, 1));
    check("quiet host: not before 5 s, at 5 s, and right across the millisecond counter's wrap",
          !hm_quiet(4999, 0) && hm_quiet(5000, 0) && !hm_quiet(100u, 0xFFFFFFFFu - 1000u) && hm_quiet(4500u, 0xFFFFFFFFu - 1000u));

    printf("hostm_test: %d passed, %d failed (expected %d)\n", pass, fail, EXPECTED);
    return (fail || pass + fail != EXPECTED) ? 1 : 0;
}
