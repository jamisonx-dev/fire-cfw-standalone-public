/* The save area's clean-up (v0.31): erase sector 5 and rewrite what counts: the settings record, every pattern that has
 * something, and the UNIVERSAL pages. The pages live only in flash, so they are first copied into RAM the caller lends
 * (main.c: the undo buffers + the MIDI batch buffer). Pure on top of store2 + uni: compact_test.c runs THIS code against
 * a real save area read off the Fire. */
#ifndef COMPACT_H
#define COMPACT_H
#include <stdint.h>
#include <stdbool.h>
#include "store2.h"

#define CP_KIND_PAT 17                       /* main.c KIND_PAT(0) */
#define CP_KIND_UNI 49                       /* main.c KIND_UNI(0) */
#define CP_KIND_PAINT 57                     /* v0.37 main.c KIND_PAINT(0): the saved paintings */
#define CP_PAINTS 4                          /* = paint.h PAINT_SLOTS */
#define CP_PAINT_LEN 32                      /* = paint.h PAINT_LEN */
#define CP_KIND_CRASH 61                     /* v0.39 main.c KIND_CRASH: the last crash, kept through a power-off */
#define CP_CRASH_LEN 64
typedef struct {
    const void *global; uint32_t global_len;            /* the settings record (kind 0) */
    int npat;                                            /* patterns 0 .. npat-1: kind CP_KIND_PAT + p */
    bool (*pat_keep)(int p);                             /* it has something to write */
    uint32_t (*pat_len)(int p);
    s2_reader pat_reader; const int *pat_ids;            /* pat_reader(&pat_ids[p], ...) */
    uint8_t *sa; uint32_t sa_len, sb_len; uint8_t *sb;   /* the scratch for the pages, in two pieces */
    const uint8_t *paint; uint8_t paint_has;             /* v0.37: CP_PAINTS paintings in RAM (CP_PAINT_LEN each), bit s = slot s kept; 0 = none */
    const void *crash;                                   /* v0.39: the crash record (CP_CRASH_LEN bytes) to keep, 0 = none */
} compact_t;
enum { CP_REFUSED = -1, CP_FAIL = 0, CP_OK = 1 };
/* A page record the clean-up keeps: a known version, a valid length, every field valid (uni_check). Anything else
 * (a junk or cut-off record) is dropped there, so it can never eat the budget. */
bool compact_page_ok(const uint8_t *r, uint32_t len);
/* CP_REFUSED = the pages would not fit the scratch: NOTHING was touched (no copy, no erase). *kept = page bytes copied. */
int compact_run(const compact_t *c, uint32_t *kept);

#endif
