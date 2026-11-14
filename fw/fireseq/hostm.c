/* ORC mode = HOST mode (see hostm.h). Pure C: no hardware, so hostm_test.c runs it on the Pi.
 * The two decoders are v0.15's host.c (branch host-mode), which was checked against the ORC's own encoder. */
#include <string.h>
#include "hostm.h"
#include "fire_map.h"

static uint8_t *FB;
static volatile uint8_t *LEDS, *FINE;
static uint8_t fine_w = HM_W_DEFAULT;
static int MAXF = 21;

/* Akai's OLED packing (SEGGER's "_aBitMutate", the table akai-fire-handler and the ORC use):
 * reference bit number for each (y % 8, x % 7). Each 8-byte group of the payload carries 7 columns
 * x 8 rows = 56 bits; byte j bit b of a group = reference bit j*7 + b.
 * INV is that table turned round (reference bit -> y << 3 | x), worked out once and kept in flash: RAM is short
 * (hostm_test.c rebuilds it from the table and compares). */
const uint8_t HM_INV[56] = {
     8, 16, 24, 32, 40, 48, 56, 17, 25, 33, 41, 49, 57,  0,
    26, 34, 42, 50, 58,  1,  9, 35, 43, 51, 59,  2, 10, 18,
    44, 52, 60,  3, 11, 19, 27, 53, 61,  4, 12, 20, 28, 36,
    62,  5, 13, 21, 29, 37, 45,  6, 14, 22, 30, 38, 46, 54,
};
#define INV HM_INV
static uint8_t curve = HM_GAMMA_X10;                /* the pad colour curve x 10 */

/* SysEx parser */
enum { S_IDLE, S_HDR, S_DATA, S_SKIP };
static uint8_t st, hdr[6], hn, cell[4];
static unsigned len, pos;
static uint8_t ob0, ob1, oc0, oc1;                  /* screen window: bands, columns */
static bool owin_ok;

void hm_reset(void) { st = S_IDLE; hn = 0; pos = 0; owin_ok = false; }

void hm_init(uint8_t *fb, volatile uint8_t *leds, volatile uint8_t *fine, int max_fine)
{
    FB = fb; LEDS = leds; FINE = fine; MAXF = max_fine;
    hm_set_gamma(HM_GAMMA_X10);
    hm_reset();
}

/* The curve, in whole numbers (powf would pull newlib's stdio into RAM, and there is no RAM for a table): x = v / 127;
   curve 10 = x, 20 = x * x, 30 = x * x * x, and a straight blend of the two neighbours in between. */
void hm_duty(uint8_t v, uint8_t *m, uint8_t *f)
{
    v &= 0x7F;
    *m = *f = 0;
    if (!v) return;
    uint32_t x = (uint32_t)v * 32768u / 127u;       /* 0 .. 32768 */
    uint32_t x2 = x * x >> 15, x3 = x2 * x >> 15;
    uint32_t a = curve <= 20 ? x : x2, b = curve <= 20 ? x2 : x3, t = curve <= 20 ? curve - 10u : curve - 20u;
    uint32_t y = (a * (10u - t) + b * t) / 10u;     /* the brightness wanted, 0 .. 32768 */
    /* One cycle = MAXF passes; a pass = 256 units, the short slot fine_w of them. Wanted, in units per cycle: */
    uint32_t u = (y * (uint32_t)MAXF * 256u + 16384u) >> 15, lw = 256u - fine_w;
    uint32_t mm = u / lw;
    if (mm > (uint32_t)MAXF) mm = (uint32_t)MAXF;
    uint32_t ff = (u - mm * lw + fine_w / 2u) / fine_w, fmax = mm < (uint32_t)MAXF ? lw / fine_w : (uint32_t)MAXF;
    if (fmax > (uint32_t)MAXF) fmax = (uint32_t)MAXF;
    if (ff > fmax) ff = fmax;                       /* never past the next long step: brighter in is never dimmer out */
    *m = (uint8_t)mm; *f = (uint8_t)ff;
}

void hm_set_weight(int w)
{
    if (w < 12) w = 12;                             /* (below 12 the short slot's 21 steps no longer reach the next long step) */
    if (w > 64) w = 64;
    fine_w = (uint8_t)w;
}
int hm_weight(void) { return fine_w; }

void hm_set_gamma(int x10)
{
    if (x10 < 10) x10 = 10;
    if (x10 > 30) x10 = 30;
    curve = (uint8_t)x10;
}

/* Akai's firmware's resting button look, from photos of the stock Fire running the ORC (2026-10-13). */
void hm_buttons(void)
{
    static const uint8_t SOFT[] = {LED_STEP_Y, LED_NOTE_Y, LED_DRUM_Y, LED_PERFORM_Y, LED_SHIFT_Y,
                                   LED_PATSONG_Y, LED_PLAY_Y, LED_STOP_Y, LED_REC_Y};
    static const uint8_t BRIGHT[] = {LED_ALT_Y, LED_PAT_UP, LED_PAT_DN, LED_BROWSER, LED_GRID_L, LED_GRID_R,
                                     LED_CHANNEL};
    for (unsigned i = 0; i < sizeof SOFT; i++) LEDS[SOFT[i]] = FINE[SOFT[i]] = HM_BTN_SOFT;
    for (unsigned i = 0; i < sizeof BRIGHT; i++) LEDS[BRIGHT[i]] = FINE[BRIGHT[i]] = (uint8_t)MAXF;
    for (int r = 0; r < 4; r++) LEDS[LED_ROW_G[r]] = FINE[LED_ROW_G[r]] = HM_BTN_SOFT;    /* the MUTE buttons' green rings */
}

