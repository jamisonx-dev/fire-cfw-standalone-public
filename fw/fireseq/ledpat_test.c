/* Host test of ledpat.h (the LED blink patterns, v0.33). gcc -O1 -Wall -Wextra -I. ledpat_test.c -o ledpat_test */
#include <stdio.h>
#include "ledpat.h"

static int fails, checks;
#define EXPECTED 11
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static int ones(uint32_t m) { int n = 0; while (m) { n += (int)(m & 1u); m >>= 1; } return n; }

/* the longest run of dark frames, the cycle repeating */
static int longest_gap(uint32_t m, int frames)
{
    if (!m) return frames;
    int best = 0, run = 0;
    for (int f = 0; f < 2 * frames; f++) {
        if (m >> (f % frames) & 1) run = 0;
        else if (++run > best) best = run;
    }
    return best;
}

int main(void)
{
    int ok;

    ok = 1;                                         /* a level is lit in exactly that many frames */
    for (int L = 0; L <= LED_FRAMES; L++) if (ones(ledpat_bits(L, LED_FRAMES)) != L) ok = 0;
    CHECK(ok, "fine level L is lit in exactly L of the 21 frames");

    CHECK(ledpat_bits(0, LED_FRAMES) == 0 && ledpat_bits(LED_FRAMES, LED_FRAMES) == (1u << LED_FRAMES) - 1u,
          "level 0 is never lit, level 21 always");

    ok = 1;                                         /* nothing above frame 20 is ever set (the interrupt shifts by 0-20) */
    for (int L = -3; L <= 40; L++) if (ledpat_bits(L, LED_FRAMES) >> LED_FRAMES) ok = 0;
    CHECK(ok, "no bit above the last frame, for any level");

    CHECK(ledpat_bits(-5, LED_FRAMES) == 0 && ledpat_bits(22, LED_FRAMES) == ledpat_bits(21, LED_FRAMES)
          && ledpat_bits(31, LED_FRAMES) == ledpat_bits(21, LED_FRAMES),
          "out-of-range levels clamp (table entries 22-31 are full on)");

    ok = 1;                                         /* spread: the dark gaps are as short as they can be */
    for (int L = 1; L <= LED_FRAMES; L++) {
        int want = (LED_FRAMES - L + L - 1) / L;    /* ceil(dark frames / lit frames) */
        if (longest_gap(ledpat_bits(L, LED_FRAMES), LED_FRAMES) > want) ok = 0;
    }
    CHECK(ok, "the lit frames are spread evenly (no dark gap longer than it must be)");

    ok = 1;                                         /* the old 8 levels: same on-time, and the same 7-frame rhythm */
    for (int k = 0; k <= 7; k++) {
        uint32_t m = ledpat_bits(ledpat_fine(k), LED_FRAMES);
        if (ones(m) * 7 != k * LED_FRAMES) ok = 0;
        uint32_t seven = m & 0x7Fu;
        if (m != (seven | seven << 7 | seven << 14)) ok = 0;
        if (ones(seven) != k) ok = 0;
    }
    CHECK(ok, "an old level 0-7 keeps its on-time (k / 7) and repeats every 7 frames, as before v0.33");

    CHECK(ledpat_fine(-1) == 0 && ledpat_fine(0) == 0 && ledpat_fine(1) == 3 && ledpat_fine(7) == 21 && ledpat_fine(200) == 21,
          "old levels map to 3 x level, clamped to 0-7");

    ok = 1;                                         /* brighter level = never fewer lit frames */
    for (int L = 1; L <= LED_FRAMES; L++) if (ones(ledpat_bits(L, LED_FRAMES)) <= ones(ledpat_bits(L - 1, LED_FRAMES))) ok = 0;
    CHECK(ok, "every step up is brighter");

    CHECK(LED_FRAMES == 7 * LED_PER_LEVEL && LED_FRAMES < 32 && LED_PAT_N == 32, "21 frames = 7 x 3, fits a 32-bit pattern and the masked table");

    ok = 1;                                         /* v0.34d: the table on its side holds exactly the same bits */
    for (int f = 0; f < LED_FRAMES; f++)
        for (int L = 0; L < LED_PAT_N; L++)
            if ((ledpat_lit(f, LED_FRAMES) >> L & 1u) != (ledpat_bits(L, LED_FRAMES) >> f & 1u)) ok = 0;
    CHECK(ok, "v0.34d ledpat_lit(frame) bit L = ledpat_bits(L) bit frame, for every level 0-31 and every frame");

    ok = 1;                                         /* v0.34d: a chain byte = what the v0.33 / v0.34 interrupt built, LED by LED */
    {
        uint32_t seed = 12345u;
        for (int n = 0; n < 20000; n++) {
            volatile uint8_t lv[8];
            for (int k = 0; k < 8; k++) {
                seed = seed * 1664525u + 1013904223u;
                lv[k] = (uint8_t)(n < 256 ? n : seed >> 24);            /* every value 0-255 in every position, then random mixes */
            }
            for (int f = 0; f < LED_FRAMES; f++) {
                uint32_t o = 0;
                for (int k = 0; k < 8; k++) o |= (ledpat_bits(lv[k] & (LED_PAT_N - 1), LED_FRAMES) >> f & 1u) << k;
                if (ledpat_byte(lv, ledpat_lit(f, LED_FRAMES)) != (uint8_t)o) ok = 0;
            }
        }
    }
    CHECK(ok, "v0.34d ledpat_byte gives the same 8 bits as the old one-LED-at-a-time build (all levels 0-255, every frame, LED k = bit k)");

    printf("ledpat_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
