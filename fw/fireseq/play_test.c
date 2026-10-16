/* Host test for play.c (what a step sends), on the Pi:
 *   gcc -O2 -Wall -fsanitize=bounds -fsanitize-undefined-trap-on-error -o play_test play_test.c play.c && ./play_test
 * Each case plays a short run of steps on one track and compares the exact MIDI sequence. */
#include <stdio.h>
#include <string.h>
#include "play.h"

static char got[2048];
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static void app(const char *s) { strncat(got, s, sizeof got - strlen(got) - 1); }
static void o_cc(uint8_t ch, uint8_t cc, uint8_t v) { char b[32]; snprintf(b, sizeof b, "cc%d:%d=%d ", ch + 1, cc, v); app(b); }
static void o_on(uint8_t ch, uint8_t n, uint8_t vel) { char b[32]; snprintf(b, sizeof b, "on%d:%d ", ch + 1, n); app(b); (void)vel; }
static void o_off(uint8_t ch, uint8_t n) { char b[32]; snprintf(b, sizeof b, "off%d:%d ", ch + 1, n); app(b); }
static const play_out_t O = {o_cc, o_on, o_off};

static step_t S(uint8_t note, int8_t a, int8_t b, int hold, uint8_t lock)
{
    step_t v = {2, note, (uint8_t)(hold << 6), lock, a, b};
    return v;
}
/* play a step, append "|n=<returned>" */
static void P(const step_t *v, bool mel, uint8_t ch, uint8_t lockcc, held_notes_t *h)
{
    uint8_t notes[3];
    int n = play_step_notes(v, mel, 36, ch, lockcc, 100, h, &O, notes);
    char b[16]; snprintf(b, sizeof b, "|%d ", n); app(b);
}
static void expect(const char *want, const char *what)
{
    CHECK(!strcmp(got, want), what);
    if (strcmp(got, want)) printf("   got:  %s\n   want: %s\n", got, want);
    got[0] = 0;
}

