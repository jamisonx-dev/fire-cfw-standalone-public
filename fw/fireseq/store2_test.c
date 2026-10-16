/* Host test for store2.c (and the legacy v1/v2 loader used for migration), on the Pi:
 *   gcc -O2 -Wall -o store2_test store2_test.c store2.c store.c && ./store2_test
 * Fake NOR flash: erase -> 0xFF, programming only clears bits, optional power cut after N words. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "store2.h"
#include "store.h"

static uint8_t flash[S2_AREA_SIZE];
const uint8_t *store_area = flash;
static int erases, cut_after = -1, fails, checks;

bool store_hw_erase(void) { memset(flash, 0xFF, sizeof flash); erases++; return true; }
bool store_hw_program(uint32_t off, const uint32_t *w, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (cut_after == 0) return false;
        if (cut_after > 0) cut_after--;
        uint32_t *d = (uint32_t *)(flash + off + 4 * i);
        *d &= w[i];
    }
    return true;
}

#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static uint8_t pat_a[2064], pat_b[2064], got[2064];

static void fill(uint8_t *p, int seed) { srand((unsigned)seed); for (int i = 0; i < 2064; i++) p[i] = (uint8_t)rand(); }

int main(void)
{
    memset(flash, 0xFF, sizeof flash);
    store2_scan();
    CHECK(!store2_has_any(), "blank: nothing");
    CHECK(store2_free_bytes() == S2_AREA_SIZE, "blank: all free");

    uint8_t g1[64]; memset(g1, 0x11, sizeof g1);
    fill(pat_a, 1);
    CHECK(store2_append(S2_KIND_GLOBAL, g1, sizeof g1), "append global");
    CHECK(store2_append(1, pat_a, sizeof pat_a), "append pattern 1");
    CHECK(store2_read(1, got, sizeof got) && !memcmp(got, pat_a, sizeof pat_a), "pattern 1 reads back");
    uint8_t g2[64];
    CHECK(store2_read(S2_KIND_GLOBAL, g2, sizeof g2) && !memcmp(g1, g2, sizeof g1), "global reads back");

    fill(pat_b, 2);
    store2_append(1, pat_b, sizeof pat_b);
    CHECK(store2_read(1, got, sizeof got) && !memcmp(got, pat_b, sizeof pat_b), "newest pattern 1 wins");
    CHECK(store2_read(S2_KIND_GLOBAL, g2, sizeof g2) && !memcmp(g1, g2, sizeof g1), "global untouched by pattern save");

    /* shorter stored record zero-fills a larger read (fields appended later = defaults) */
    uint8_t big[100]; memset(big, 0xEE, sizeof big);
    CHECK(store2_read(S2_KIND_GLOBAL, big, sizeof big) && big[63] == 0x11 && big[64] == 0 && big[99] == 0, "short record zero-fills");

    /* power cut at every word of a pattern save: the previous version must still load */
    int cut_ok = 1;
    for (int cut = 0; cut < (int)(32 + sizeof pat_a) / 4 + 2; cut++) {
        uint8_t before[2064]; store2_read(1, before, sizeof before);
        uint8_t next[2064]; fill(next, 1000 + cut);
        cut_after = cut;
        bool r = store2_append(1, next, sizeof next);
        cut_after = -1;
        store2_scan();
        store2_read(1, got, sizeof got);
        if (!(r ? !memcmp(got, next, sizeof next) : !memcmp(got, before, sizeof before))) cut_ok = 0;
        if (store2_free_bytes() < 4096) {           /* keep room: compact with the current state */
            int kinds[2] = {S2_KIND_GLOBAL, 1};
            const void *items[2] = {g1, got};
            uint32_t lens[2] = {sizeof g1, sizeof got};
            store2_compact(kinds, items, lens, 2);
        }
    }
    CHECK(cut_ok, "power cut at every word: the previous version loads, or the new one if it completed");

    /* after a cut-off save, the next save still works */
    fill(pat_a, 77);
    CHECK(store2_append(1, pat_a, sizeof pat_a) && store2_read(1, got, sizeof got) && !memcmp(got, pat_a, sizeof pat_a), "save after cuts works");

    /* a header cut after its first word: the scan skips the stub and later saves still land */
    store2_scan();
    uint32_t stub = S2_AREA_SIZE - store2_free_bytes();
    cut_after = 1;
    store2_append(2, pat_b, sizeof pat_b);
    cut_after = -1;
    store2_scan();
    CHECK(S2_AREA_SIZE - store2_free_bytes() > stub, "header stub is skipped, not reused");
    CHECK(store2_append(2, pat_b, sizeof pat_b) && store2_read(2, got, sizeof got) && !memcmp(got, pat_b, sizeof pat_b), "save after a header stub works");

    /* v0.10: a save cancelled half-way (its data changed underneath) never counts */
    {
        uint8_t before[2064]; store2_read(1, before, sizeof before);
        uint8_t live[2064]; fill(live, 4242);
        CHECK(store2_begin(1, live, sizeof live), "begin from live data");
        store2_step(100);                           /* header + part of the payload */
        live[1000] ^= 0xFF;                         /* edited mid-save -> the caller cancels */
        store2_cancel();
        CHECK(!store2_busy(), "cancel stops the writer");
        store2_scan();
        CHECK(store2_read(1, got, sizeof got) && !memcmp(got, before, sizeof before), "cancelled save: the previous version still loads");
        CHECK(store2_append(1, live, sizeof live) && store2_read(1, got, sizeof got) && !memcmp(got, live, sizeof live), "the retry saves the edited data");
        store2_scan();
        CHECK(store2_read(1, got, sizeof got) && !memcmp(got, live, sizeof live), "and it survives a rescan (CRC written last is right)");
    }
    /* v0.12: a save cancelled BEFORE its first word gives its space back (no blank hole) */
    {
        uint32_t used0 = S2_AREA_SIZE - store2_free_bytes();
        uint8_t live[300]; fill(live, 77);
        CHECK(store2_begin(4, live, sizeof live), "begin");
        store2_cancel();                            /* nothing written yet */
        CHECK(S2_AREA_SIZE - store2_free_bytes() == used0, "cancel before the first word: the space is given back");
        uint8_t later[200]; fill(later, 78);
        CHECK(store2_append(4, later, sizeof later), "the next save");
        store2_scan();
        CHECK(store2_read(4, got, sizeof later) && !memcmp(got, later, sizeof later), "and it loads after a reboot");
    }
    /* a blank hole in the middle (e.g. left by older firmware) does not hide the records after it */
    {
        uint8_t r1[100], r2[100]; fill(r1, 81); fill(r2, 82);
        CHECK(store2_append(6, r1, sizeof r1), "record before the hole");
        store2_scan();
        uint32_t hole = S2_AREA_SIZE - store2_free_bytes();
        cut_after = 0;                              /* claim space, write nothing: a hole */
        store2_append(7, r1, sizeof r1);
        cut_after = -1;
        /* make it a true hole as older firmware could (claim kept), then write after it by hand */
        uint32_t after = hole + 256;
        uint32_t c = 0xFFFFFFFFu;
        for (unsigned i = 0; i < sizeof r2; i++) { c ^= r2[i]; for (int j = 0; j < 8; j++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
        uint32_t hdr[8] = {0x3343524Fu, 6u | (uint32_t)sizeof r2 << 16, 900000, ~c, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        store_hw_program(after, hdr, 8);
        store_hw_program(after + 32, (const uint32_t *)r2, sizeof r2 / 4);
        store2_scan();
        CHECK(store2_read(6, got, sizeof r2) && !memcmp(got, r2, sizeof r2), "a record after a blank hole still loads");
        CHECK(S2_AREA_SIZE - store2_free_bytes() == after + 256, "and the free space starts after it");
    }
    /* a header cut after its first word: the scan skips one block, not 64 KB */
    {
        uint32_t at = S2_AREA_SIZE - store2_free_bytes();
        uint32_t magic = 0x3343524Fu;
        store_hw_program(at, &magic, 1);
        store2_scan();
        CHECK(S2_AREA_SIZE - store2_free_bytes() == at + 256, "a magic-only header costs one block");
        uint8_t r3[64]; fill(r3, 83);
        CHECK(store2_append(8, r3, sizeof r3), "save after it");
        store2_scan();
        CHECK(store2_read(8, got, sizeof r3) && !memcmp(got, r3, sizeof r3), "and it loads");
    }

    /* the writer reads the payload live: a reader callback, odd length */
    {
        static uint8_t src[777]; fill(src, 9);
        CHECK(store2_append(5, src, sizeof src), "odd-length record");
        store2_scan();
        uint32_t len = 0; const uint8_t *q = store2_ptr(5, &len);
        CHECK(q && len == sizeof src && !memcmp(q, src, sizeof src), "store2_ptr gives the record in place");
    }
    /* a record of a kind this firmware doesn't know is skipped whole; later records still load */
    {
        uint8_t g3[64]; memset(g3, 0x33, sizeof g3);
        int k = S2_KINDS + 3;
        uint8_t tmp[300]; memset(tmp, 0x5A, sizeof tmp);
        /* write it by hand (store2 refuses unknown kinds): header, payload, CRC, commit */
        uint32_t off = S2_AREA_SIZE - store2_free_bytes();
        uint32_t c = 0xFFFFFFFFu;                   /* a VALID CRC, so only the kind check can reject it */
        for (unsigned i = 0; i < sizeof tmp; i++) { c ^= tmp[i]; for (int j = 0; j < 8; j++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
        uint32_t hdr[8] = {0x3343524Fu, (uint32_t)k | (uint32_t)sizeof tmp << 16, 999999, ~c, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        store_hw_program(off, hdr, 8);
        store_hw_program(off + 32, (const uint32_t *)tmp, sizeof tmp / 4);
        store2_scan();
        CHECK(store2_append(S2_KIND_GLOBAL, g3, sizeof g3), "append after a foreign-kind record");
        store2_scan();
        CHECK(store2_read(S2_KIND_GLOBAL, g2, sizeof g2) && !memcmp(g2, g3, sizeof g3), "foreign kind skipped, the next record found");
        store2_scan();
        CHECK(store2_free_bytes() < S2_AREA_SIZE && store2_read(1, got, sizeof got), "the index is intact after scanning a foreign kind");
        uint8_t g4[64]; memset(g4, 0x44, sizeof g4);
        CHECK(store2_append(S2_KIND_GLOBAL, g4, sizeof g4), "a later save gets a newer sequence than the foreign record");
        store2_scan();
        CHECK(store2_read(S2_KIND_GLOBAL, g2, sizeof g2) && !memcmp(g2, g4, sizeof g4), "and is the one that loads");
        CHECK(store2_append(S2_KIND_GLOBAL, g1, sizeof g1), "restore global");
    }

    /* fill until full, then compact */
    int n = 0;
    while (store2_append(3, pat_a, sizeof pat_a)) n++;
    CHECK(n > 10 && store2_free_bytes() < 2400, "fills up, then refuses");
    int kinds[3] = {S2_KIND_GLOBAL, 1, 2};
    const void *items[3] = {g1, pat_a, pat_b};
    uint32_t lens[3] = {sizeof g1, sizeof pat_a, sizeof pat_b};
    int e0 = erases;
    CHECK(store2_compact(kinds, items, lens, 3) && erases == e0 + 1, "compact erases once");
    CHECK(store2_read(1, got, sizeof got) && !memcmp(got, pat_a, sizeof pat_a), "after compact: pattern 1");
    CHECK(store2_read(2, got, sizeof got) && !memcmp(got, pat_b, sizeof pat_b), "after compact: pattern 2");
    CHECK(!store2_read(3, got, sizeof got), "after compact: dropped kinds are gone");
    CHECK(store2_free_bytes() > S2_AREA_SIZE - 5000, "after compact: space is back");

    /* migration: an area holding only old v2 records is not v3 data, but the legacy loader reads it */
    store_hw_erase();
    store_data_t old; memset(&old, 0, sizeof old);
    old.bpm = 97; old.cur_pattern = 3; old.steps[3][0] = 0x1111; old.swing = 60; old.track_note[1] = 40;
    for (int i = 0; i < 5; i++) { old.bpm = (uint16_t)(90 + i); store_save(&old, false); }
    store2_scan();
    CHECK(!store2_has_any(), "old-format area: no v3 records");
    store_data_t back;
    CHECK(store_load(&back) && back.bpm == 94 && back.steps[3][0] == 0x1111 && back.swing == 60 && back.track_note[1] == 40, "legacy loader reads the old saves (newest)");

    printf("%d checks, %s\n", checks, fails ? "FAILED" : "ALL PASSED");
    return fails != 0;
}
