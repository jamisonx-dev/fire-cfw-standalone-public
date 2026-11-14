/* UNIVERSAL mode (v0.31): pages of pad + knob assignments stored ON THE FIRE, so they work with no Pi (like FADERS).
 * PURE logic (no hardware, no globals): main.c calls it, uni_test.c checks it.
 *
 * A page is one save record (store2 kind 49 + page), variable length, every byte 7-bit (it goes over SysEx as is):
 *   [0] version (1)   [1..8] name, ASCII 0x20-0x7E (spaces = no name)   then n entries of 6 bytes, any order:
 *   pad   [pad 0-63] [layer (0 base, 1 SHIFT) | colour << 1 (0-15 palette, 16 grey)] [type << 4 | channel 0-15] [number] [on] [off]
 *   knob  [64 + slot 0-15 (bank * 4 + knob)] [type 1 CC / 2 pitch bend] [channel 0-15] [CC] [start value] [0]
 * A pad or knob with no entry does nothing (an empty page = 9 bytes). One entry per pad + layer, one per knob slot.
 * Only the base entry's colour is shown. Pages live in flash only (RAM has no room for them); an edit made on the Fire is
 * a small PATCH held in RAM until it is saved: the save writes the page record with the patches applied (uni_patched_*).
 *
 * All pages together are kept within UNI_BUDGET bytes: a compaction (erase + rewrite of the save area) must hold them in
 * RAM for a moment, and the only RAM free for that is the undo buffers + the MIDI batch buffer (main.c). */
#ifndef UNI_H
#define UNI_H
#include <stdint.h>

#define UNI_PAGES 8
#define UNI_HDR 9
#define UNI_ENT 6
#define UNI_PADS 64
#define UNI_KNOBS 16                         /* 4 banks (KNOB MODE) x 4 knobs */
#define UNI_KEYS (UNI_PADS * 2 + UNI_KNOBS)  /* pad x layer, then knob slots: 144 */
#define UNI_MAX_LEN (UNI_HDR + UNI_ENT * UNI_KEYS)   /* 873 */
#define UNI_BUDGET 4096u                     /* all pages together (bytes) */
#define UNI_CHUNK 24                         /* host write / read chunk (a SysEx holds 31 at most) */
#define UNI_VER 1
#define UNI_NAME 8
#define UNI_COL_GREY 16
#define UNI_COLS 17

enum { UT_OFF, UT_NOTE, UT_LATCH, UT_HOLD, UT_TOGGLE, UT_PRESS, UT_PC, UT_N };   /* pad types */
enum { UK_NONE, UK_CC, UK_PB, UK_N };                                             /* knob types */
enum { UL_BASE, UL_SHIFT };
enum { UREL_NONE, UREL_NOTE, UREL_CC };      /* what a pad's release must do (uni_press) */
/* host results (the second byte of every reply) */
enum { UR_OK, UR_BAD, UR_BUSY, UR_ORDER, UR_SUM, UR_INVALID, UR_FULL, UR_ROOM, UR_FAIL };

typedef struct { uint8_t type, ch, num, on, off, col; } uni_pad_t;     /* ch 0-15 */
typedef struct { uint8_t type, ch, cc, init; } uni_knob_t;

int uni_len_ok(uint32_t len);
int uni_check(const uint8_t *r, uint32_t len);          /* UR_OK or UR_INVALID (every field in range, no duplicate) */
uint16_t uni_sum(const uint8_t *r, uint32_t len);       /* sum of the bytes, 14 bits (the host's checksum) */
int uni_key(const uint8_t *e);                          /* 0-143: pad * 2 + layer, then 128 + knob slot */
int uni_pad_key(int pad, int layer);
int uni_knob_key(int slot);
int uni_find(const uint8_t *r, uint32_t len, int key);  /* entry number, -1 none */
/* ix[key] = the entry number of every key (each pad's base AND SHIFT entry, each knob slot), 255 = none: one pass, so
 * a lookup in the page being played is O(1) (the pads repaint every pass; review 2026-10-27). */
void uni_index(const uint8_t *r, uint32_t len, uint8_t ix[UNI_KEYS]);
uni_pad_t uni_pad_of(const uint8_t *e);
uni_knob_t uni_knob_of(const uint8_t *e);
void uni_pad_enc(uint8_t e[UNI_ENT], int pad, int layer, uni_pad_t p);
void uni_knob_enc(uint8_t e[UNI_ENT], int slot, uni_knob_t k);
uni_pad_t uni_pad_blank(void);                          /* what a pad with no entry shows when edited (type off) */
uni_knob_t uni_knob_blank(void);

/* A pad pressed: the message to send (m[0..*n-1]; *n = 0, 2 or 3); *tog = the layer's toggle state (flipped by a CC
 * toggle pad). Returns what its release must do: UREL_NOTE (note off), UREL_CC (CC <num> = off), UREL_NONE.
 * A note LATCH is not handled here (it sends nothing): it goes through the latch list below. */
int uni_press(uni_pad_t p, uint8_t *tog, uint8_t m[3], int *n);

/* ---- latched notes: what each latched pad really SENT (channel + note), so its note-off is exactly that, whatever
 * happens to the pad's assignment meanwhile (an edit, a host write, a page change: review 2026-10-27). ---- */