int main(void)
{
    held_notes_t h = {{0}, 0, 0, 0};
    step_t a;

    a = S(60, 0, 0, H_NONE, 0); P(&a, true, 0, 0, &h);
    expect("on1:60 |1 ", "plain melodic step");
    a = S(0, 0, 0, H_NONE, 0); P(&a, true, 0, 0, &h);
    expect("on1:60 |1 ", "melodic note 0 = C4");
    a = S(99, 0, 0, H_NONE, 0); P(&a, false, 9, 0, &h);
    expect("on10:36 |1 ", "drum track plays its own note, ignores the step's");
    a = S(60, 4, 7, H_NONE, 0); P(&a, false, 9, 0, &h);
    expect("on10:36 |1 ", "drum track ignores a chord");

    a = S(60, 4, 7, H_NONE, 0); P(&a, true, 0, 0, &h);
    expect("on1:60 on1:64 on1:67 |3 ", "chord: three notes");
    a = S(60, -12, 0, H_NONE, 0); P(&a, true, 0, 0, &h);
    expect("on1:60 on1:48 |2 ", "chord with a note below");
    a = S(120, 12, 7, H_NONE, 0); P(&a, true, 0, 0, &h);
    expect("on1:120 on1:127 |2 ", "chord notes past 127 are dropped");
    a = S(60, 12, 12, H_NONE, 0); P(&a, true, 0, 0, &h);
    expect("on1:60 on1:72 |2 ", "a duplicate chord note sounds once");

    a = S(60, 0, 0, H_NONE, 97); P(&a, true, 2, 74, &h);
    expect("cc3:74=96 on3:60 |1 ", "lock: the CC goes first, value = lock - 1");
    a = S(60, 0, 0, H_NONE, 1); P(&a, true, 2, 74, &h);
    expect("cc3:74=0 on3:60 |1 ", "lock value 0");
    a = S(60, 0, 0, H_NONE, 97); P(&a, true, 2, 0, &h);
    expect("on3:60 |1 ", "lock without a track CC sends nothing");
    a = S(60, 0, 0, H_NONE, 0); P(&a, true, 2, 74, &h);
    expect("on3:60 |1 ", "track CC without a step lock sends nothing");

    /* TIE */
    step_t t1 = S(60, 0, 0, H_TIE, 0), n62 = S(62, 0, 0, H_NONE, 0), n60 = S(60, 0, 0, H_NONE, 0);
    P(&t1, true, 0, 0, &h); P(&n62, true, 0, 0, &h);
    expect("on1:60 |0 off1:60 on1:62 |1 ", "tie to another note: the old ends, then the new starts");
    P(&t1, true, 0, 0, &h); P(&n60, true, 0, 0, &h);
    expect("on1:60 |0 |1 ", "tie to the same note: no restrike, the next step gates it");
    P(&t1, true, 0, 0, &h); P(&t1, true, 0, 0, &h); P(&t1, true, 0, 0, &h); P(&n60, true, 0, 0, &h);
    expect("on1:60 |0 |0 |0 |1 ", "a chain of ties on one note: struck once");
    /* SLIDE */
    step_t s1 = S(60, 0, 0, H_SLIDE, 0);
    P(&s1, true, 0, 0, &h); P(&n62, true, 0, 0, &h);
    expect("on1:60 |0 on1:62 off1:60 |1 ", "slide: the new note starts BEFORE the old ends (legato)");
    P(&s1, true, 0, 0, &h); P(&n60, true, 0, 0, &h);
    expect("on1:60 |0 |1 ", "slide to the same note: carries on");
    step_t s62 = S(62, 0, 0, H_SLIDE, 0), n64 = S(64, 0, 0, H_NONE, 0);
    P(&s1, true, 0, 0, &h); P(&s62, true, 0, 0, &h); P(&n64, true, 0, 0, &h);
    expect("on1:60 |0 on1:62 off1:60 |0 on1:64 off1:62 |1 ", "slide, slide: a gliding line");
    /* chords + hold */
    step_t tc = S(60, 4, 7, H_TIE, 0), nc = S(60, 3, 7, H_NONE, 0);
    P(&tc, true, 0, 0, &h); P(&nc, true, 0, 0, &h);
    expect("on1:60 on1:64 on1:67 |0 off1:64 on1:63 |3 ", "tied chord into a related chord: shared notes carry on");
    /* channel change between a tie and the next step: no carry-over across channels */
    P(&t1, true, 0, 0, &h); P(&n60, true, 1, 0, &h);
    expect("on1:60 |0 off1:60 on2:60 |1 ", "held note on another channel ends; the new one strikes");
    /* release (mute / stop) */
    P(&t1, true, 0, 0, &h); play_release(&h, &O);
    expect("on1:60 |0 off1:60 ", "release ends a held note");
    play_release(&h, &O);
    expect("", "release with nothing held sends nothing");
    /* lock + slide ordering */
    step_t sl = S(62, 0, 0, H_NONE, 11);
    P(&s1, true, 0, 74, &h); P(&sl, true, 0, 74, &h);
    expect("on1:60 |0 cc1:74=10 on1:62 off1:60 |1 ", "lock CC comes before the slide hand-over");
    CHECK(h.n == 0, "a normal step leaves nothing held");

    /* conditions */
    char row[64]; int ok = 1;
    const char *want[C_COUNT] = {"11111111", "10101010", "01010101", "10010010", "01001001", "00100100", "10001000", "01000100", "00100010", "00010001", "10000000", "01111111", "", ""};
    for (int c = 0; c < C_FILL; c++) {
        for (int i = 0; i < 8; i++) row[i] = cond_ok(c, (uint32_t)i, false) ? '1' : '0';
        row[8] = 0;
        if (strcmp(row, want[c])) { printf("   cond %s: %s want %s\n", COND_NAME[c], row, want[c]); ok = 0; }
    }
    CHECK(ok, "ratio and first conditions over loops 0-7");
    CHECK(cond_ok(C_FILL, 3, true) && !cond_ok(C_FILL, 3, false) && cond_ok(C_NFILL, 3, false) && !cond_ok(C_NFILL, 3, true), "FILL / not FILL follow the button");
    CHECK(cond_of(0x80 | C_R13) == C_R13 && hold_of(0x80 | C_R13) == H_SLIDE && hold_of(C_R13) == H_NONE, "condition and hold mode share the byte");
    CHECK(cond_of(63) == C_NONE && hold_of(0xC0) == H_NONE, "unknown codes read as none");

    /* v0.12: external clock: exactly 2 steps per 12 ticks, the even one on tick 0, whatever swing does */
    {
        int ok2 = 1;
        for (int trial = 0; trial < 2000 && ok2; trial++) {
            bool due = false; int steps = 0, evens_on_0 = 1;
            unsigned seed = (unsigned)trial * 2654435761u;
            for (int tick = 0; tick < 12 * 40; tick++) {
                seed = seed * 1103515245u + 12345u;
                int swing = 50 + (int)((seed >> 16) % 26);      /* swing changes on ANY tick */
                int late = (swing - 50) * 6 / 50;
                if (ext_tick_plays(tick % 12, late, &due)) {
                    if ((steps & 1) == 0 && tick % 12 != 0) evens_on_0 = 0;
                    steps++;
                }
                if (tick % 12 == 11 && steps != (tick / 12 + 1) * 2) ok2 = 0;
            }
            if (!evens_on_0) ok2 = 0;
        }
        CHECK(ok2, "external clock: 2 steps per pair, evens on tick 0, with swing changing at random ticks");
        bool due = false; int at = -1;
        for (int tick = 0; tick < 12; tick++) if (ext_tick_plays(tick, 2, &due) && tick) at = tick;
        CHECK(at == 8, "the odd step plays on tick 6 + late");
        /* the reviewer's case: swing moves 58 -> 59 (late 0 -> 1) between tick 6 and 7 */
        due = false; int n = 0;
        for (int tick = 0; tick < 12; tick++) n += ext_tick_plays(tick, tick < 7 ? 0 : 1, &due);
        CHECK(n == 2, "swing 58 -> 59 mid-pair: still exactly 2 steps (was 3)");
        due = false; n = 0;
        for (int tick = 0; tick < 12; tick++) n += ext_tick_plays(tick, tick < 7 ? 1 : 0, &due);
        CHECK(n == 2, "swing 59 -> 58 mid-pair: still exactly 2 steps (was 1)");
    }
    /* v0.12: REC goes to the step nearest by when it SOUNDS (the reviewer's swing-75 cases) */
    {
        /* step 1000 us, swing 75 % -> odd steps sound 500 us late (at 1500, 3500 ...) */
        CHECK(nearest_sounding_step(0, 0, 1000, 500, 650) == 0, "hit at 650 (step 0 sounded at 0, step 1 at 1500): step 0");
        CHECK(nearest_sounding_step(0, 0, 1000, 500, 800) == 1, "hit at 800: step 1 (700 away vs 800)");
        CHECK(nearest_sounding_step(1, 1000, 1000, 500, 1600) == 1, "hit at 1600, just behind swung step 1: step 1 (was 2)");
        CHECK(nearest_sounding_step(1, 1000, 1000, 500, 1100) == 1, "hit at 1100 (step 1 not sounded yet, 400 early): step 1");
        CHECK(nearest_sounding_step(1, 1000, 1000, 500, 1010) == 1, "hit at 1010: step 1 (sounds at 1500, 490 away) beats step 0 (1010 away)");
        CHECK(nearest_sounding_step(2, 2000, 1000, 0, 2600) == 3, "no swing: past halfway goes to the next step");
        CHECK(nearest_sounding_step(2, 2000, 1000, 0, 2400) == 2, "no swing: before halfway stays");
        CHECK(nearest_sounding_step(0, 0, 1000, 0, 100) == 0, "step 0 has no step before it");
        CHECK(nearest_sounding_step(5, 0xFFFFFF00u, 1000, 0, 0xFFFFFF00u + 700) == 6, "works across the 32-bit timer wrap");
    }

    printf("%d checks, %s\n", checks, fails ? "FAILED" : "ALL PASSED");
    return fails != 0;
}
