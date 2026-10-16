/* Host test for steps.c (sparse step storage), on the Pi:
 *   gcc -O2 -Wall -fsanitize=bounds -fsanitize-undefined-trap-on-error -o steps_test steps_test.c steps.c && ./steps_test
 * Every operation is mirrored on a plain dense model (a full step_t per step), and the two must
 * agree after each one: random writes/removals, byte-only edits, pattern copies and clears, save-
 * record round trips, a full pool, malformed records, v0.10 (v1) records, and records written
 * with other entry sizes (older = shorter, future = longer). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "steps.h"

static step_t ref[ST_PATTERNS][ST_TRACKS][ST_MAXSTEPS];
static uint8_t refl[ST_PATTERNS][ST_TRACKS];
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static int mel(int t) { return t >= ST_MEL_FIRST; }
static step_t norm(int t, step_t v)                 /* what the store keeps of a write */
{
    if (!(v.b & 3)) { memset(&v, 0, sizeof v); return v; }
    if (!mel(t)) { v.note = 0; v.ch2 = v.ch3 = 0; }
    return v;
}
static uint32_t ref_used(void)
{
    uint32_t u = 0;
    for (int p = 0; p < ST_PATTERNS; p++) for (int t = 0; t < ST_TRACKS; t++) for (int s = 0; s < ST_MAXSTEPS; s++)
        if (ref[p][t][s].b & 3) u += mel(t) ? ST_MEL_SIZE : ST_DRUM_SIZE;
    return u;
}
static int agree(void)
{
    for (int p = 0; p < ST_PATTERNS; p++) for (int t = 0; t < ST_TRACKS; t++) {
        if (steps_len[p][t] != refl[p][t]) return 0;
        for (int s = 0; s < ST_MAXSTEPS; s++) {
            step_t v; steps_read(p, t, s, &v);
            if (memcmp(&v, &ref[p][t][s], sizeof v)) return 0;
            if (steps_get(p, t, s) != ref[p][t][s].b) return 0;
        }
    }
    return steps_used() == ref_used();
}
static step_t rnd_step(void)
{
    step_t v = {(uint8_t)rand(), (uint8_t)(rand() % 128), (uint8_t)(rand() % 16), (uint8_t)(rand() % 129), (int8_t)(rand() % 49 - 24), (int8_t)(rand() % 49 - 24)};
    return v;
}
static void ref_copy(int src, int dst) { memcpy(ref[dst], ref[src], sizeof ref[dst]); memcpy(refl[dst], refl[src], sizeof refl[dst]); }

static uint8_t rec[ST_REC_HDR + ST_POOL + 64];
static uint32_t read_record(int p)
{
    uint32_t len = steps_record_len(p);
    for (uint32_t o = 0; o < len; o += 7) steps_record_read(p, o, rec + o, len - o < 7 ? len - o : 7);   /* odd chunks */
    return len;
}

/* build a record from the dense model with given entry sizes (v1 layout if v1) */
static uint8_t alt[ST_REC_HDR + 8 * ST_TRACKS * ST_MAXSTEPS];
static uint32_t build_record(int p, int v1, uint32_t dsz, uint32_t msz)
{
    uint32_t o = 0;
    if (!v1) { alt[0] = 2; alt[1] = (uint8_t)dsz; alt[2] = (uint8_t)msz; alt[3] = 0; o = 4; }
    for (int t = 0; t < ST_TRACKS; t++) alt[o++] = refl[p][t];
    for (int t = 0; t < ST_TRACKS; t++)
        for (int w = 0; w < 4; w++) {
            uint32_t m = 0;
            for (int b = 0; b < 32; b++) if (ref[p][t][w * 32 + b].b & 3) m |= 1u << b;
            for (int k = 0; k < 4; k++) alt[o++] = (uint8_t)(m >> (8 * k));
        }
    for (int t = 0; t < ST_TRACKS; t++)
        for (int s = 0; s < ST_MAXSTEPS; s++) {
            step_t v = ref[p][t][s];
            if (!(v.b & 3)) continue;
            uint8_t full[8] = {v.b, 0, 0, 0, 0, 0, 0xAB, 0xCD};   /* bytes past today's fields = a future field */
            if (mel(t)) { full[1] = v.note; full[2] = v.cond; full[3] = v.lock; full[4] = (uint8_t)v.ch2; full[5] = (uint8_t)v.ch3; }
            else { full[1] = v.cond; full[2] = v.lock; full[3] = 0xEE; }
            uint32_t sz = mel(t) ? msz : dsz;
            for (uint32_t k = 0; k < sz; k++) alt[o++] = full[k];
        }
    return o;
}
/* the dense model as it should load from entries cut to dsz / msz bytes */
static void ref_truncate(int p, uint32_t dsz, uint32_t msz)
{
    for (int t = 0; t < ST_TRACKS; t++) for (int s = 0; s < ST_MAXSTEPS; s++) {
        step_t *v = &ref[p][t][s];
        if (!(v->b & 3)) continue;
        if (mel(t)) { if (msz < 2) v->note = 0; if (msz < 3) v->cond = 0; if (msz < 4) v->lock = 0; if (msz < 5) v->ch2 = 0; if (msz < 6) v->ch3 = 0; }
        else { if (dsz < 2) v->cond = 0; if (dsz < 3) v->lock = 0; }
    }
}

