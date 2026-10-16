/* Storage v3: a variable-length append log in flash sector 5 (0x08020000, 128 KB).
 *
 * Records start on 256-byte boundaries:
 *   u32 magic "ORC3", u16 kind, u16 len, u32 seq, u32 crc (payload), u32 commit (0xFFFFFFFF -> 0 last),
 *   u32 reserved x3, then len payload bytes.
 * A record counts only if commit == 0 and the CRC matches, so a save cut off anywhere never loads.
 * Write order: header (CRC + commit blank), payload, CRC, commit. Records of a kind this firmware
 * doesn't know are skipped whole.
 * Each kind (global settings, pattern 0..7) keeps only its newest record. When the area is full,
 * store2_compact erases it and rewrites the current records (done only while stopped).
 */
#ifndef STORE2_H
#define STORE2_H
#include <stdint.h>
#include <stdbool.h>

#define S2_AREA_SIZE (128u * 1024u)
#define S2_KINDS 64                                 /* 0 = global, 1..8 old full patterns, 9..16 v0.10 sparse, 17..48 patterns (v0.11 on) */
#define S2_KIND_GLOBAL 0

/* Hardware hooks (store_hw.c on the Fire, fakes in the host test). */
extern const uint8_t *store_area;
bool store_hw_erase(void);
bool store_hw_program(uint32_t offset, const uint32_t *words, uint32_t nwords);

void store2_scan(void);                             /* index the area (call once at boot, and after writes) */
bool store2_has_any(void);                          /* any valid v3 record at all? */
/* Copy the newest record of `kind` into buf (up to max bytes; shorter records zero-fill the rest). */
bool store2_read(int kind, void *buf, uint32_t max);
const uint8_t *store2_ptr(int kind, uint32_t *len);  /* the newest record in place (flash is readable), 0 = none */
/* Append one record. false = not enough room (compact first) or a write failed. */
bool store2_append(int kind, const void *data, uint32_t len);   /* blocking: begin + all steps */
uint32_t store2_free_bytes(void);

/* Background writing (so a save never stalls playback): begin claims the space; step programs a
 * few words per call, reading the payload from the caller's LIVE data (no copy). If that data
 * changes before the record is done, call store2_cancel: the half-written record never counts.
 * false from begin = no room (compact) or busy. */
#define S2_MAX_RECORD 0xFFFFu                       /* the header's 16-bit length */
typedef void (*s2_reader)(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n);
bool store2_begin_src(int kind, uint32_t len, s2_reader rd, const void *ctx);
bool store2_begin(int kind, const void *data, uint32_t len);   /* from memory */
bool store2_step(uint32_t max_words);               /* true while more remains */
bool store2_busy(void);
int store2_busy_kind(void);                         /* kind being written, -1 none */
void store2_cancel(void);
bool store2_last_failed(void);
bool store2_append_src(int kind, uint32_t len, s2_reader rd, const void *ctx);   /* blocking */
bool store2_erase(void);                            /* erase the area (then append the current state) */
/* Erase and rewrite: items[i] = record for kind kinds[i]. */
bool store2_compact(const int *kinds, const void *const *items, const uint32_t *lens, int n);

#endif
