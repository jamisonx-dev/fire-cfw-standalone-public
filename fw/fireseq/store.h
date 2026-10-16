/* Pattern storage in flash sector 5 (0x08020000, 128 KB), as an append-only log of 1 KB records.
 * Each save writes a new record after the last one; boot loads the newest record whose
 * checksum is good. Only when the sector is full is it erased (once per 128 saves).
 * The record carries a version so later features can add fields without losing saved data. */
#ifndef STORE_H
#define STORE_H
#include <stdint.h>
#include <stdbool.h>

#define STORE_PATTERNS 8
#define STORE_TRACKS 4
#define STORE_REC_SIZE 1024u
#define STORE_AREA_SIZE (128u * 1024u)

/* Fields are only ever APPENDED: an older (shorter) record loads with the new fields zeroed,
 * and zero always means "default". */
typedef struct {
    /* v1 */
    uint16_t bpm;
    uint8_t cur_pattern;
    uint8_t mute_bits;                              /* bit t = track t muted */
    uint16_t steps[STORE_PATTERNS][STORE_TRACKS];   /* bit s = step s on */
    /* v2 */
    uint16_t accent[STORE_PATTERNS][STORE_TRACKS];  /* bit s = step s accented (velocity 127) */
    uint16_t soft[STORE_PATTERNS][STORE_TRACKS];    /* bit s = step s soft (velocity 60) */
    uint8_t swing;                                  /* 50-75 (%), 0 = 50 */
    uint8_t clock_out;                              /* 1 = send MIDI clock while playing */
    uint8_t track_note[STORE_TRACKS];               /* 0 = default */
    uint8_t track_chan[STORE_TRACKS];               /* 1-16, 0 = default (10) */
    uint8_t reserved[22];
} store_data_t;

/* Hardware hooks (store_hw.c on the Fire, fakes in the host test). */
extern const uint8_t *store_area;                   /* memory-mapped view of the area */
bool store_hw_erase(void);
bool store_hw_program(uint32_t offset, const uint32_t *words, uint32_t nwords);

bool store_load(store_data_t *out);                 /* newest good record; false if none */
/* Append a record. Needs an erase when the area is full: only done if allow_erase. */
typedef enum { STORE_OK, STORE_NEED_ERASE, STORE_FAIL } store_result_t;
store_result_t store_save(const store_data_t *d, bool allow_erase);
uint32_t store_free_records(void);

#endif
