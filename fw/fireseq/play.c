#include "play.h"

const char *const COND_NAME[C_COUNT] = {"--", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1st", "not 1st", "fill", "not fill"};
static const uint8_t COND_A[C_COUNT] = {0, 1, 2, 1, 2, 3, 1, 2, 3, 4};
static const uint8_t COND_B[C_COUNT] = {0, 2, 2, 3, 3, 3, 4, 4, 4, 4};

bool cond_ok(int c, uint32_t iter, bool fill)
{
    switch (c) {
    case C_NONE: return true;
    case C_FIRST: return iter == 0;
    case C_NFIRST: return iter != 0;
    case C_FILL: return fill;
    case C_NFILL: return !fill;
    default:
        if (c < 0 || c >= C_COUNT || !COND_B[c]) return true;
        return iter % COND_B[c] == (uint32_t)(COND_A[c] - 1);   /* "A:B" = the A-th of every B loops */
    }
}

void play_release(held_notes_t *h, const play_out_t *o)
{
    for (int i = 0; i < h->n; i++) o->off(h->chan, h->note[i]);
    h->n = 0;
}

static bool in(const uint8_t *a, int n, uint8_t x) { for (int i = 0; i < n; i++) if (a[i] == x) return true; return false; }

int play_step_notes(const step_t *v, bool mel, uint8_t drum_note, uint8_t ch, uint8_t lock_cc, uint8_t vel,
                    held_notes_t *h, const play_out_t *o, uint8_t notes[3])
{
    if (v->lock && lock_cc) o->cc(ch, lock_cc, (uint8_t)(v->lock - 1));   /* parameter lock, before the notes */
    int nn = 0;
    notes[nn++] = mel ? (v->note ? v->note : 60) : drum_note;
    if (mel) {                                      /* chord: up to two more notes */
        int c2 = notes[0] + v->ch2, c3 = notes[0] + v->ch3;
        if (v->ch2 && c2 >= 0 && c2 <= 127) notes[nn++] = (uint8_t)c2;
        if (v->ch3 && c3 >= 0 && c3 <= 127 && !in(notes, nn, (uint8_t)c3)) notes[nn++] = (uint8_t)c3;
    }
    /* what a previous TIE / SLIDE step holds: same notes on the same channel carry on */
    uint8_t ends[3]; int ne = 0;
    for (int j = 0; j < h->n; j++)
        if (!(h->chan == ch && in(notes, nn, h->note[j]))) ends[ne++] = h->note[j];
    bool slide = h->n && h->mode == H_SLIDE;
    uint8_t hch = h->chan;
    if (!slide) for (int j = 0; j < ne; j++) o->off(hch, ends[j]);   /* tie: end first */
    for (int i = 0; i < nn; i++)
        if (!(h->n && h->chan == ch && in(h->note, h->n, notes[i]))) o->on(ch, notes[i], vel);
    if (slide) for (int j = 0; j < ne; j++) o->off(hch, ends[j]);    /* slide: end after (legato) */
    h->n = 0;
    int hold = hold_of(v->cond);
    if (hold) {                                     /* this step holds until the track's next note */
        for (int i = 0; i < nn; i++) h->note[i] = notes[i];
        h->n = (uint8_t)nn; h->chan = ch; h->mode = (uint8_t)hold;
        return 0;
    }
    return nn;
}

bool ext_tick_plays(int pos, int late, bool *odd_due)
{
    if (pos == 0) { *odd_due = true; return true; }
    if (*odd_due && (pos >= 6 + late || pos == 11)) { *odd_due = false; return true; }
    return false;
}

uint32_t nearest_sounding_step(uint32_t r, uint32_t tr, uint32_t S, uint32_t W, uint32_t now)
{
    uint32_t g = r;
    int32_t best = 0x7FFFFFFF;
    for (int d = -1; d <= 1; d++) {
        if (d < 0 && r == 0) continue;
        uint32_t k = r + (uint32_t)d, at = tr + (uint32_t)(d * (int32_t)S) + ((k & 1) ? W : 0);
        int32_t dist = (int32_t)(now - at);
        if (dist < 0) dist = -dist;
        if (dist < best) { best = dist; g = k; }
    }
    return g;
}