uint8_t hm_rel(int d)
{
    if (d > 63) d = 63;
    if (d < -63) d = -63;
    return (uint8_t)(d < 0 ? 128 + d : d);
}

static void pad_byte(uint8_t b)
{
    unsigned k = pos & 3;
    cell[k] = b;
    if (k != 3) return;
    int p = cell[0] & 0x3F, r = p / 16, c = p % 16;
    for (int j = 0; j < 3; j++) {
        uint8_t m, f;
        hm_duty(cell[1 + j], &m, &f);
        LEDS[led_pad(r, c, j)] = m; FINE[led_pad(r, c, j)] = f;
    }
}

static void oled_byte(uint8_t b)
{
    if (pos < 4) {
        uint8_t *w[4] = {&ob0, &ob1, &oc0, &oc1};
        *w[pos] = b;
        if (pos == 3) owin_ok = ob0 <= ob1 && ob1 < 8 && oc0 <= oc1 && oc1 < 128;
        return;
    }
    if (!owin_ok) return;
    unsigned k = pos - 4, g = k >> 3, j = k & 7;
    unsigned w = (unsigned)(oc1 - oc0 + 1), n = w * (unsigned)(ob1 - ob0 + 1);
    for (unsigned bit = 0; bit < 7; bit++) {
        uint8_t inv = INV[j * 7 + bit];
        unsigned X = g * 7 + (inv & 7);
        if (X >= n) continue;
        uint8_t *byte = &FB[(ob0 + X / w) * 128 + oc0 + X % w];
        uint8_t m = (uint8_t)(1u << (inv >> 3));
        if (b >> bit & 1) *byte |= m; else *byte = (uint8_t)(*byte & ~m);
    }
}

int hm_byte(uint8_t b)
{
    if (b >= 0xF8) return 0;                        /* real-time bytes may sit inside a SysEx */
    if (b == 0xF0) { st = S_HDR; hn = 0; return 0; }
    if (b == 0xF7) {
        int r = 0;
        if (st == S_DATA && hdr[3] == 0x0E && pos >= 4 && owin_ok) r = HM_OLED_DONE;
        st = S_IDLE;
        return r;
    }
    if (b & 0x80) { st = S_IDLE; return 0; }        /* any other status byte ends a SysEx */

    if (st == S_HDR) {
        static const uint8_t AKAI[3] = {0x47, 0x7F, 0x43};
        hdr[hn++] = b;
        if (hn <= 3 && b != AKAI[hn - 1]) { st = S_SKIP; return 0; }   /* ours (7D 'ORC') goes the usual way, not here */
        if (hn == 6) {
            len = (unsigned)hdr[4] << 7 | hdr[5];
            pos = 0; owin_ok = false;
            st = (hdr[3] == 0x65 || hdr[3] == 0x0E) ? S_DATA : S_SKIP;
        }
        return 0;
    }
    if (st == S_DATA) {
        if (pos >= len) return 0;                   /* longer than it said: ignore the rest */
        if (hdr[3] == 0x65) pad_byte(b);
        else oled_byte(b);
        pos++;
    }
    return 0;
}

/* Inputs 64-87 (buttons) -> Akai's note numbers; 0 = no meaning to the ORC. */
static const uint8_t BTN_NOTE[IN_COUNT - 64] = {
    [IN_ALT - 64] = 49, [IN_PATSONG - 64] = 50, [IN_PLAY - 64] = 51, [IN_STOP - 64] = 52,
    [IN_REC - 64] = 53, [IN_ROW1 - 64] = 36, [IN_ROW2 - 64] = 37, [IN_ROW3 - 64] = 38,
    [IN_ROW4 - 64] = 39, [IN_STEP - 64] = 44, [IN_NOTE - 64] = 45, [IN_DRUM - 64] = 46,
    [IN_PERFORM - 64] = 47, [IN_SHIFT - 64] = 48, [IN_GRID_R - 64] = 35, [IN_GRID_L - 64] = 34,
    [IN_SELECT - 64] = 25, [IN_BROWSER - 64] = 33, [IN_PAT_UP - 64] = 31, [IN_PAT_DN - 64] = 32,
    [IN_KNOBMODE - 64] = 26,
};

int hm_input(int ev, uint8_t out[2])
{
    int idx = ev & 0x7F;
    bool down = ev & 0x80;
    int note, vel;
    if (idx < 64) { note = 54 + pad_of_input(idx); vel = 100; }   /* (the chain has no pad velocity) */
    else if (idx < IN_COUNT && BTN_NOTE[idx - 64]) { note = BTN_NOTE[idx - 64]; vel = 127; }
    else return 0;
    out[0] = (uint8_t)note;
    out[1] = down ? (uint8_t)vel : 0;
    return 1;
}

int hm_exit(int ev, int shift_held) { return (ev & 0x80) && (ev & 0x7F) == IN_BROWSER && shift_held; }

int hm_quiet(uint32_t now_ms, uint32_t seen_ms) { return now_ms - seen_ms >= HM_QUIET_MS; }
