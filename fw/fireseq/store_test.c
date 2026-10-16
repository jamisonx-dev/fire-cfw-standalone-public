/* Host test for store.c (run on the Pi with plain gcc):
 *   gcc -O2 -Wall -o store_test store_test.c store.c && ./store_test
 * Simulates flash: erase -> all 0xFF, program can only clear bits (like real NOR flash),
 * and a "power cut" that stops a program part-way. */
#include <stdio.h>
#include <string.h>
#include "store.h"

static uint8_t flash[STORE_AREA_SIZE];
const uint8_t *store_area = flash;
static int erases, cut_after_words = -1;
static int fails;

bool store_hw_erase(void) { memset(flash, 0xFF, sizeof flash); erases++; return true; }

bool store_hw_program(uint32_t offset, const uint32_t *words, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (cut_after_words == 0) return false;     /* power cut */
        if (cut_after_words > 0) cut_after_words--;
        uint32_t *dst = (uint32_t *)(flash + offset + 4 * i);
        *dst &= words[i];                           /* NOR flash: 1 -> 0 only */
    }
    return true;
}

#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

static store_data_t make(int tag)
{
    store_data_t d;
    memset(&d, 0, sizeof d);
    d.bpm = (uint16_t)(100 + tag);
    d.cur_pattern = (uint8_t)(tag % 8);
    d.mute_bits = (uint8_t)(tag & 15);
    for (int p = 0; p < STORE_PATTERNS; p++)
        for (int t = 0; t < STORE_TRACKS; t++) d.steps[p][t] = (uint16_t)(tag * 31 + p * 7 + t);
    return d;
}

int main(void)
{
    store_data_t d, got;
    memset(flash, 0xFF, sizeof flash);

    CHECK(!store_load(&got), "blank area: nothing to load");
    CHECK(store_free_records() == 128, "blank area: 128 free records");

    d = make(1);
    CHECK(store_save(&d, false) == STORE_OK, "first save");
    CHECK(store_load(&got) && !memcmp(&got, &d, sizeof d), "loads what was saved");

    for (int i = 2; i <= 128; i++) { d = make(i); store_save(&d, false); }
    CHECK(store_load(&got) && got.bpm == 100 + 128, "after 128 saves: newest loads");
    CHECK(store_free_records() == 0, "area full");

    d = make(200);
    CHECK(store_save(&d, false) == STORE_NEED_ERASE, "full + erase not allowed -> NEED_ERASE");
    CHECK(store_load(&got) && got.bpm == 100 + 128, "...and the old data is untouched");
    CHECK(store_save(&d, true) == STORE_OK && erases == 1, "full + erase allowed -> erases once, saves");
    CHECK(store_load(&got) && got.bpm == 300, "newest after the erase");

    /* power cut part-way through a save: the previous record must still load */
    for (int cut = 0; cut < 80; cut += 3) {
        d = make(400 + cut);
        cut_after_words = cut;
        store_save(&d, false);
        cut_after_words = -1;
        store_data_t before = got;
        CHECK(store_load(&got) && (got.bpm == before.bpm || got.bpm == d.bpm), "power cut mid-save: a good record still loads");
        if (got.bpm == d.bpm) { /* save completed despite the cut point being past its end */ }
        d = make(500 + cut);
        CHECK(store_save(&d, false) == STORE_OK && store_load(&got) && got.bpm == d.bpm, "next save after a cut works");
    }

    /* a corrupted newest record (bit rot) falls back to the one before */
    d = make(600); store_save(&d, false);
    d = make(601); store_save(&d, false);
    uint32_t free_before = store_free_records();
    uint32_t last = 128 - free_before - 1;
    flash[last * STORE_REC_SIZE + 40] ^= 0x01;      /* flip a payload bit (only 1->0 really, but checks the CRC) */
    CHECK(store_load(&got) && got.bpm == 700, "corrupt newest -> previous record loads");

    /* a v1 record (68-byte payload: bpm, cur_pattern, mute_bits, steps) loads into the
       current layout with every newer field zeroed (= defaults) */
    {
        const unsigned V1 = 68;
        store_hw_erase();
        uint32_t rec[STORE_REC_SIZE / 4];
        memset(rec, 0xFF, sizeof rec);
        store_data_t v1 = make(7);
        uint8_t *payload = (uint8_t *)&rec[4];
        memcpy(payload, &v1, V1);
        uint32_t c = 0xFFFFFFFFu;
        for (unsigned i = 0; i < V1; i++) { c ^= payload[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); }
        rec[0] = 0x4643524Fu; rec[1] = 1u | (V1 << 16); rec[2] = 5; rec[3] = ~c;
        memcpy(flash, rec, sizeof rec);
        store_data_t got2;
        memset(&got2, 0xAA, sizeof got2);
        bool ok = store_load(&got2);
        int zero = 1;
        for (unsigned i = V1; i < sizeof got2; i++) if (((uint8_t *)&got2)[i]) zero = 0;
        CHECK(V1 == (unsigned)((uint8_t *)&got2.accent - (uint8_t *)&got2), "v1 layout ends where the v2 fields begin (68 bytes)");
        CHECK(ok && got2.bpm == v1.bpm && got2.mute_bits == v1.mute_bits && !memcmp(got2.steps, v1.steps, sizeof v1.steps), "v1 record loads into the v2 layout (old fields kept)");
        CHECK(ok && zero, "v1 record: the newer fields come back zeroed (= defaults)");
    }

    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails != 0;
}
