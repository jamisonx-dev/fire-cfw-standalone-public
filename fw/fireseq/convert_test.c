/* Conversion test against a REAL save area read off the Fire (sector 5, 128 KB), on the Pi:
 *   gcc -O2 -Wall -fsanitize=bounds -fsanitize-undefined-trap-on-error \
 *       -o convert_test convert_test.c steps.c store2.c && ./convert_test <sector5.bin>
 * For each pattern it picks the newest format present, the same way the firmware's load_state does
 * (v0.11 record kinds 17-24, else v0.10 sparse kinds 9-16, else full-size kinds 1-8), decodes it here
 * DIRECTLY (independent code), loads it through steps.c, and compares every step. Then it writes
 * v0.11 records the way the firmware does (appended, and again after a compaction) and checks they
 * load back the same. If the area already holds v0.11 records next to older ones (read back after
 * the first boot of v0.11), the Fire's own conversion is checked too. The input is never modified. */
#include <stdio.h>
#include <string.h>
#include "steps.h"
#include "store2.h"

static uint8_t flash[S2_AREA_SIZE];
const uint8_t *store_area = flash;
bool store_hw_erase(void) { memset(flash, 0xFF, sizeof flash); return true; }
bool store_hw_program(uint32_t off, const uint32_t *w, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) { uint32_t *d = (uint32_t *)(flash + off + 4 * i); *d &= w[i]; }
    return true;
}

static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static step_t truth[ST_PATTERNS][ST_TRACKS][ST_MAXSTEPS];
static uint8_t tlen_[ST_PATTERNS][ST_TRACKS];
static bool have[ST_PATTERNS];
static int mel(int t) { return t >= ST_MEL_FIRST; }