#define UNI_LATCH_MAX 16
typedef struct { uint8_t key1, note, ch; } uni_latch_t;    /* key1 = key + 1, 0 = free (all zero = empty) */
void uni_latch_init(uni_latch_t L[UNI_LATCH_MAX]);
int uni_latch_on(const uni_latch_t L[UNI_LATCH_MAX], int key);
/* a note-latch pad (key) pressed: note on (remembered) or the remembered note off; m = the message, returns 3,
 * or 0 = UNI_LATCH_MAX notes are latched already (nothing sent) */
int uni_latch_press(uni_latch_t L[UNI_LATCH_MAX], int key, uni_pad_t p, uint8_t m[3]);
/* note-offs for the latches of pad (both layers), or of every pad (pad < 0), into out (3 bytes each, cap bytes):
 * they are forgotten. Returns the bytes written. */
int uni_latch_release(uni_latch_t L[UNI_LATCH_MAX], int pad, uint8_t *out, int cap);
/* A knob turned d detents: *val moves (0-127, kept), the message to send in m; returns its length (0 = nothing:
 * unassigned, or already at the end). Pitch bend: val 64 = centre, sent as 14 bits. */
int uni_knob_turn(uni_knob_t k, uint8_t *val, int d, uint8_t m[3]);
/* v0.38 (the owner 2026-11-13: "one secondary function across the board"): every mode button's second tap is its own
 * second view. PERFORM tapped: any mode -> FADERS; FADERS -> the host's DAW page (only while has_daw), else back to
 * `before`; the DAW page -> `before`. (v0.31-v0.37 went FADERS -> UNIVERSAL -> DAW page -> back.) */
int uni_perform_next(int mode, int before, int m_fader, int m_daw, int has_daw);
/* v0.38 NOTE pressed: on the keyboard (m_note) -> UNIVERSAL; anywhere else, UNIVERSAL included -> the keyboard. */
int uni_note_next(int mode, int m_note, int m_uni);

/* ---- on-Fire editing ----
 * Knob page 0: 1 type, 2 channel, 3 number, 4 on value (velocity for notes); page 1: 1 off value, 2 colour (base only).
 * A knob: 1 type, 2 channel, 3 CC, 4 start value. */
void uni_edit_pad(uni_pad_t *p, int kpage, int knob, int d, int layer);
void uni_edit_knob(uni_knob_t *k, int knob, int d);
/* the 4 OLED lines (each <= 19 characters) for an edit, and one knob row of the play screen */
void uni_pad_lines(uni_pad_t p, int kpage, int layer, char lines[4][20]);
void uni_knob_lines(uni_knob_t k, char lines[4][20]);
void uni_knob_row(uni_knob_t k, int slot, char out[20]);   /* "2C CC74 ch1" / "2C --" (the value is drawn as a bar) */
extern const char *const UNI_TYPE_NAME[UT_N];
extern const char *const UNI_COL_NAME[UNI_COLS];

/* ---- patches: edits made on the Fire, until saved ----
 * Each slot replaces (or with del = 1, removes) the entry with its key on its page. The newest slot per key wins (main.c
 * keeps one slot per key). uni_patched_len / _read describe the record that results from base (the stored page, or
 * none = an empty page) with the slots of `page` whose state is in `mask` (1 << state) applied: kept base entries in their
 * order (a replaced one in place), then new entries. */
enum { US_FREE, US_EDIT, US_PEND, US_SAVE };
#define UNI_SLOTS 8
typedef struct { int8_t page; uint8_t state, del, key; uint8_t e[UNI_ENT]; } uni_patch_t;
uint32_t uni_patched_len(const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask);
void uni_patched_read(const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask,
                      uint32_t off, uint8_t *dst, uint32_t k);
uint16_t uni_patched_sum(const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask);   /* = uni_sum of it */
/* A CURSOR over that record for the saver, which reads it a word at a time in order: init is O(entries + slots) once,
 * each read then moves on from where the last one stopped (O(1) per word; a read before the cursor starts it over).
 * Every call above is one pass (O(entries + slots)), never one per byte (review 2026-10-27: 7-10 us per word). */
typedef struct {
    const uint8_t *base; uint32_t blen;
    const uni_patch_t *ps; int n, page; unsigned mask;
    uint8_t has[(UNI_KEYS + 7) / 8], slot[(UNI_KEYS + 7) / 8];   /* keys in base / keys with a slot that counts */
    uint32_t bo; int si, idx;                                 /* next base entry, next slot (new entries), output entry now */
    const uint8_t *e;                                         /* that entry's 6 bytes (0 = none / past the end) */
} uni_cur_t;
void uni_cur_init(uni_cur_t *c, const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask);
void uni_cur_read(uni_cur_t *c, uint32_t off, uint8_t *dst, uint32_t k);
#ifdef UNI_TEST
extern unsigned long uni_ops;                /* entries + slots looked at: uni_test counts the work so an O(n)-per-word path is red */
#define UNI_OP() (uni_ops++)
#else
#define UNI_OP() ((void)0)
#endif

/* ---- host writes ---- */
int uni_begin_check(int page, uint32_t len, uint32_t used_others);   /* UR_OK, UR_BAD (range / length), UR_FULL (budget) */
int uni_chunk_check(uint32_t off, uint32_t n, uint32_t done, uint32_t len);   /* UR_OK, UR_ORDER (not the next), UR_BAD */

#endif
