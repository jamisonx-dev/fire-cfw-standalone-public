/* What one step of one track sends: the decision logic of playback, kept free of hardware so the
 * host test (play_test.c) can run it. main.c does the timing (gates, ratchets, swing).
 *
 * Step condition byte (step_t.cond): bits 0-5 condition, bits 6-7 hold mode.
 * Conditions count the TRACK's own loops (steps played / its length) since PLAY from the start.
 */
#ifndef PLAY_H
#define PLAY_H
#include <stdint.h>
#include <stdbool.h>
#include "steps.h"

enum { C_NONE, C_R12, C_R22, C_R13, C_R23, C_R33, C_R14, C_R24, C_R34, C_R44, C_FIRST, C_NFIRST, C_FILL, C_NFILL, C_COUNT };
extern const char *const COND_NAME[C_COUNT];
enum { H_NONE, H_TIE, H_SLIDE };
static inline int cond_of(uint8_t c) { int k = c & 0x3F; return k < C_COUNT ? k : C_NONE; }
static inline int hold_of(uint8_t c) { int h = c >> 6; return h <= H_SLIDE ? h : H_NONE; }

bool cond_ok(int cond, uint32_t iter, bool fill); /* iter = how many times the track has looped */

typedef struct { uint8_t note[3], n, chan, mode; } held_notes_t;   /* notes a TIE / SLIDE step is holding */
typedef struct {
    void (*cc)(uint8_t ch, uint8_t cc, uint8_t value);
    void (*on)(uint8_t ch, uint8_t note, uint8_t vel);
    void (*off)(uint8_t ch, uint8_t note);
} play_out_t;

/* Play a lit step that passed its condition and chance: the lock CC (if the step has a value and the
 * track a CC), then its notes (the drum note, or the melodic note + chord), handing over from what a
 * previous TIE / SLIDE step holds in *h:
 *   - a note already held carries on (no restrike);
 *   - after a TIE the other held notes end BEFORE the new ones start;
 *   - after a SLIDE they end AFTER the new ones start (legato, so mono synths glide).
 * If this step is itself TIE / SLIDE, its notes go into *h (held until the track's next note) and
 * the return is 0. Otherwise *h is emptied and the notes are returned in notes[] for main.c to gate. */
int play_step_notes(const step_t *v, bool mel, uint8_t drum_note, uint8_t ch, uint8_t lock_cc, uint8_t vel,
                    held_notes_t *h, const play_out_t *o, uint8_t notes[3]);
void play_release(held_notes_t *h, const play_out_t *o);   /* end whatever *h holds */

/* External clock: called on each clock tick, pos = tick within the 12-tick pair of steps, late = how
   many ticks the swung (odd) step is delayed. true = play the next step now. The even step plays on
   tick 0; the odd one once per pair, at its tick or later (never skipped or doubled when swing moves). */
bool ext_tick_plays(int pos, int late, bool *odd_due);
/* REC: the step nearest to `now` by when it SOUNDS. r = the step that last started, tr its straight-
   grid time, S the step length, W the swing delay of odd steps (all us). Looks at r-1, r, r+1. */
uint32_t nearest_sounding_step(uint32_t r, uint32_t tr, uint32_t S, uint32_t W, uint32_t now);

#endif
