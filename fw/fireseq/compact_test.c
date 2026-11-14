/* The save area's clean-up (compact.c, THE code main.c runs) with UNIVERSAL pages: kept byte for byte, junk page records
 * dropped, a clean-up that can't keep the pages refused before anything is touched. With a full-chip backup of the Fire
 * (argument 1), also against the owner's REAL save area: 8 pages at the budget, the area filled, a clean-up, and every
 * pattern + the settings byte-identical after it (from the 2026-10-27 review's rv_compact_test.c). Without one, those
 * checks print SKIPPED (counted, so the total stays fixed).
 *   gcc -O2 -Wall -fsanitize=address,undefined -I. compact_test.c compact.c store2.c steps.c uni.c -o ct && ./ct [fullchip.bin] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "steps.h"
#include "store2.h"
#include "uni.h"
#include "compact.h"

static uint8_t flash[S2_AREA_SIZE];
const uint8_t *store_area = flash;
static int erases;
bool store_hw_erase(void) { memset(flash, 0xFF, sizeof flash); erases++; return true; }
bool store_hw_program(uint32_t off, const uint32_t *w, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) { uint32_t d; memcpy(&d, flash + off + 4 * i, 4); d &= w[i]; memcpy(flash + off + 4 * i, &d, 4); }
    return true;
}
static int fails, checks, skipped;
#define EXPECTED 17
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define SKIP(msg) do { checks++; skipped++; printf("SKIPPED: %s\n", msg); } while (0)

/* main.c's scratch: the undo buffers (2 x 1536) + the MIDI batch buffer (1024) */
static uint8_t undo_buf[2][1536], batch[1024];
static uint8_t G[568];
static const int PAT_ID[ST_PATTERNS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
static bool pat_keep(int p)                          /* main.c pat_keep_cb */
{
    if (steps_pattern_size(p)) return true;
    for (int t = 0; t < ST_TRACKS; t++) if (steps_len[p][t]) return true;
    return false;
}
static uint32_t pat_len(int p) { return steps_record_len(p); }
static void pat_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { steps_record_read(*(const int *)ctx, off, dst, n); }
static const compact_t C = {G, sizeof G, ST_PATTERNS, pat_keep, pat_len, pat_reader, PAT_ID,
                            &undo_buf[0][0], sizeof undo_buf, sizeof batch, batch};

static uint32_t make_page(uint8_t *r, int p, uint32_t want)   /* a valid page of about `want` bytes */
{
    r[0] = UNI_VER; memcpy(r + 1, "PAGE    ", 8); r[5] = (uint8_t)('1' + p);
    uint32_t n = (want - UNI_HDR) / UNI_ENT, o = UNI_HDR;
    for (uint32_t i = 0; i < n && i < UNI_KEYS; i++, o += UNI_ENT) {
        if (i < 128) { uni_pad_t pp = {(uint8_t)(UT_NOTE + (int)(i % 6)), (uint8_t)(i & 15), (uint8_t)(i & 127), 100, 0, (uint8_t)(i % 17)}; uni_pad_enc(r + o, (int)(i / 2), (int)(i & 1), pp); }
        else { uni_knob_t kk = {UK_CC, 1, (uint8_t)i, 64}; uni_knob_enc(r + o, (int)(i - 128), kk); }
    }
    return o;
}
static uint8_t pg[UNI_PAGES][UNI_MAX_LEN];
static uint32_t pglen[UNI_PAGES];

static void synthetic(void)
{
    store_hw_erase(); store2_scan(); steps_clear_all();
    memset(G, 0x5A, sizeof G);
    steps_set(0, 0, 0, 2, 0); steps_set(0, 2, 7, 3, 0); steps_set(3, 17, 5, 2, 60);
    uint8_t junk[UNI_HDR]; memset(junk, 0xFF, sizeof junk);
    pglen[0] = make_page(pg[0], 0, 489);
    memcpy(pg[1], pg[0], 30); memcpy(pg[1] + 30, pg[0] + 9, 6);   /* a valid header, a duplicate entry: invalid */
    CHECK(store2_append(0, G, sizeof G) && store2_append(CP_KIND_PAT, "x", 1) && store2_append(CP_KIND_UNI + 0, pg[0], pglen[0])
          && store2_append(CP_KIND_UNI + 6, pg[1], 36) && store2_append(CP_KIND_UNI + 7, junk, sizeof junk), "a save area with a page, an invalid page, a 0xFF page");
    CHECK(compact_page_ok(pg[0], pglen[0]) && !compact_page_ok(pg[1], 36) && !compact_page_ok(junk, sizeof junk) && !compact_page_ok(0, 9),
          "which page records count: a valid one only (a junk / invalid one never eats the budget)");
    uint32_t kept = 0; int e0 = erases;
    int r = compact_run(&C, &kept);
    store2_scan();
    uint32_t l;
    const uint8_t *p0 = store2_ptr(CP_KIND_UNI, &l);
    CHECK(r == CP_OK && erases == e0 + 1 && kept == pglen[0] && p0 && l == pglen[0] && !memcmp(p0, pg[0], l)
          && !store2_ptr(CP_KIND_UNI + 6, &l) && !store2_ptr(CP_KIND_UNI + 7, &l), "the clean-up keeps the valid page byte for byte and drops the junk");
    uint8_t g2[sizeof G];
    int pats = 1;
    for (int p = 0; p < ST_PATTERNS; p++) {
        const uint8_t *rec = store2_ptr(CP_KIND_PAT + p, &l);
        if (pat_keep(p) != (rec != 0)) pats = 0;
        if (rec) { static uint8_t b[S2_MAX_RECORD]; steps_record_read(p, 0, b, steps_record_len(p)); if (l != steps_record_len(p) || memcmp(rec, b, l)) pats = 0; }
    }
    CHECK(pats && store2_read(0, g2, sizeof g2) && !memcmp(g2, G, sizeof G), "patterns 1 + 4 and the settings rewritten (the stray record dropped), empty patterns not");
    /* pages that would not fit the scratch (written past the budget): refused, NOTHING touched */
    store_hw_erase(); store2_scan();
    uint32_t total = 0;
    for (int p = 0; p < UNI_PAGES; p++) { pglen[p] = make_page(pg[p], p, UNI_MAX_LEN); store2_append(CP_KIND_UNI + p, pg[p], pglen[p]); total += pglen[p]; }
    static uint8_t before[S2_AREA_SIZE]; memcpy(before, flash, sizeof flash);
    memset(undo_buf, 0x11, sizeof undo_buf);
    e0 = erases;
    r = compact_run(&C, &kept);
    CHECK(total > sizeof undo_buf + sizeof batch && r == CP_REFUSED && erases == e0 && !memcmp(before, flash, sizeof flash) && undo_buf[0][0] == 0x11,
          "pages too big for the scratch: refused before anything (flash, undo) is touched");
    /* exactly the budget: kept */
    store_hw_erase(); store2_scan();
    total = 0;
    for (int p = 0; p < UNI_PAGES; p++) {
        uint32_t want = p < 7 ? 489 : UNI_BUDGET - total;
        pglen[p] = make_page(pg[p], p, want); total += pglen[p];
        store2_append(CP_KIND_UNI + p, pg[p], pglen[p]);
    }
    r = compact_run(&C, &kept);
    store2_scan();
    int same = 1;
    for (int p = 0; p < UNI_PAGES; p++) { const uint8_t *q = store2_ptr(CP_KIND_UNI + p, &l); if (!q || l != pglen[p] || memcmp(q, pg[p], l)) same = 0; }
    CHECK(r == CP_OK && same && total <= UNI_BUDGET && total > UNI_BUDGET - UNI_ENT, "8 pages at the budget (%u B) kept byte for byte", total);
    /* v0.37 the paintings: written from their RAM copies (the page scratch is full), only the slots marked kept */
    static uint8_t paint[CP_PAINTS][CP_PAINT_LEN];
    for (int k = 0; k < CP_PAINTS; k++) for (int i = 0; i < CP_PAINT_LEN; i++) paint[k][i] = (uint8_t)(k * 40 + i);
    store2_append(CP_KIND_PAINT + 1, paint[1], CP_PAINT_LEN);   /* an old record of a slot not kept */
    compact_t C2 = C; C2.paint = &paint[0][0]; C2.paint_has = 0x05;
    r = compact_run(&C2, &kept);
    store2_scan();
    const uint8_t *a = store2_ptr(CP_KIND_PAINT + 0, &l); uint32_t la = l;
    const uint8_t *c3 = store2_ptr(CP_KIND_PAINT + 2, &l); uint32_t lc = l;
    same = 1;
    for (int p = 0; p < UNI_PAGES; p++) { const uint8_t *q = store2_ptr(CP_KIND_UNI + p, &l); if (!q || l != pglen[p] || memcmp(q, pg[p], l)) same = 0; }
    CHECK(r == CP_OK && same && a && la == CP_PAINT_LEN && !memcmp(a, paint[0], la) && c3 && lc == CP_PAINT_LEN && !memcmp(c3, paint[2], lc)
          && !store2_ptr(CP_KIND_PAINT + 1, &l) && !store2_ptr(CP_KIND_PAINT + 3, &l), "paintings 1 + 3 kept byte for byte beside 8 full pages, the others not");
    /* v0.39 the crash record (kind 61): kept byte for byte when given, beside the full pages and the paintings */
    static uint8_t crash[CP_CRASH_LEN];
    for (int i = 0; i < CP_CRASH_LEN; i++) crash[i] = (uint8_t)(200 - i);
    compact_t C3 = C2; C3.crash = crash;
    r = compact_run(&C3, &kept);
    store2_scan();
    const uint8_t *cr = store2_ptr(CP_KIND_CRASH, &l);
    CHECK(r == CP_OK && cr && l == CP_CRASH_LEN && !memcmp(cr, crash, l) && store2_ptr(CP_KIND_PAINT + 0, &l),
          "the crash record kept byte for byte beside 8 full pages and the paintings");
    r = compact_run(&C, &kept);
    store2_scan();
    CHECK(r == CP_OK && !store2_ptr(CP_KIND_PAINT, &l) && !store2_ptr(CP_KIND_PAINT + 2, &l) && !store2_ptr(CP_KIND_CRASH, &l),
          "no paintings / crash given: none written");
}

static uint8_t orig_pat[ST_PATTERNS][S2_MAX_RECORD + 1];
static uint32_t orig_len[ST_PATTERNS];
static bool had[ST_PATTERNS];

static void real(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : 0;
    if (!f) {
        for (int i = 0; i < 8; i++) SKIP(path ? "no full-chip backup at that path" : "no full-chip backup given");
        return;
    }
    fseek(f, 0x20000, SEEK_SET);                     /* sector 5 */
    size_t got = fread(flash, 1, sizeof flash, f); fclose(f);
    CHECK(got == sizeof flash, "read the real save area (128 KB at 0x20000)");
    store2_scan();
    printf("real area: %u of %u bytes used\n", S2_AREA_SIZE - store2_free_bytes(), S2_AREA_SIZE);
    store2_read(S2_KIND_GLOBAL, G, sizeof G);
    steps_clear_all();
    int npat = 0, loads = 1, nopages = 1;
    for (int p = 0; p < ST_PATTERNS; p++) {
        uint32_t len; const uint8_t *r = store2_ptr(CP_KIND_PAT + p, &len);
        if (!r) continue;
        had[p] = true; npat++;
        if (!steps_record_load(p, r, len)) loads = 0;
        orig_len[p] = steps_record_len(p); steps_record_read(p, 0, orig_pat[p], orig_len[p]);   /* what a save writes (v3) */
    }
    for (int k = 0; k < UNI_PAGES; k++) { uint32_t l; if (store2_ptr(CP_KIND_UNI + k, &l)) nopages = 0; }
    CHECK(npat > 0 && loads, "his %d patterns load", npat);
    CHECK(nopages, "no page records on pre-v0.31 data");
    uint32_t total = 0; int valid = 1;
    for (int p = 0; p < UNI_PAGES; p++) {
        uint32_t want = p < 7 ? 489 : UNI_BUDGET - total;
        pglen[p] = make_page(pg[p], p, want); total += pglen[p];
        if (uni_check(pg[p], pglen[p]) != UR_OK || !store2_append(CP_KIND_UNI + p, pg[p], pglen[p])) valid = 0;
    }
    CHECK(valid && total <= UNI_BUDGET, "8 valid pages at the budget (%u B) added", total);
    while (store2_append(CP_KIND_PAT, orig_pat[0], had[0] ? orig_len[0] : 9)) {}   /* old versions until the area is full */
    if (had[0]) { uint32_t l; const uint8_t *r = store2_ptr(CP_KIND_PAT, &l); (void)r; (void)l; }
    if (!had[0]) { steps_clear_all(); for (int p = 0; p < ST_PATTERNS; p++) if (had[p]) steps_record_load(p, orig_pat[p], orig_len[p]); }
    uint32_t kept = 0;
    int r = compact_run(&C, &kept);
    CHECK(r == CP_OK && kept == total, "the area full: the clean-up runs (pages kept %u B)", kept);
    store2_scan();                                    /* a reboot */
    int pats = 1;
    for (int p = 0; p < ST_PATTERNS; p++) {
        uint32_t len; const uint8_t *q = store2_ptr(CP_KIND_PAT + p, &len);
        if (had[p] && !(q && len == orig_len[p] && !memcmp(q, orig_pat[p], len))) { pats = 0; printf("  pattern %d differs\n", p + 1); }
    }
    CHECK(pats, "every one of his patterns byte-identical after it");
    int same = 1;
    for (int p = 0; p < UNI_PAGES; p++) { uint32_t l; const uint8_t *q = store2_ptr(CP_KIND_UNI + p, &l); if (!q || l != pglen[p] || memcmp(q, pg[p], l)) same = 0; }
    CHECK(same, "the 8 pages byte-identical after it");
    uint8_t g2[sizeof G];
    steps_clear_all();
    int reload = 1;
    for (int p = 0; p < ST_PATTERNS; p++) {
        uint32_t len; const uint8_t *q = store2_ptr(CP_KIND_PAT + p, &len);
        if (q && !steps_record_load(p, q, len)) reload = 0;
        if (had[p]) { static uint8_t b[S2_MAX_RECORD]; uint32_t n = steps_record_len(p); steps_record_read(p, 0, b, n); if (n != orig_len[p] || memcmp(b, orig_pat[p], n)) reload = 0; }
    }
    CHECK(reload && store2_read(0, g2, sizeof g2) && !memcmp(g2, G, sizeof G), "the settings identical, and the patterns reload identical");
}

int main(int argc, char **argv)
{
    synthetic();
    real(argc > 1 ? argv[1] : 0);
    printf("compact_test: %s (%d checks, %d skipped%s)\n", fails ? "FAILED" : "all passed", checks, skipped,
           checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
