/* Sparse step storage: only lit steps take memory.
 *
 * Each (pattern, track) has a 128-bit map of which steps are lit, and its lit steps' data packed in
 * step order inside one shared pool. The blocks sit in the pool in (pattern, track) order, so a
 * pattern's data is one contiguous run, which is also how it is saved.
 *
 * A lit step's entry (fields are only ever APPENDED, so an older, shorter entry loads as a prefix
 * with the rest zero = default):
 *   drum tracks    [0] step byte  [1] condition  [2] lock
 *   melodic tracks [0] step byte  [1] note       [2] condition  [3] lock  [4] chord 2nd  [5] chord 3rd
 * Step byte bits 0-1 level (0 = off: setting it removes the step), 2-3 chance, 4-5 ratchet, 6-7 gate.
 * Condition 0 = none (codes: see main.c). Lock 0 = none, else CC value + 1.
 * Chord: signed semitones from the step's note, 0 = no extra note.
 *
 * Saved record of one pattern (v2, kinds 17-24):
 *   [0] format 2  [1] drum entry size  [2] melodic entry size  [3] 0
 *   [4 .. 27]    track lengths (1-128, 0 = 16)
 *   [28 .. 411]  lit-step maps, 24 tracks x 4 little-endian 32-bit words
 *   [412 .. ]    the pattern's pool data
 * v1 records (v0.10, kinds 9-16) have no 4-byte head and entries of 1 / 2 bytes.
 */
#ifndef STEPS_H
#define STEPS_H
#include <stdint.h>
#include <stdbool.h>

#define ST_PATTERNS 32
#define ST_TRACKS 24
#define ST_MEL_FIRST 16                             /* tracks 16-23 are melodic */
#define ST_MAXSTEPS 128
#define ST_POOL 20480u                              /* bytes shared by all patterns' lit steps */
#define ST_DRUM_SIZE 3u
#define ST_MEL_SIZE 6u
#define ST_REC_HDR (4 + ST_TRACKS + ST_TRACKS * 16) /* 412: head + lengths + maps */
#define ST_REC_HDR_V1 (ST_TRACKS + ST_TRACKS * 16)  /* 408 */

typedef struct {
    uint8_t b;                                      /* step byte; level 0 = off */
    uint8_t note;                                   /* melodic: the step's note (0 = unset) */
    uint8_t cond;                                   /* condition, 0 = none */
    uint8_t lock;                                   /* 0 = none, else CC value + 1 */
    int8_t ch2, ch3;                                /* melodic chord: semitones from the note, 0 = none */
} step_t;

extern uint8_t steps_len[ST_PATTERNS][ST_TRACKS];   /* 1-128, 0 = 16 */

void steps_clear_all(void);
void steps_clear_pattern(int p);                    /* steps only; lengths are left alone */
bool steps_read(int p, int t, int s, step_t *o);    /* false (o zeroed) = off */
/* Write a whole step; (v->b & 3) == 0 removes it. Drum tracks ignore note/chord.
   false = the pool is full (nothing changed). */
bool steps_write(int p, int t, int s, const step_t *v);
uint8_t steps_get(int p, int t, int s);             /* step byte, 0 = off */
uint8_t steps_note(int p, int t, int s);            /* melodic step's note (0 = unset / off / drum track) */
/* Set the step byte (and the note, melodic); a step that was off starts with everything else 0,
   a lit one keeps its other fields. (b & 3) == 0 removes it. false = pool full. */
bool steps_set(int p, int t, int s, uint8_t b, uint8_t note);
bool steps_set_byte(int p, int t, int s, uint8_t b);   /* change the step byte only */
bool steps_copy_pattern(int src, int dst);          /* steps + lengths; false = no room (dst unchanged) */
bool steps_track_empty(int t);                      /* no lit step in any pattern */
uint32_t steps_used(void);                          /* pool bytes in use */
uint32_t steps_pattern_size(int p);                 /* pool bytes pattern p uses now */
/* Data bytes of each pattern's newest saved record, kept by main.c (after a save, a load, a
   compaction). Every room check counts a pattern as max(live, saved), so whatever is in flash
   always fits back into the pool at boot. steps_clear_all leaves it alone. */
extern uint16_t steps_saved[ST_PATTERNS];

uint32_t steps_record_len(int p);                   /* v2 record */
void steps_record_read(int p, uint32_t off, uint8_t *dst, uint32_t n);   /* bytes past the end read 0xFF */
bool steps_record_load(int p, const uint8_t *rec, uint32_t len);       /* v2; false = malformed or no room */
bool steps_record_load_v1(int p, const uint8_t *rec, uint32_t len);    /* v0.10's records */
/* An old full-size pattern record (v0.4-v0.8: 2064 bytes, v0.9: 4120), e.g. read in place from flash.
   false = the pool ran out (what fit is kept). */
bool steps_convert_full(int p, const uint8_t *r, uint32_t len);

#endif
