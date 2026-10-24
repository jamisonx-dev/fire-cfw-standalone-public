/* Host test for lights.c: prints every pad of every time-driven mode at a set of times, and checks the
 * data-driven bars and the level quantiser. The effects themselves are compared against the ORC's
 * Python (lights_vs_orc.py) by reading this program's output.
 *   gcc -O1 -Wall -I. lights_test.c lights.c -lm -o lights_test && ./lights_test dump | python3 lights_vs_orc.py <orc1_fire.py> */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lights.h"

static int fails, checks;
#define EXPECTED 1441                              /* the number of checks a full run makes: a drift fails the run */
static float hue_of(const uint8_t *c)                    /* 0..1 */
{
    float r = c[0] / 255.0f, g = c[1] / 255.0f, b = c[2] / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn, h;
    if (d < 1e-6f) return 0.0f;
    if (mx == r) h = fmodf((g - b) / d, 6.0f); else if (mx == g) h = (b - r) / d + 2.0f; else h = (r - g) / d + 4.0f;
    h /= 6.0f;
    return h < 0 ? h + 1.0f : h;
}
#define ON(a) ((a)[0] + (a)[1] + (a)[2] > 0)
#define GREY(a) ((a)[0] > 0 && (a)[0] == (a)[1] && (a)[1] == (a)[2])   /* a peak dot (bars are always coloured) */
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(int argc, char **argv)
{
    static const float TS[] = {0.0f, 0.37f, 1.9f, 7.3f, 33.1f, 250.6f, 3000.2f};
    uint8_t out[64][3];
    if (argc > 1 && !strcmp(argv[1], "dump")) {
        for (int m = 0; m < LT_FX; m++)
            if (m != 11)
            for (unsigned k = 0; k < sizeof TS / sizeof TS[0]; k++) {
                lights_cols(m, TS[k], 0, 16, out);
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 16; c++)
                        printf("%d %.2f %d %d %d %d %d\n", m, TS[k], r, c, out[r * 16 + c][0], out[r * 16 + c][1], out[r * 16 + c][2]);
            }
        return 0;
    }

    /* slicing: drawing a frame in 4 column groups == drawing it whole */
    static uint8_t whole[64][3], part[64][3];
    for (int m = 0; m < LT_FX; m++) {
        lights_cols(m, 12.34f, 0, 16, whole);
        memset(part, 0xEE, sizeof part);
        for (int s = 0; s < 4; s++) lights_cols(m, 12.34f, s * 4, s * 4 + 4, part);
        CHECK(!memcmp(whole, part, sizeof whole), "sliced frame equals whole frame");
    }
    /* no effect is a blank screen over a spread of times (sparkle may be sparse but not empty overall) */
    for (int m = 0; m < LT_FX; m++) {
        if (m == 11) continue;                          /* (the removed fake VU) */
        long lit = 0;
        for (int k = 0; k < 40; k++) {
            lights_cols(m, 0.9f * (float)k, 0, 16, whole);
            for (int i = 0; i < 64; i++) lit += whole[i][0] + whole[i][1] + whole[i][2] > 0;
        }
        CHECK(lit > 40, "effect lights something");
    }

    /* quantiser: monotone, 0 -> 0, 255 -> 7, smooth averages to the value */
    CHECK(lights_level(0, 0, 0, 0, 1) == 0 && lights_level(255, 1, 1, 3, 0) == 7 && lights_level(255, 1, 1, 3, 1) == 7, "ends");
    int prev = 0;
    for (int v = 0; v <= 255; v++) { int L = lights_level(v, 2, 3, 0, 0); CHECK(L >= prev, "monotone"); prev = L; }
    for (int v = 8; v < 250; v += 17) {                 /* across the pads, the dithered average = (v/255)^2 * 7 */
        double sum = 0;
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) sum += lights_level(v, r, c, 0, 1);
        double want = ((double)v / 255.0) * ((double)v / 255.0) * 7.0, got = sum / 16;
        CHECK(fabs(got - want) < 0.3, "smooth quantiser keeps the average brightness across pads");
        /* and NEVER flickers: one pad, one value, the same level on every frame (a 30 Hz flip was a strobe) */
        for (int f = 1; f < 64; f++)
            CHECK(lights_level(v, 1, 2, (unsigned)f, 1) == lights_level(v, 1, 2, 0, 1), "no frame-to-frame flicker");
    }

    /* HITS: a hit lifts its column, bars fall to nothing, accents are taller than soft */
    lights_clear_bars();
    lights_hit(3, 3); lights_hit(5, 1); lights_hit(21, 2);   /* 21 folds to column 5 */
    lights_step(0.0f);                                   /* the lit-pad counts update once a frame */
    lights_cols(LT_M_HITS, 0, 0, 16, whole);
    int lit3 = 0, lit5 = 0;
    for (int r = 0; r < 4; r++) { lit3 += ON(whole[r * 16 + 3]); lit5 += ON(whole[r * 16 + 5]); }
    CHECK(lit3 == 4, "accent fills the column");
    CHECK(lit5 >= 3 && lit5 < 4, "normal hit (folded onto soft) is shorter than an accent");
    for (int i = 0; i < 100; i++) lights_step(0.05f);
    lights_cols(LT_M_HITS, 0, 0, 16, whole);
    long any = 0; for (int i = 0; i < 64; i++) any += whole[i][0] + whole[i][1] + whole[i][2];
    CHECK(any == 0, "bars fall to dark");
    lights_hit(-1, 3); lights_hit(32, 3); lights_hit(0, 0);   /* ignored */
    lights_cols(LT_M_HITS, 0, 0, 16, whole);
    any = 0; for (int i = 0; i < 64; i++) any += whole[i][0] + whole[i][1] + whole[i][2];
    CHECK(any == 0, "out-of-range hits are ignored");

    /* HOST: levels set bars; a lower level does not pull a bar down early; they fall */
    uint8_t lv[16] = {127, 64, 0, 127, 10};
    lights_clear_bars();
    for (int i = 0; i < 30; i++) { lights_host(lv); lights_step(0.033f); }   /* a second of the host streaming: the bars get there */
    lights_cols(LT_M_HOST, 0, 0, 16, whole);
    int c0 = 0, c1 = 0, c2 = 0;
    for (int r = 0; r < 4; r++) { c0 += ON(whole[r * 16 + 0]); c1 += ON(whole[r * 16 + 1]); c2 += ON(whole[r * 16 + 2]); }
    CHECK(c0 == 4 && c1 >= 2 && c1 < 4 && c2 == 0, "host levels map to bar heights");
    for (int i = 0; i < 100; i++) lights_step(0.05f);
    lights_cols(LT_M_HOST, 0, 0, 16, whole);
    any = 0; for (int i = 0; i < 64; i++) any += whole[i][0] + whole[i][1] + whole[i][2];
    CHECK(any == 0, "host bars fall to dark (the host went quiet: its last level is let go)");

    /* SNAPPY (look 0, the default): a level goes straight to the bar, the bar falls in a steady line, the top pad is partly lit */
    {
        uint8_t full[16], none[16], mid[16];
        memset(full, 127, sizeof full); memset(none, 0, sizeof none); memset(mid, 0, sizeof mid); mid[0] = 70;   /* 0.55 = 2.2 pads */
        lights_set_bar_style(0);
        lights_clear_bars();
        lights_host(full);
        lights_step(0.033f);
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        int rows = 0; for (int r = 0; r < 4; r++) rows += ON(whole[r * 16 + 0]);
        CHECK(rows == 4, "snappy: a sudden full level fills the bar at once (no following)");
        for (int i = 0; i < 15; i++) { lights_host(none); lights_step(0.033f); }     /* ~0.5 s: a straight line down at 1.2/s */
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(ON(whole[3 * 16 + 0]) && ON(whole[2 * 16 + 0]) && !ON(whole[1 * 16 + 0]),
              "snappy: the bar falls in a steady line (0.5 s after a full bar it is at ~40%: the bottom 2 pads; the peak dot hangs above)");
        CHECK(ON(whole[0 * 16 + 0]), "snappy: ...and the peak dot is still up at the top, falling more slowly than the bar");
        lights_clear_bars();
        for (int i = 0; i < 10; i++) { lights_host(mid); lights_step(0.0f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        rows = 0; for (int r = 0; r < 4; r++) rows += ON(whole[r * 16 + 0]);
        CHECK(rows == 3, "snappy: a bar at 2.2 pads lights the partly covered third pad");
        int top_v = 0, bot_v = 0;
        for (int j = 0; j < 3; j++) { if (whole[1 * 16 + 0][j] > top_v) top_v = whole[1 * 16 + 0][j]; if (whole[3 * 16 + 0][j] > bot_v) bot_v = whole[3 * 16 + 0][j]; }
        CHECK(top_v < bot_v, "...and that partly covered pad is dimmer than a fully covered one");
        lights_set_bar_style(2);
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(mid); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        rows = 0; for (int r = 0; r < 4; r++) rows += ON(whole[r * 16 + 0]);
        CHECK(rows == 2, "whole: the same 2.2-pad bar lights only 2 whole pads");
        static uint8_t g4[64][3], g99[64][3];
        lights_set_bar_style(4);
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(mid); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, g4);
        lights_set_bar_style(99);
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(mid); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, g99);
        CHECK(!memcmp(g4, g99, sizeof g4), "an out-of-range look is treated as the last one (heat smooth)");
        lights_set_bar_style(-5);
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(mid); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, g99);
        lights_set_bar_style(0);
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(mid); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, g4);
        CHECK(!memcmp(g4, g99, sizeof g4), "a negative look is treated as the first (snappy)");
    }

    /* SMOOTH (look 1): the bar FOLLOWS the level: no jump to a new reading, no ratchet, a peak dot that waits before it falls */
    lights_set_bar_style(1);
    {
        uint8_t full[16], none[16];
        memset(full, 127, sizeof full); memset(none, 0, sizeof none);
        lights_clear_bars();
        lights_host(full);
        lights_step(0.033f);
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        int rows = 0; for (int r = 0; r < 4; r++) rows += ON(whole[r * 16 + 0]);
        CHECK(rows >= 1 && rows < 4, "a sudden full level is followed, not jumped to (one frame later the bar is partway)");
        for (int i = 0; i < 30; i++) { lights_host(full); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        rows = 0; for (int r = 0; r < 4; r++) rows += ON(whole[r * 16 + 0]);
        CHECK(rows == 4, "...and a second later it is full");
        lights_host(none);
        lights_step(0.033f);
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        rows = 0; for (int r = 0; r < 4; r++) rows += ON(whole[r * 16 + 0]);
        CHECK(rows >= 3, "a sudden drop is followed too: one frame later the bar has only started down");
        for (int i = 0; i < 15; i++) { lights_host(none); lights_step(0.033f); }    /* ~0.5 s later */
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(ON(whole[0 * 16 + 0]), "the peak dot is still at the top 0.5 s after the drop (it holds 0.4 s first)");
        for (int i = 0; i < 45; i++) { lights_host(none); lights_step(0.033f); }    /* ~2.0 s after the drop */
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(!ON(whole[0 * 16 + 0]) && ON(whole[1 * 16 + 0]) + ON(whole[2 * 16 + 0]) + ON(whole[3 * 16 + 0]) >= 1,
              "...and by 2 s it has fallen a pad or two but is still showing lower down");
        for (int i = 0; i < 300; i++) { lights_host(none); lights_step(0.033f); }   /* ~10 s */
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        any = 0; for (int i = 0; i < 64; i++) any += ON(whole[i]);
        CHECK(any == 0, "...and long after, bar and peak are gone");
    }

    /* WHOLE (look 2): a bar looks the same at any height that lights the same number of pads (no half-lit top pad) */
    lights_set_bar_style(2);
    {
        uint8_t a[64][3], b[64][3], lvA[16], lvB[16];
        memset(lvA, 0, sizeof lvA); memset(lvB, 0, sizeof lvB);
        lvA[0] = 70; lvB[0] = 78;                        /* 0.55 and 0.61 of full: both 2 pads */
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(lvA); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, a);
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(lvB); lights_step(0.033f); }
        lights_cols(LT_M_HOST, 0, 0, 16, b);
        CHECK(!memcmp(a, b, sizeof a), "bars are whole pads: two heights that light the same pads look identical");
        int lit_rows = 0; for (int r = 0; r < 4; r++) lit_rows += ON(a[r * 16 + 0]);
        CHECK(lit_rows == 2, "...and that is 2 pads for a bar at 0.55-0.61");
    }

    /* hysteresis: pad 2 turns on at 1.65 pads and off only below 1.35, so a bar that settles in between keeps its state */
    {
        uint8_t lv[16], z[64][3];
        #define ROWS_AT(level, frames) ({ memset(lv, 0, sizeof lv); lv[0] = (level);             for (int i_ = 0; i_ < (frames); i_++) { lights_host(lv); lights_step(0.033f); }             lights_cols(LT_M_HOST, 0, 0, 1, z); int r_ = 0; for (int k_ = 0; k_ < 4; k_++) r_ += ON(z[k_ * 16 + 0]); r_; })
        lights_clear_bars();
        CHECK(ROWS_AT(48, 60) == 1, "a bar rising to 1.5 pads (under the 1.65 turn-on) shows 1 pad");
        CHECK(ROWS_AT(57, 60) == 2, "...rising to 1.8 pads it shows 2");
        CHECK(ROWS_AT(48, 60) == 2, "...settling back to 1.5 pads (above the 1.35 turn-off) it KEEPS 2: no chatter on a boundary");
        CHECK(ROWS_AT(46, 60) == 2, "...and still 2 at 1.45 pads (the turn-off is a whole 0.3 pad below the turn-on, not half way)");
        CHECK(ROWS_AT(40, 60) == 1, "...and only a real drop to 1.26 pads takes it back to 1");
    }

    /* HEAT (looks 3 and 4): a heat-sensitive sheet. Cold = black, then purple, blue, cyan, green, yellow, orange, red. */
    {
        uint8_t col[3], lo[3], hi[3];
        lights_heat_rgb(0.0f, col);
        CHECK(col[0] == 0 && col[1] == 0 && col[2] == 0, "heat: no warmth is BLACK");
        lights_heat_rgb(1.0f, col);
        CHECK(col[0] > 200 && col[1] < 30 && col[2] < 30, "heat: full warmth is red");
        lights_heat_rgb(0.02f, lo); lights_heat_rgb(0.10f, hi);
        CHECK(lo[0] + lo[1] + lo[2] < hi[0] + hi[1] + hi[2] && lo[0] + lo[1] + lo[2] > 0, "heat: it fades up from black (a touch of warmth is dim, not off)");
        lights_heat_rgb(0.15f, lo);
        CHECK(lo[2] > lo[1] && lo[0] > lo[1], "heat: just warm is purple (red + blue, no green)");
        lights_heat_rgb(0.05f, lo);
        CHECK(lo[2] > lo[1] && lo[0] > lo[1] && lo[0] + lo[1] + lo[2] > 0, "heat: the faintest warmth is already a (dark) purple, not another colour");
        lights_heat_rgb(0.62f, col);
        CHECK(col[1] > col[0] && col[1] > col[2], "heat: warm-ish is green");
        lights_heat_rgb(0.80f, col);
        CHECK(col[0] > 150 && col[1] > 100 && col[2] < 30, "heat: warmer is yellow-orange");
        float prev_h = 2.0f; int ok = 1;
        for (int i = 15; i <= 100; i++) {
            lights_heat_rgb((float)i / 100.0f, col);
            float hh = hue_of(col);
            if (i == 100 && hh > 0.5f) hh = 0.0f;
            if (hh > prev_h + 0.012f) ok = 0;
            prev_h = hh;
        }
        CHECK(ok, "heat: the hue only ever moves one way as it warms (purple -> blue -> green -> yellow -> red)");

        uint8_t lv[16]; memset(lv, 0, sizeof lv); lv[0] = 79;               /* 0.62 = 2.5 pads */
        lights_set_bar_style(3);
        lights_clear_bars();
        for (int i = 0; i < 10; i++) { lights_host(lv); lights_step(0.0f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(ON(whole[3 * 16 + 0]) && ON(whole[2 * 16 + 0]) && ON(whole[1 * 16 + 0]) && !ON(whole[0 * 16 + 0]), "heat: a bar of 2.5 pads lights 3 pads (the third partly: its colour says how much)");
        float hb = hue_of(whole[3 * 16 + 0]), hm = hue_of(whole[2 * 16 + 0]), ht = hue_of(whole[1 * 16 + 0]);
        CHECK(hb < hm && hm < ht, "heat: the base is the hottest (lowest hue), the tip the coolest: a gradient inside the bar");
        int vt = 0;
        for (int j = 0; j < 3; j++) if (whole[1 * 16 + 0][j] > vt) vt = whole[1 * 16 + 0][j];
        CHECK(vt > 0, "heat: the tip is lit");
        memset(lv, 0, sizeof lv); lv[0] = 1;                                  /* a sliver: 0.03 pads */
        lights_clear_bars();
        for (int i = 0; i < 10; i++) { lights_host(lv); lights_step(0.0f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        long anyp = 0; for (int i = 0; i < 64; i++) anyp += ON(whole[i]);
        CHECK(anyp == 0, "heat: a sliver of a bar lights nothing (cold = black)");
        /* flipped: cool at the base, hot at the tip */
        lights_set_heat_flip(1);
        memset(lv, 0, sizeof lv); lv[0] = 79;                                  /* 2.5 pads again */
        lights_set_bar_style(3); lights_clear_bars();
        for (int i = 0; i < 10; i++) { lights_host(lv); lights_step(0.0f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        float fb0 = hue_of(whole[3 * 16 + 0]), fb1 = hue_of(whole[2 * 16 + 0]), fb2 = hue_of(whole[1 * 16 + 0]);
        CHECK(fb0 > fb1 && fb1 > fb2, "heat flip: the base is the COOLEST (highest hue), the tip the hottest: the gradient runs the other way");
        memset(lv, 0, sizeof lv); lv[0] = 127;                                 /* a full bar */
        lights_clear_bars();
        for (int i = 0; i < 10; i++) { lights_host(lv); lights_step(0.0f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(whole[0 * 16 + 0][0] > 200 && whole[0 * 16 + 0][1] < 40 && whole[0 * 16 + 0][2] < 40, "heat flip: a full bar's top pad is red");
        CHECK(whole[3 * 16 + 0][2] > whole[3 * 16 + 0][0], "heat flip: ...and its bottom pad is blue (cool)");
        uint8_t g_flip[64][3];
        memcpy(g_flip, whole, sizeof g_flip);
        lights_set_heat_flip(0);
        lights_clear_bars();
        for (int i = 0; i < 10; i++) { lights_host(lv); lights_step(0.0f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(memcmp(g_flip, whole, sizeof g_flip) != 0 && whole[3 * 16 + 0][0] > 200, "heat flip off: a full bar's BASE is the red one again");
        memset(lv, 0, sizeof lv); lv[0] = 1;                                   /* a sliver is black either way */
        lights_set_heat_flip(1); lights_clear_bars();
        for (int i = 0; i < 10; i++) { lights_host(lv); lights_step(0.0f); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        long anyf = 0; for (int i = 0; i < 64; i++) anyf += ON(whole[i]);
        CHECK(anyf == 0, "heat flip: a sliver still lights nothing");
        lights_set_heat_flip(0);
        /* the movement: look 3 jumps to a level, look 4 follows it */
        uint8_t full[16]; memset(full, 127, sizeof full);
        lights_set_bar_style(3); lights_clear_bars(); lights_host(full); lights_step(0.033f);
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        int rows3 = 0; for (int r = 0; r < 4; r++) rows3 += ON(whole[r * 16 + 0]);
        lights_set_bar_style(4); lights_clear_bars(); lights_host(full); lights_step(0.033f);
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        int rows4 = 0; for (int r = 0; r < 4; r++) rows4 += ON(whole[r * 16 + 0]);
        CHECK(rows3 == 4 && rows4 < 4, "heat moves snappy (a full level fills the bar at once); heat smooth follows it");
    }

    /* BAR TIMING, frame by frame (review 4.10: until these, the peak hold, the rise speed, the "host went quiet"
       let-go, the whole look's peak dot and the TRACKS peak fall could all be broken with every test still green). */
    {
        uint8_t full[16], none[16];
        memset(full, 127, sizeof full); memset(none, 0, sizeof none);
        const float DT = 0.033f;                                          /* one 30 fps frame */
        #define PADS(col) ({ int n_ = 0; for (int r_ = 0; r_ < 4; r_++) n_ += ON(whole[r_ * 16 + (col)]); n_; })
        #define BARPADS(col) ({ int n_ = 0; for (int r_ = 0; r_ < 4; r_++) n_ += ON(whole[r_ * 16 + (col)]) && !GREY(whole[r_ * 16 + (col)]); n_; })
        #define ALLDARK() ({ int n_ = 0; for (int i_ = 0; i_ < 64; i_++) n_ += ON(whole[i_]); n_ == 0; })

        /* smooth: how fast the bar follows a level up, and down */
        lights_set_bar_style(1); lights_clear_bars();
        int fr_full = 0;
        for (int i = 1; i <= 30 && !fr_full; i++) {
            lights_host(full); lights_step(DT); lights_cols(LT_M_HOST, 0, 0, 16, whole);
            if (PADS(0) == 4) fr_full = i;
        }
        CHECK(fr_full >= 3 && fr_full <= 5, "smooth: a sudden full level fills the bar in 3-5 frames (follows quickly, but not in one jump)");
        for (int i = 0; i < 30; i++) { lights_host(full); lights_step(DT); }
        int fr_low = 0;
        for (int i = 1; i <= 60 && !fr_low; i++) {
            lights_host(none); lights_step(DT); lights_cols(LT_M_HOST, 0, 0, 16, whole);
            if (BARPADS(0) <= 1) fr_low = i;
        }
        CHECK(fr_low >= 6 && fr_low <= 10, "smooth: after a drop to silence the bar is down to one pad in 6-10 frames (it falls slower than it rises)");

        /* smooth: the peak dot waits, then falls a pad at a time */
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(full); lights_step(DT); }
        int left_top = 0, to_next = 0;
        for (int i = 1; i <= 90 && !left_top; i++) {
            lights_host(none); lights_step(DT); lights_cols(LT_M_HOST, 0, 0, 16, whole);
            if (!ON(whole[0])) { left_top = i; to_next = GREY(whole[16]); }
        }
        float t_left = (float)left_top * DT;
        CHECK(t_left > 1.1f && t_left < 1.45f, "smooth: after a drop the peak dot waits ~0.4 s on the top pad, then falls: it leaves the top pad ~1.3 s after the drop");
        CHECK(to_next, "smooth: ...and moves down to the next pad (it falls, it does not just vanish)");

        /* smooth: the host stops sending (a gap is not a drop; a long silence lets go) */
        lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(full); lights_step(DT); }
        for (int i = 0; i < 14; i++) lights_step(DT);                    /* 0.46 s with no frames from the host */
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(PADS(0) == 4, "smooth: when the host's frames stop, the bar holds its last level for half a second");
        for (int i = 0; i < 200; i++) lights_step(DT);                   /* 6.6 s more */
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(ALLDARK(), "smooth: ...then lets it go: with no host at all, the bars and dots fall to dark");

        /* whole: the peak dot (whole pads have their own dot rule) */
        lights_set_bar_style(2); lights_clear_bars();
        for (int i = 0; i < 40; i++) { lights_host(full); lights_step(DT); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        int greys = 0; for (int r = 0; r < 4; r++) greys += GREY(whole[r * 16 + 0]);
        CHECK(PADS(0) == 4 && greys == 0, "whole: a full bar is 4 coloured pads, no dot");
        for (int i = 0; i < 9; i++) { lights_host(none); lights_step(DT); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(GREY(whole[0]) && !ON(whole[16]) && !ON(whole[32]) && ON(whole[48]) && !GREY(whole[48]),
              "whole: 0.3 s after a drop the bar is down to its bottom pad and the peak dot waits on the top pad");

        /* heat: the peak dot */
        lights_set_bar_style(3); lights_clear_bars();
        lights_host(full); lights_step(DT);
        for (int i = 0; i < 12; i++) { lights_host(none); lights_step(DT); }
        lights_cols(LT_M_HOST, 0, 0, 16, whole);
        CHECK(GREY(whole[0]) && !ON(whole[16]) && ON(whole[32]) && ON(whole[48]) && !GREY(whole[32]),
              "heat: 0.4 s after a full bar the bar is under 2 pads and its peak dot is still on the top pad");

        /* TRACKS (hits): the bar drops fast, its peak dot falls more slowly behind it */
        lights_set_bar_style(0); lights_clear_bars();
        lights_hit(3, 3);
        for (int i = 0; i < 15; i++) lights_step(DT);                    /* 0.5 s */
        lights_cols(LT_M_HITS, 0, 0, 16, whole);
        CHECK(GREY(whole[0 * 16 + 3]) && !ON(whole[1 * 16 + 3]) && !ON(whole[2 * 16 + 3]) && ON(whole[3 * 16 + 3]) && !GREY(whole[3 * 16 + 3]),
              "TRACKS: 0.5 s after an accent the bar is down to the bottom pad and the peak dot is still on the top pad");
        for (int i = 0; i < 21; i++) lights_step(DT);                    /* 1.2 s */
        lights_cols(LT_M_HITS, 0, 0, 16, whole);
        CHECK(GREY(whole[2 * 16 + 3]) && !ON(whole[0 * 16 + 3]) && !ON(whole[1 * 16 + 3]) && !ON(whole[3 * 16 + 3]),
              "TRACKS: by 1.2 s the bar is gone and the dot has fallen to the third pad");
        for (int i = 0; i < 60; i++) lights_step(DT);
        lights_cols(LT_M_HITS, 0, 0, 16, whole);
        CHECK(ALLDARK(), "TRACKS: ...and then it is gone too");
        lights_clear_bars();
        lights_hit(7, 3); lights_hit(7, 1); lights_step(0.0f);
        lights_cols(LT_M_HITS, 0, 0, 16, whole);
        CHECK(PADS(7) == 4, "TRACKS: a soft hit right after an accent does not shorten the accent's bar");
    }

    lights_set_bar_style(0);                            /* (back to the default look) */

    /* the ORC's fake time-driven VU (mode 11) is gone */
    lights_cols(11, 5.0f, 0, 16, whole);
    any = 0; for (int i = 0; i < 64; i++) any += ON(whole[i]);
    CHECK(any == 0 && !strcmp(LT_NAME[11], "-"), "mode 11 (the old fake VU) draws nothing and has no name");

    CHECK(lights_scale(3) == 1.0f && lights_scale(1) == 0.4f, "ORC paces");

    /* TEXT: the 3x4 font, the scroll direction, the wrap, and the clamps */
    {
        static uint8_t fr[64][3];
        #define COLV(col) ((ON(fr[0 * 16 + (col)]) << 3) | (ON(fr[1 * 16 + (col)]) << 2) | (ON(fr[2 * 16 + (col)]) << 1) | ON(fr[3 * 16 + (col)]))
        CHECK(!strcmp(lights_text(), "HELLO"), "the default message is HELLO");
        lights_set_text("Hi", 2);
        CHECK(!strcmp(lights_text(), "HI"), "lower case is shown as capitals");
        lights_cols(LT_M_TEXT, 100.0f, 0, 16, fr);                    /* the first frame: p = 0, nothing on screen yet */
        long any = 0; for (int i = 0; i < 64; i++) any += ON(fr[i]);
        CHECK(any == 0, "the message starts off the right edge");
        lights_cols(LT_M_TEXT, 100.0f + 16.02f / 1.4f, 0, 16, fr);    /* p = 16: the H's first column is at pad column 0 */
        CHECK(COLV(0) == 15 && COLV(1) == 4 && COLV(2) == 15 && COLV(3) == 0, "H is two stems and a bar, then a gap");
        CHECK(COLV(4) == 9 && COLV(5) == 15 && COLV(6) == 9 && COLV(7) == 0, "I follows the H: serifs top and bottom, a full middle column");
        lights_cols(LT_M_TEXT, 100.0f + 17.02f / 1.4f, 0, 16, fr);    /* one column later: everything is one column LEFT */
        CHECK(COLV(0) == 4 && COLV(1) == 15, "the message scrolls to the left, a column at a time");
        lights_cols(LT_M_TEXT, 100.0f + (16.02f + 24.0f) / 1.4f, 0, 16, fr);   /* the strip is 2*4+16 = 24 columns: p + 24 repeats */
        CHECK(COLV(0) == 15 && COLV(1) == 4 && COLV(2) == 15, "the message repeats after one strip length");
        lights_set_text("  ", 2);
        lights_cols(LT_M_TEXT, 200.0f, 0, 16, fr); lights_cols(LT_M_TEXT, 230.0f, 0, 16, fr);
        any = 0; for (int i = 0; i < 64; i++) any += ON(fr[i]);
        CHECK(any == 0, "spaces draw nothing");
        char junk[5] = {'A', 1, 127, (char)200, 0};
        lights_set_text(junk, 4);
        CHECK(!strcmp(lights_text(), "A   "), "unprintable characters become spaces");
        char big[80]; memset(big, 'X', sizeof big);
        lights_set_text(big, 80);
        CHECK((int)strlen(lights_text()) == LT_TEXT_MAX, "a long message is cut to 32 characters");
        lights_set_text("", 0);
        lights_cols(LT_M_TEXT, 300.0f, 0, 16, fr);
        CHECK(strlen(lights_text()) == 0, "an empty message is allowed");
        lights_set_text("HELLO", 5);
    }

    /* the fast sine against the real one, over the small range and out to the large times the show reaches */
    double worst = 0;
    for (double x = -60.0; x <= 60.0; x += 0.0137) { double d = fabs((double)lights_fsin((float)x) - sin((double)(float)x)); if (d > worst) worst = d; }
    CHECK(worst < 2e-5, "fast sine is accurate over -60..60");
    worst = 0;
    for (double x = 0.0; x <= 40000.0; x += 3.17) { double d = fabs((double)lights_fsin((float)x) - sin((double)(float)x)); if (d > worst) worst = d; }
    CHECK(worst < 3e-4, "fast sine holds up at large arguments (the float input itself limits it)");
    /* speed factor: 2^((v-64)/32) at every setting */
    for (int v = 0; v < 128; v++)
        CHECK(fabs((double)lights_speed_factor(v) - pow(2.0, (v - 64) / 32.0)) < 1e-5 * pow(2.0, (v - 64) / 32.0) * 4, "speed factor equals 2^((v-64)/32)");
    printf("lights_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