/* independent decoders */
static void decode_full(int p, const uint8_t *r, uint32_t len)
{
    memset(truth[p], 0, sizeof truth[p]); memset(tlen_[p], 0, sizeof tlen_[p]);
    for (int t = 0; t < 16; t++) {
        tlen_[p][t] = r[t];
        for (int s = 0; s < 128; s++) { uint8_t b = r[16 + t * 128 + s]; if (b & 3) truth[p][t][s].b = b; }
    }
    if (len >= 4120)
        for (int i = 0; i < 8; i++) {
            tlen_[p][16 + i] = r[2064 + i];
            for (int s = 0; s < 128; s++) { uint8_t b = r[2072 + i * 128 + s]; if (b & 3) { truth[p][16 + i][s].b = b; truth[p][16 + i][s].note = r[3096 + i * 128 + s]; } }
        }
}
static void decode_sparse(int p, const uint8_t *r, uint32_t hdr, uint32_t dsz, uint32_t msz)
{
    memset(truth[p], 0, sizeof truth[p]);
    const uint8_t *lens = r + hdr - 408, *maps = lens + 24, *d = r + hdr;
    for (int t = 0; t < 24; t++) tlen_[p][t] = lens[t];
    for (int t = 0; t < 24; t++)
        for (int s = 0; s < 128; s++) {
            const uint8_t *q = maps + t * 16 + (s / 32) * 4;
            uint32_t w = (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24;
            if (!(w >> (s % 32) & 1)) continue;
            uint32_t sz = mel(t) ? msz : dsz;
            uint8_t e[8] = {0}; memcpy(e, d, sz > 8 ? 8 : sz); d += sz;
            step_t *v = &truth[p][t][s];
            v->b = e[0];
            if (mel(t)) { v->note = e[1]; v->cond = e[2]; v->lock = e[3]; v->ch2 = (int8_t)e[4]; v->ch3 = (int8_t)e[5]; }
            else { v->cond = e[1]; v->lock = e[2]; }
        }
}

static int same(int p)
{
    for (int t = 0; t < ST_TRACKS; t++) {
        uint8_t l = tlen_[p][t] <= 128 ? tlen_[p][t] : 0;
        if (steps_len[p][t] != l) return 0;
        for (int s = 0; s < ST_MAXSTEPS; s++) { step_t v; steps_read(p, t, s, &v); if (memcmp(&v, &truth[p][t][s], sizeof v)) return 0; }
    }
    return 1;
}

static const int PID[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static void rd(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { steps_record_read(*(const int *)ctx, off, dst, n); }

static int reload_all_match(void)
{
    store2_scan();
    steps_clear_all();
    for (int p = 0; p < ST_PATTERNS; p++) {
        if (!have[p]) continue;
        uint32_t len = 0; const uint8_t *r = store2_ptr(17 + p, &len);
        if (!r || !steps_record_load(p, r, len) || !same(p)) return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: convert_test <sector5.bin>\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(flash, 1, sizeof flash, f) != sizeof flash) { printf("can't read %s\n", argv[1]); return 2; }
    fclose(f);
    store2_scan();
    uint8_t G[128];
    CHECK(store2_read(0, G, sizeof G), "the global record is there");
    printf("save area: %u bytes free; global: bpm %u, pattern %u\n", store2_free_bytes(), G[0] | G[1] << 8, G[2] + 1);

    steps_clear_all();
    int total = 0, fw_v2 = 0, fw_v2_ok = 1;
    for (int p = 0; p < ST_PATTERNS; p++) {
        uint32_t len = 0;
        const uint8_t *r2 = store2_ptr(17 + p, &len); uint32_t len2 = len;
        const uint8_t *r1 = p < 8 ? store2_ptr(9 + p, &len) : 0; uint32_t len1 = len;   /* old formats: patterns 1-8 only */
        const uint8_t *r0 = p < 8 ? store2_ptr(1 + p, &len) : 0; uint32_t len0 = len;
        const char *src; bool loaded;
        if (r2) { decode_sparse(p, r2, ST_REC_HDR, r2[1], r2[2]); loaded = steps_record_load(p, r2, len2); src = "v0.11"; }
        else if (r1) { decode_sparse(p, r1, ST_REC_HDR_V1, 1, 2); loaded = steps_record_load_v1(p, r1, len1); src = "v0.10"; }
        else if (r0) { decode_full(p, r0, len0); loaded = steps_convert_full(p, r0, len0); src = "full-size"; }
        else { printf("pattern %d: no record\n", p + 1); continue; }
        have[p] = true;
        int lit = 0;
        for (int t = 0; t < ST_TRACKS; t++) for (int s = 0; s < 128; s++) if (truth[p][t][s].b & 3) lit++;
        total += lit;
        printf("pattern %d: from the %s record, %d lit steps -> %u-byte v0.11 record\n", p + 1, src, lit, steps_record_len(p));
        CHECK(loaded, "the record loads");
        CHECK(same(p), "every step matches a direct decode of the record");
        if (r2 && r1) {                             /* the Fire converted v0.10 -> v0.11 itself */
            fw_v2++;
            static step_t keep[ST_TRACKS][ST_MAXSTEPS]; static uint8_t keepl[ST_TRACKS];
            memcpy(keep, truth[p], sizeof keep); memcpy(keepl, tlen_[p], sizeof keepl);
            decode_sparse(p, r1, ST_REC_HDR_V1, 1, 2);
            if (memcmp(keep, truth[p], sizeof keep) || memcmp(keepl, tlen_[p], sizeof keepl)) fw_v2_ok = 0;
            memcpy(truth[p], keep, sizeof keep); memcpy(tlen_[p], keepl, sizeof keepl);
        }
    }
    printf("total lit steps %d, pool used %u of %u bytes\n", total, steps_used(), ST_POOL);
    if (fw_v2)                                      /* information only: edits made after the conversion show up here too */
        printf("the Fire wrote %d v0.11 records next to v0.10 ones: %s\n", fw_v2,
               fw_v2_ok ? "identical steps" : "they differ (edits since the conversion, or a conversion bug: diff them)");

    for (int p = 0; p < ST_PATTERNS; p++)
        if (have[p]) CHECK(store2_append_src(17 + p, steps_record_len(p), rd, &PID[p]), "v0.11 record appends");
    CHECK(reload_all_match(), "reboot: the v0.11 records load and match");

    store2_erase();
    CHECK(store2_append(0, G, sizeof G), "compaction: global");
    for (int p = 0; p < ST_PATTERNS; p++) if (have[p]) CHECK(store2_append_src(17 + p, steps_record_len(p), rd, &PID[p]), "compaction: pattern");
    CHECK(reload_all_match(), "after compaction everything loads and matches");
    uint32_t len = 0;
    int old = 0;
    for (int k = 1; k <= 16; k++) if (store2_ptr(k, &len)) old++;
    CHECK(!old, "older record kinds are gone after compaction");
    printf("after compaction: %u of %u bytes of the save area used\n", S2_AREA_SIZE - store2_free_bytes(), S2_AREA_SIZE);

    printf("%d checks, %s\n", checks, fails ? "FAILED" : "ALL PASSED");
    return fails != 0;
}