int main(void)
{
    srand(12345);
    steps_clear_all();
    CHECK(agree() && steps_used() == 0, "empty");
    step_t one = {2, 60, 0, 0, 0, 0};
    CHECK(steps_get(0, 0, 200) == 0 && !steps_write(0, 0, 200, &one) && !steps_write(ST_PATTERNS, 0, 0, &one), "out of range is refused");

    int ok = 1;
    for (int i = 0; i < 40000; i++) {
        int p = rand() % ST_PATTERNS, t = rand() % ST_TRACKS, s = rand() % ST_MAXSTEPS, op = rand() % 100;
        step_t v = rnd_step();
        if (op < 2) v.b &= ~3;
        if (op < 50) { if (steps_write(p, t, s, &v)) ref[p][t][s] = norm(t, v); }
        else if (op < 60) {                         /* steps_set: keeps the other fields of a lit step */
            if (steps_set(p, t, s, v.b, v.note)) {
                step_t r = ref[p][t][s]; if (!(r.b & 3)) memset(&r, 0, sizeof r);
                r.b = v.b; r.note = v.note; ref[p][t][s] = norm(t, r);
            }
        } else if (op < 66) {
            if (steps_set_byte(p, t, s, v.b)) { step_t r = ref[p][t][s]; if (!(r.b & 3)) memset(&r, 0, sizeof r); r.b = v.b; ref[p][t][s] = norm(t, r); }
        } else if (op < 90) { step_t z = {0}; if (steps_write(p, t, s, &z)) ref[p][t][s] = norm(t, z); }
        else if (op < 93) { uint8_t l = (uint8_t)(rand() % 129); steps_len[p][t] = l; refl[p][t] = l; }
        else if (op < 95) { int q = rand() % ST_PATTERNS; if (steps_copy_pattern(p, q) && p != q) ref_copy(p, q); }
        else if (op < 96) { steps_clear_pattern(p); memset(ref[p], 0, sizeof ref[p]); }
        else {
            int q = rand() % ST_PATTERNS;
            uint32_t len = read_record(p);
            if (steps_record_load(q, rec, len) && p != q) ref_copy(p, q);
        }
        if ((i % 97 == 0 || op >= 90) && !agree()) { ok = 0; printf("  diverged at op %d (kind %d)\n", i, op); break; }
    }
    CHECK(ok && agree(), "40000 random writes / copies / clears / record round trips agree with the dense model");
    CHECK(steps_note(0, 3, 0) == 0, "drum tracks have no note");

    int te = 1;
    for (int t = 0; t < ST_TRACKS; t++) {
        int e = 1;
        for (int p = 0; p < ST_PATTERNS; p++) for (int s = 0; s < ST_MAXSTEPS; s++) if (ref[p][t][s].b & 3) e = 0;
        if (e != steps_track_empty(t)) te = 0;
    }
    CHECK(te, "track_empty matches");

    /* other entry sizes: v1 (1 / 2 bytes), short v2, and future longer v2 */
    {
        int fine = 1;
        const uint32_t sizes[][3] = {{1, 1, 2}, {0, 1, 2}, {0, 2, 4}, {0, 3, 6}, {0, 4, 8}};   /* {v1?, dsz, msz} */
        for (int k = 0; k < 5; k++) {
            int p = 2, q = 5;
            uint32_t n = build_record(p, (int)sizes[k][0], sizes[k][1], sizes[k][2]);
            bool r = sizes[k][0] ? steps_record_load_v1(q, alt, n) : steps_record_load(q, alt, n);
            if (!r) { printf("  sizes case %d refused\n", k); fine = 0; continue; }
            ref_copy(p, q); ref_truncate(q, sizes[k][1], sizes[k][2]);
            if (!agree()) { printf("  sizes case %d disagrees\n", k); fine = 0; }
        }
        CHECK(fine, "records with older/shorter and future/longer entries load as a prefix, the rest zero");
        uint32_t n = build_record(1, 0, 3, 6);
        alt[0] = 1;
        CHECK(!steps_record_load(4, alt, n), "a record with an unknown format byte is refused");
        CHECK(agree(), "and changes nothing");
    }

    /* fill the pool: the refused write changes nothing */
    int full = 0, unchanged = 1;
    for (int i = 0; i < 400000 && !full; i++) {
        int p = rand() % ST_PATTERNS, t = rand() % ST_TRACKS, s = rand() % ST_MAXSTEPS;
        step_t v = rnd_step(); v.b |= 1;
        if (steps_write(p, t, s, &v)) ref[p][t][s] = norm(t, v);
        else { full = 1; if (!agree()) unchanged = 0; }
    }
    CHECK(full && steps_used() <= ST_POOL && steps_used() > ST_POOL - ST_MEL_SIZE, "pool fills up to its size");
    CHECK(unchanged, "a refused write leaves everything as it was");
    {
        int src = 0, dst = 1;
        uint32_t ssz = steps_record_len(src), dsz = steps_record_len(dst);
        bool r = steps_copy_pattern(src, dst);
        CHECK(ssz <= dsz ? r : !r, "copy into a full pool: allowed only if it fits");
        if (r) ref_copy(src, dst);
        CHECK(agree(), "after the copy attempt");
    }

    /* malformed records are refused without touching anything */
    {
        uint32_t len = read_record(3);
        CHECK(!steps_record_load(2, rec, len - 1), "truncated record refused");
        CHECK(!steps_record_load(2, rec, ST_REC_HDR - 1), "too-short record refused");
        if (len > ST_REC_HDR) {
            uint8_t keep = rec[ST_REC_HDR];
            rec[ST_REC_HDR] &= ~3;
            CHECK(!steps_record_load(2, rec, len), "record with an 'off' step byte refused");
            rec[ST_REC_HDR] = keep;
        }
        CHECK(agree(), "refused records changed nothing");
        uint8_t past[4];
        steps_record_read(3, len, past, 4);
        CHECK(past[0] == 0xFF && past[3] == 0xFF, "reading past a record's end gives 0xFF");
    }

    /* v0.12: whatever was last SAVED always fits back into the pool together (a pattern counts as
       max(live, saved)). Random edits + random saves, and at checkpoints: reboot from the saved set. */
    {
        static uint8_t saved_rec[ST_PATTERNS][ST_REC_HDR + ST_POOL];
        static uint32_t saved_len[ST_PATTERNS];
        static uint8_t live_rec[ST_PATTERNS][ST_REC_HDR + ST_POOL];
        steps_clear_all(); memset(steps_saved, 0, sizeof steps_saved);
        memset(ref, 0, sizeof ref); memset(refl, 0, sizeof refl);
        for (int p = 0; p < ST_PATTERNS; p++) { saved_len[p] = read_record(p); memcpy(saved_rec[p], rec, saved_len[p]); }
        int boots_ok = 1, refused = 0;
        for (int i = 0; i < 60000 && boots_ok; i++) {
            int p = rand() % ST_PATTERNS, op = rand() % 1000;
            if (op < 950) {                         /* add steps (mostly), so the pool runs full */
                int t = rand() % ST_TRACKS, s = rand() % ST_MAXSTEPS;
                step_t v = rnd_step(); v.b |= 1;
                if (!steps_write(p, t, s, &v)) refused++;
            } else if (op < 952) {                  /* a pattern is cleared but NOT saved */
                steps_clear_pattern(p);
            } else if (op < 990) {                  /* save pattern p */
                saved_len[p] = read_record(p); memcpy(saved_rec[p], rec, saved_len[p]);
                steps_saved[p] = (uint16_t)(saved_len[p] - ST_REC_HDR);
            } else {                                /* reboot from the saved set, then put the live state back */
                for (int q = 0; q < ST_PATTERNS; q++) { uint32_t l = read_record(q); memcpy(live_rec[q], rec, l); }
                uint8_t lens_back[ST_PATTERNS][ST_TRACKS]; memcpy(lens_back, steps_len, sizeof lens_back);
                steps_clear_all();
                for (int q = 0; q < ST_PATTERNS; q++) if (!steps_record_load(q, saved_rec[q], saved_len[q])) boots_ok = 0;
                steps_clear_all();
                for (int q = 0; q < ST_PATTERNS; q++) {
                    uint32_t l = ST_REC_HDR; /* recompute the live record length from its maps */
                    for (int t = 0; t < ST_TRACKS; t++) for (int w = 0; w < 4; w++) {
                        const uint8_t *m = live_rec[q] + 4 + ST_TRACKS + t * 16 + w * 4;
                        uint32_t x = (uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 | (uint32_t)m[3] << 24;
                        int c = 0; while (x) { c += x & 1; x >>= 1; }
                        l += (uint32_t)c * (mel(t) ? ST_MEL_SIZE : ST_DRUM_SIZE);
                    }
                    if (!steps_record_load(q, live_rec[q], l)) boots_ok = 0;
                }
                memcpy(steps_len, lens_back, sizeof lens_back);
            }
        }
        CHECK(refused > 0, "the pool ran full during the run (the case that matters)");
        CHECK(boots_ok, "after any mix of edits, unsaved clears and saves, the saved set always loads back");
        memset(steps_saved, 0, sizeof steps_saved);
        steps_clear_all();
        memset(ref, 0, sizeof ref); memset(refl, 0, sizeof refl);
    }

    /* clear everything, then rebuild a pattern from a saved record into an empty store */
    {
        uint32_t len = read_record(5);
        static uint8_t keep[ST_REC_HDR + ST_POOL]; memcpy(keep, rec, len);
        steps_clear_all();
        memset(ref, 0, sizeof ref); memset(refl, 0, sizeof refl);
        CHECK(steps_record_load(0, keep, len), "record loads into an empty store");
        uint32_t len2 = read_record(0);
        CHECK(len2 == len && !memcmp(rec, keep, len), "and saves back byte-identical");
    }

    printf("%d checks, %s\n", checks, fails ? "FAILED" : "ALL PASSED");
    return fails != 0;
}
