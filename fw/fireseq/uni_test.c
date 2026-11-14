/* UNIVERSAL mode logic (uni.c): the page format, pads, knobs, the on-Fire edit, patches, host write checks.
 *   gcc -O1 -Wall -Wextra -I. uni_test.c uni.c -o uni_test && ./uni_test */
#include <stdio.h>
#include <string.h>
#include "uni.h"

static int fails, checks;
#define EXPECTED 61
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static uint32_t page_new(uint8_t *r, const char *name)
{
    r[0] = UNI_VER;
    for (int i = 0; i < UNI_NAME; i++) r[1 + i] = (uint8_t)(name[i] ? name[i] : ' ');
    return UNI_HDR;
}
static uint32_t add_pad(uint8_t *r, uint32_t len, int pad, int layer, uni_pad_t p) { uni_pad_enc(r + len, pad, layer, p); return len + UNI_ENT; }
static uint32_t add_knob(uint8_t *r, uint32_t len, int slot, uni_knob_t k) { uni_knob_enc(r + len, slot, k); return len + UNI_ENT; }

int main(void)
{
    uint8_t r[UNI_MAX_LEN + 16];
    uint32_t len = page_new(r, "drums\0\0\0");

    /* format */
    CHECK(uni_len_ok(9) && uni_len_ok(15) && uni_len_ok(UNI_MAX_LEN) && !uni_len_ok(8) && !uni_len_ok(14) && !uni_len_ok(UNI_MAX_LEN + 6),
          "length = 9 + 6 x entries, at most 144 entries (873 bytes)");
    CHECK(UNI_MAX_LEN == 873 && UNI_KEYS == 144, "64 pads x 2 layers + 16 knob slots");
    CHECK(uni_check(r, len) == UR_OK, "an empty page (version + name) is valid");
    uni_pad_t note = {UT_NOTE, 9, 36, 100, 0, 3}, hold = {UT_HOLD, 0, 64, 127, 0, 16}, tog = {UT_TOGGLE, 1, 80, 90, 10, 5};
    uni_knob_t kcc = {UK_CC, 2, 74, 64}, kpb = {UK_PB, 0, 0, 64};
    len = add_pad(r, len, 0, UL_BASE, note);
    len = add_pad(r, len, 0, UL_SHIFT, hold);
    len = add_pad(r, len, 63, UL_BASE, tog);
    len = add_knob(r, len, 5, kcc);
    len = add_knob(r, len, 15, kpb);
    CHECK(uni_check(r, len) == UR_OK && len == 9 + 5 * 6, "pads on both layers + knobs: valid");
    {
        uint8_t b[UNI_MAX_LEN]; memcpy(b, r, len);
        b[0] = 2; int v1 = uni_check(b, len) == UR_INVALID; b[0] = UNI_VER;
        b[3] = 0x7F; int v2 = uni_check(b, len) == UR_INVALID; b[3] = 'u';
        b[9 + 1] = 17 << 1; int v3 = uni_check(b, len) == UR_INVALID; b[9 + 1] = r[9 + 1];
        b[9 + 2] = 0x05; int v4 = uni_check(b, len) == UR_INVALID; b[9 + 2] = r[9 + 2];     /* type 0 in an entry */
        b[9 + 2] = 0x75; int v5 = uni_check(b, len) == UR_INVALID; b[9 + 2] = r[9 + 2];     /* type 7 */
        CHECK(v1 && v2 && v3 && v4 && v5, "refused: another version, a name byte outside ASCII, colour 17, pad type 0 or 7");
        b[9 + 3] = 128; int w1 = uni_check(b, len) == UR_INVALID; b[9 + 3] = r[9 + 3];
        b[27] = 80; int w2 = uni_check(b, len) == UR_INVALID; b[27] = r[27];                /* entry 3: target past knob 15 */
        b[27 + 1] = 0; int w3 = uni_check(b, len) == UR_INVALID; b[27 + 1] = r[27 + 1];     /* knob type none in an entry */
        b[27 + 2] = 16; int w4 = uni_check(b, len) == UR_INVALID; b[27 + 2] = r[27 + 2];
        b[27 + 5] = 1; int w5 = uni_check(b, len) == UR_INVALID; b[27 + 5] = 0;
        CHECK(w1 && w2 && w3 && w4 && w5, "refused: a byte > 127, target 80+, knob type 0, knob channel 16, knob spare byte set");
        memcpy(b + len, b + 9, 6); int d1 = uni_check(b, len + 6) == UR_INVALID;          /* pad 0 base twice */
        memcpy(b + len, b + 27, 6); int d2 = uni_check(b, len + 6) == UR_INVALID;         /* knob 5 twice */
        CHECK(d1 && d2 && uni_check(b, len + 1) == UR_INVALID && uni_check(0, 9) == UR_INVALID, "refused: duplicates, a bad length, no data");
    }
    {
        uint32_t s = 0; for (uint32_t i = 0; i < len; i++) s += r[i];
        uint8_t big[UNI_MAX_LEN]; memset(big, 127, sizeof big);
        CHECK(uni_sum(r, len) == (s & 0x3FFF) && uni_sum(big, UNI_MAX_LEN) == ((127u * UNI_MAX_LEN) & 0x3FFF) && uni_sum(big, UNI_MAX_LEN) < 16384,
              "checksum = byte sum, 14 bits (a full page's sum wraps)");
    }
    uint8_t ix[UNI_KEYS];
    uni_ops = 0;
    uni_index(r, len, ix);
    CHECK(ix[uni_pad_key(0, UL_BASE)] == 0 && ix[uni_pad_key(0, UL_SHIFT)] == 1 && ix[uni_pad_key(63, UL_BASE)] == 2 && ix[uni_pad_key(1, UL_BASE)] == 255
          && ix[uni_knob_key(5)] == 3 && ix[uni_knob_key(15)] == 4 && ix[uni_knob_key(0)] == 255 && ix[uni_pad_key(63, UL_SHIFT)] == 255,
          "index: every key (base, SHIFT, knobs) in one pass");
    CHECK(uni_ops == (len - UNI_HDR) / UNI_ENT, "the index looks at each entry once (so a SHIFT lookup in the paint is O(1))");
    CHECK(uni_find(r, len, uni_pad_key(0, UL_SHIFT)) == 1 && uni_find(r, len, uni_pad_key(1, UL_SHIFT)) == -1 && uni_find(r, len, uni_knob_key(15)) == 4,
          "find by key");
    {
        uni_pad_t p = uni_pad_of(r + 9), q = uni_pad_of(r + 15);
        uni_knob_t k = uni_knob_of(r + 27);
        CHECK(!memcmp(&p, &note, sizeof p) && !memcmp(&q, &hold, sizeof q) && !memcmp(&k, &kcc, sizeof k), "encode / decode round trip");
        CHECK((r[9 + 1] & 1) == 0 && (r[15 + 1] & 1) == 1 && r[27] == 64 + 5, "layer bit and knob slot in the entry");
    }

    /* pads */
    {
        uint8_t m[3], t = 0; int n;
        int rel = uni_press(note, &t, m, &n);
        CHECK(rel == UREL_NOTE && n == 3 && m[0] == 0x99 && m[1] == 36 && m[2] == 100, "note: note on at its velocity, note off on release");
        uni_pad_t z = note; z.on = 0;
        uni_press(z, &t, m, &n);
        CHECK(m[2] == 1, "a note with velocity 0 plays at 1 (0 would be a note off)");
        uni_pad_t l = {UT_LATCH, 0, 60, 90, 0, 0};
        int a;
        rel = uni_press(l, &t, m, &n);
        CHECK(n == 0 && rel == UREL_NONE, "uni_press leaves a note latch alone (it goes through the latch list)");
        rel = uni_press(hold, &t, m, &n);
        CHECK(rel == UREL_CC && m[0] == 0xB0 && m[1] == 64 && m[2] == 127, "CC hold: on value now, off value on release");
        t = 0; uni_press(tog, &t, m, &n); a = t == 1 && m[2] == 90;
        uni_press(tog, &t, m, &n);
        CHECK(a && t == 0 && m[2] == 10 && m[0] == 0xB1, "CC toggle: on value, then off value");
        uni_pad_t pr = {UT_PRESS, 3, 20, 55, 9, 0};
        rel = uni_press(pr, &t, m, &n);
        CHECK(rel == UREL_NONE && m[0] == 0xB3 && m[2] == 55, "CC press: on value on press, nothing on release (the old /control CC)");
        uni_pad_t pc = {UT_PC, 15, 12, 0, 0, 0};
        rel = uni_press(pc, &t, m, &n);
        CHECK(n == 2 && m[0] == 0xCF && m[1] == 12 && rel == UREL_NONE, "program change: 2 bytes");
        uni_pad_t off = uni_pad_blank();
        CHECK(uni_press(off, &t, m, &n) == UREL_NONE && n == 0, "an off pad sends nothing");
    }

    /* knobs */
    {
        uint8_t m[3], v = 126;
        int n = uni_knob_turn(kcc, &v, 5, m);
        CHECK(n == 3 && v == 127 && m[0] == 0xB2 && m[1] == 74 && m[2] == 127, "CC knob: absolute value, clamped");
        CHECK(uni_knob_turn(kcc, &v, 1, m) == 0 && v == 127, "at the end: nothing sent");
        uni_knob_t none = uni_knob_blank();
        v = 3; CHECK(uni_knob_turn(none, &v, 1, m) == 0 && v == 3, "an unassigned knob does nothing");
        v = 63; uni_knob_turn(kpb, &v, 1, m); int c = m[0] == 0xE0 && m[1] == 0 && m[2] == 64;
        v = 126; uni_knob_turn(kpb, &v, 1, m); int hi = m[1] == 127 && m[2] == 127;
        v = 1; uni_knob_turn(kpb, &v, -1, m);
        CHECK(c && hi && m[1] == 0 && m[2] == 0, "pitch bend: 64 = centre 8192, 127 = 16383, 0 = 0");
    }
    /* modes 0-2 = STEP / NOTE / DRUM, 3 FADERS, 4 UNIVERSAL, 5 the DAW page (v0.38: UNIVERSAL is NOTE's 2nd) */
    CHECK(uni_perform_next(1, 1, 3, 5, 1) == 3 && uni_perform_next(3, 1, 3, 5, 1) == 5 && uni_perform_next(5, 1, 3, 5, 1) == 1,
          "PERFORM with a DAW page: -> FADERS -> DAW page -> back");
    CHECK(uni_perform_next(0, 2, 3, 5, 0) == 3 && uni_perform_next(3, 2, 3, 5, 0) == 2,
          "PERFORM with no DAW page: -> FADERS -> back (no 3rd tap)");
    CHECK(uni_perform_next(5, 0, 3, 5, 0) == 0, "on the DAW page when it is gone: PERFORM still goes back");
    CHECK(uni_perform_next(4, 1, 3, 5, 1) == 3, "PERFORM from UNIVERSAL: FADERS (UNIVERSAL is no longer in its cycle)");
    CHECK(uni_note_next(1, 1, 4) == 4 && uni_note_next(4, 1, 4) == 1 && uni_note_next(0, 1, 4) == 1 && uni_note_next(3, 1, 4) == 1
          && uni_note_next(5, 1, 4) == 1, "NOTE: the keyboard -> UNIVERSAL -> back; from anywhere else -> the keyboard");

    /* on-Fire edit */
    {
        uni_pad_t p = uni_pad_blank();
        uni_edit_pad(&p, 0, 0, 1, UL_BASE); int a = p.type == UT_NOTE;
        uni_edit_pad(&p, 0, 0, 50, UL_BASE); int b = p.type == UT_PC;
        uni_edit_pad(&p, 0, 1, -5, UL_BASE); uni_edit_pad(&p, 0, 2, 200, UL_BASE); uni_edit_pad(&p, 0, 3, -300, UL_BASE);
        CHECK(a && b && p.ch == 0 && p.num == 127 && p.on == 0, "pad edit page 1: type, channel, number, value (clamped)");
        uni_edit_pad(&p, 1, 0, 7, UL_BASE); uni_edit_pad(&p, 1, 1, 30, UL_BASE);
        int c = p.off == 7 && p.col == UNI_COL_GREY;
        uni_edit_pad(&p, 1, 1, -3, UL_SHIFT);
        CHECK(c && p.col == UNI_COL_GREY, "page 2: off value, colour (base only; the SHIFT layer has none)");
        uni_knob_t k = uni_knob_blank();
        uni_edit_knob(&k, 0, 1); uni_edit_knob(&k, 1, 20); uni_edit_knob(&k, 2, 73); uni_edit_knob(&k, 3, -100);
        CHECK(k.type == UK_CC && k.ch == 15 && k.cc == 74 && k.init == 0, "knob edit: type, channel, CC, start value");
    }
    {
        int ok = 1; char L[4][20], row[20];
        for (int t = 0; t < UT_N; t++)
            for (int pg = 0; pg < 2; pg++)
                for (int ly = 0; ly < 2; ly++)
                    for (int col = 0; col < UNI_COLS; col++) {
                        uni_pad_t p = {(uint8_t)t, 15, 127, 127, 127, (uint8_t)col};
                        uni_pad_lines(p, pg, ly, L);
                        for (int i = 0; i < 4; i++) if (strlen(L[i]) > 19) { ok = 0; printf("  long: '%s'\n", L[i]); }
                        p.num = 1; p.ch = 0; uni_pad_lines(p, pg, ly, L);
                        for (int i = 0; i < 4; i++) if (strlen(L[i]) > 19) ok = 0;
                    }
        for (int t = 0; t < UK_N; t++) {
            uni_knob_t k = {(uint8_t)t, 15, 127, 127};
            uni_knob_lines(k, L);
            for (int i = 0; i < 4; i++) if (strlen(L[i]) > 19) ok = 0;
            uni_knob_row(k, 15, row); if (strlen(row) > 13) ok = 0;
        }
        CHECK(ok, "every edit line fits 19 characters, a knob row 13 (the bar takes the rest)");
        uni_pad_t p = {UT_NOTE, 0, 61, 100, 0, 0};
        uni_pad_lines(p, 0, UL_BASE, L);
        CHECK(!strcmp(L[0], "1 type note") && !strcmp(L[2], "3 note C#4 (61)") && !strcmp(L[3], "4 velocity 100"), "note lines name the note (C4 = 60)");
        uni_knob_row(kcc, 5, row); char r2[20]; uni_knob_row(uni_knob_blank(), 0, r2);
        CHECK(!strcmp(row, "2B CC74 ch3") && !strcmp(r2, "1A --"), "knob rows: bank + knob, CC, channel");
    }

    /* latched notes: the note-off is exactly what was sent, whatever the pad is now (review 2026-10-27) */
    {
        uni_latch_t L[UNI_LATCH_MAX]; memset(L, 0, sizeof L);
        uint8_t m[3], out[3 * UNI_LATCH_MAX];
        uni_pad_t l = {UT_LATCH, 3, 60, 90, 0, 0};
        int k0 = uni_pad_key(5, UL_BASE), k1 = uni_pad_key(5, UL_SHIFT);
        CHECK(uni_latch_press(L, k0, l, m) == 3 && m[0] == 0x93 && m[1] == 60 && m[2] == 90 && uni_latch_on(L, k0) && !uni_latch_on(L, k1),
              "a latch press: note on, remembered");
        uni_pad_t l2 = l; l2.ch = 7; l2.num = 64;            /* the pad edited while latched */
        CHECK(uni_latch_press(L, k0, l2, m) == 3 && m[0] == 0x83 && m[1] == 60 && !uni_latch_on(L, k0),
              "pressed again after an edit: the note it SENT goes off (ch 4 note 60), not the new one");
        uni_latch_press(L, k0, l, m); uni_latch_press(L, k1, l2, m); uni_latch_press(L, uni_pad_key(9, UL_BASE), l, m);
        int n = uni_latch_release(L, 5, out, sizeof out);
        CHECK(n == 6 && out[0] == 0x83 && out[1] == 60 && out[3] == 0x87 && out[4] == 64 && !uni_latch_on(L, k0) && !uni_latch_on(L, k1)
              && uni_latch_on(L, uni_pad_key(9, UL_BASE)), "releasing a pad (an edit / a clear / a copy onto it): both its layers' notes off, others stay");
        n = uni_latch_release(L, -1, out, sizeof out);
        CHECK(n == 3 && out[0] == 0x83 && !uni_latch_on(L, uni_pad_key(9, UL_BASE)) && uni_latch_release(L, -1, out, sizeof out) == 0,
              "releasing all (a page change, leaving the mode, a host write): every note off once");
        int full = 1;
        for (int i = 0; i < UNI_LATCH_MAX; i++) if (uni_latch_press(L, i, l, m) != 3) full = 0;
        CHECK(full && uni_latch_press(L, 100, l, m) == 0 && !uni_latch_on(L, 100), "16 latched: a 17th press sends nothing (the screen says so)");
        uni_latch_t Z[UNI_LATCH_MAX]; memset(Z, 0, sizeof Z);
        CHECK(uni_latch_release(Z, -1, out, sizeof out) == 0 && !uni_latch_on(Z, 0), "an all-zero list is empty (nothing sent before boot init)");
    }

    /* patches */
    {
        uni_patch_t ps[UNI_SLOTS]; memset(ps, 0, sizeof ps);
        uint8_t out[UNI_MAX_LEN], want[UNI_MAX_LEN];
        CHECK(uni_patched_len(r, len, ps, UNI_SLOTS, 0, 0xF) == len && uni_patched_len(0, 0, ps, UNI_SLOTS, 0, 0xF) == UNI_HDR,
              "no slots: the record as stored (nothing stored: the 9-byte empty page)");
        uni_patched_read(r, len, ps, UNI_SLOTS, 0, 0xF, 0, out, len);
        CHECK(!memcmp(out, r, len), "... byte for byte");
        uni_pad_t newnote = {UT_NOTE, 4, 50, 70, 0, 8};
        ps[0].page = 0; ps[0].state = US_PEND; ps[0].key = (uint8_t)uni_pad_key(63, UL_BASE); uni_pad_enc(ps[0].e, 63, UL_BASE, newnote);   /* replace */
        ps[1].page = 0; ps[1].state = US_PEND; ps[1].key = (uint8_t)uni_pad_key(0, UL_SHIFT); ps[1].del = 1;                               /* delete */
        ps[2].page = 0; ps[2].state = US_EDIT; ps[2].key = (uint8_t)uni_pad_key(7, UL_BASE); uni_pad_enc(ps[2].e, 7, UL_BASE, newnote);     /* new */
        ps[3].page = 1; ps[3].state = US_PEND; ps[3].key = (uint8_t)uni_pad_key(9, UL_BASE); uni_pad_enc(ps[3].e, 9, UL_BASE, newnote);     /* another page */
        uint32_t wl = page_new(want, "drums\0\0\0");
        wl = add_pad(want, wl, 0, UL_BASE, note); wl = add_pad(want, wl, 63, UL_BASE, newnote);
        wl = add_knob(want, wl, 5, kcc); wl = add_knob(want, wl, 15, kpb);
        uint32_t pl = uni_patched_len(r, len, ps, UNI_SLOTS, 0, 1u << US_PEND);
        uni_patched_read(r, len, ps, UNI_SLOTS, 0, 1u << US_PEND, 0, out, pl);
        CHECK(pl == wl && !memcmp(out, want, wl), "replace in place + delete; an EDIT slot and another page's slot are left out");
        wl = add_pad(want, wl, 7, UL_BASE, newnote);
        pl = uni_patched_len(r, len, ps, UNI_SLOTS, 0, (1u << US_PEND) | (1u << US_EDIT));
        uni_patched_read(r, len, ps, UNI_SLOTS, 0, (1u << US_PEND) | (1u << US_EDIT), 0, out, pl);
        CHECK(pl == wl && !memcmp(out, want, wl) && uni_check(out, pl) == UR_OK, "a new key goes after the stored entries; the result is a valid page");
        uint8_t piece[UNI_MAX_LEN]; uint32_t o = 0; int step = 1;
        while (o < pl) { uint32_t k = (uint32_t)step; if (o + k > pl) k = pl - o; uni_patched_read(r, len, ps, UNI_SLOTS, 0, 0xF, o, piece + o, k); o += k; step = step % 7 + 1; }
        CHECK(!memcmp(piece, want, pl), "read in pieces of any size (the saver reads 1-4 bytes at a time) = read whole");
        CHECK(uni_patched_sum(r, len, ps, UNI_SLOTS, 0, 0xF) == uni_sum(want, pl) && uni_patched_sum(r, len, ps, UNI_SLOTS, 5, 0xF) == uni_sum(r, len),
              "patched checksum = the checksum of the patched page (no slots: the stored one)");
        pl = uni_patched_len(0, 0, ps, UNI_SLOTS, 1, 0xF);
        uni_patched_read(0, 0, ps, UNI_SLOTS, 1, 0xF, 0, out, pl);
        CHECK(pl == 15 && out[0] == UNI_VER && out[1] == ' ' && out[9] == 9 && uni_check(out, pl) == UR_OK, "no stored page: an empty header + the slots");
        memset(ps, 0, sizeof ps);
        ps[5].page = 2; ps[5].state = US_SAVE; ps[5].key = (uint8_t)uni_knob_key(5); uni_knob_enc(ps[5].e, 5, kpb);
        pl = uni_patched_len(r, len, ps, UNI_SLOTS, 2, 1u << US_SAVE);
        uni_patched_read(r, len, ps, UNI_SLOTS, 2, 1u << US_SAVE, 0, out, pl);
        CHECK(pl == len && out[27 + 1] == UK_PB, "a knob slot replaces the stored knob");
    }
    /* the WORK the saver's reader does: a full page, 8 slots, read a word at a time = O(1) per word (review 2026-10-27:
       it re-walked the page every word) */
    {
        static uint8_t big[UNI_MAX_LEN], want[UNI_MAX_LEN], got[UNI_MAX_LEN];
        uint32_t bl = page_new(big, "full");
        for (int pad = 0; pad < 64; pad++) { uni_pad_t q = {UT_NOTE, 1, (uint8_t)pad, 100, 0, 1}; bl = add_pad(big, bl, pad, UL_BASE, q); bl = add_pad(big, bl, pad, UL_SHIFT, q); }
        for (int k = 0; k < 16; k++) bl = add_knob(big, bl, k, kcc);
        uni_patch_t ps[UNI_SLOTS]; memset(ps, 0, sizeof ps);
        for (int i = 0; i < UNI_SLOTS; i++) {                  /* 4 replaced, 4 deleted, spread over the page */
            int pad = i * 8;
            ps[i].page = 0; ps[i].state = US_SAVE; ps[i].key = (uint8_t)uni_pad_key(pad, i & 1); ps[i].del = i >= 4;
            uni_pad_t q = {UT_PC, 2, 99, 0, 0, 0}; uni_pad_enc(ps[i].e, pad, i & 1, q);
        }
        uint32_t pl = uni_patched_len(big, bl, ps, UNI_SLOTS, 0, 1u << US_SAVE);
        uni_patched_read(big, bl, ps, UNI_SLOTS, 0, 1u << US_SAVE, 0, want, pl);
        uni_cur_t c;
        uni_ops = 0;
        uni_cur_init(&c, big, bl, ps, UNI_SLOTS, 0, 1u << US_SAVE);
        unsigned long init_ops = uni_ops, worst = 0;
        for (uint32_t o = 0; o < pl; o += 4) {
            uni_ops = 0;
            uni_cur_read(&c, o, got + o, pl - o < 4 ? pl - o : 4);
            if (uni_ops > worst) worst = uni_ops;
        }
        CHECK(pl == bl - 4 * UNI_ENT && !memcmp(got, want, pl) && uni_check(got, pl) == UR_OK, "the cursor, word by word = the whole record");
        CHECK(init_ops <= 144 + UNI_SLOTS && worst <= 2 * (1 + UNI_SLOTS) + 2, "cursor work: init <= entries + slots, each word <= 2 entries (+ a slot lookup)");
        uni_ops = 0; (void)uni_patched_len(big, bl, ps, UNI_SLOTS, 0, 1u << US_SAVE); unsigned long lw = uni_ops;
        uni_ops = 0; (void)uni_patched_sum(big, bl, ps, UNI_SLOTS, 0, 1u << US_SAVE); unsigned long sw = uni_ops;
        CHECK(lw <= 144 + UNI_SLOTS * (UNI_SLOTS + 3) && sw <= 3 * (144 + UNI_SLOTS), "length / checksum: one pass each, never one per byte");
        uni_cur_read(&c, 3, got, 1);                             /* behind the cursor: it starts over and is still right */
        uni_cur_read(&c, 200, got + 1, 1);
        CHECK(got[0] == want[3] && got[1] == want[200], "a read behind the cursor starts it over");
        memset(ps, 0, sizeof ps);                                /* two slots with one new key (main.c never makes them): one entry */
        uni_pad_t q = {UT_NOTE, 0, 1, 1, 0, 0};
        for (int i = 0; i < 2; i++) { ps[i].page = 0; ps[i].state = US_PEND; ps[i].key = (uint8_t)uni_pad_key(7, UL_SHIFT); uni_pad_enc(ps[i].e, 7, UL_SHIFT, q); }
        uint8_t small[UNI_HDR + 2 * UNI_ENT];
        uint32_t sl = uni_patched_len(0, 0, ps, UNI_SLOTS, 0, 0xF);
        uni_patched_read(0, 0, ps, UNI_SLOTS, 0, 0xF, 0, small, UNI_HDR + 2 * UNI_ENT);
        CHECK(sl == UNI_HDR + UNI_ENT && small[UNI_HDR + UNI_ENT] == 0 && uni_check(small, sl) == UR_OK
              && uni_patched_sum(0, 0, ps, UNI_SLOTS, 0, 0xF) == uni_sum(small, sl), "a key with two slots counts once in the length, the bytes and the sum");
    }

    /* host write checks */
    CHECK(uni_begin_check(0, 9, 0) == UR_OK && uni_begin_check(8, 9, 0) == UR_BAD && uni_begin_check(0, 10, 0) == UR_BAD
          && uni_begin_check(-1, 9, 0) == UR_BAD, "begin: page 0-7, a valid length");
    CHECK(uni_begin_check(3, 873, UNI_BUDGET - 873) == UR_OK && uni_begin_check(3, 873, UNI_BUDGET - 872) == UR_FULL,
          "begin: all pages together within the budget");
    CHECK(uni_chunk_check(0, 24, 0, 100) == UR_OK && uni_chunk_check(96, 4, 96, 100) == UR_OK && uni_chunk_check(24, 24, 0, 100) == UR_ORDER,
          "chunks: in order, full except the last");
    CHECK(uni_chunk_check(0, 20, 0, 100) == UR_BAD && uni_chunk_check(96, 8, 96, 100) == UR_BAD && uni_chunk_check(0, 0, 0, 100) == UR_BAD
          && uni_chunk_check(0, 25, 0, 100) == UR_BAD, "chunks: a short one mid-page, past the end, empty, too long = refused");
    CHECK(UNI_BUDGET >= 8u * (UNI_HDR + 80u * UNI_ENT), "the budget holds 8 full pages of 64 pads + 16 knobs (+ 30 SHIFT entries)");
    CHECK(UNI_CHUNK % 4 == 0 && 9 + 2 + UNI_CHUNK + 1 <= 40, "a chunk is whole flash words and fits the 40-byte SysEx buffer");

    printf("uni_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
