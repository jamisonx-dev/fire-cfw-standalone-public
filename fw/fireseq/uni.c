/* UNIVERSAL mode logic: see uni.h. Pure: no hardware, no globals that change. */
#include <string.h>
#include "uni.h"
#ifdef UNI_TEST
unsigned long uni_ops;
#endif

const char *const UNI_TYPE_NAME[UT_N] = {"off", "note", "note latch", "CC hold", "CC toggle", "CC press", "prog chg"};
/* the fader palette's names (main.c FAD_COL_NAME, same order) + grey */
const char *const UNI_COL_NAME[UNI_COLS] = {"red", "orange", "amber", "yellow", "lime", "green", "mint", "teal",
    "cyan", "sky", "blue", "indigo", "purple", "magenta", "pink", "white", "grey"};
static const char *const UK_NAME[UK_N] = {"none", "CC", "pitch bend"};

int uni_len_ok(uint32_t len) { return len >= UNI_HDR && len <= UNI_MAX_LEN && (len - UNI_HDR) % UNI_ENT == 0; }

int uni_key(const uint8_t *e) { return e[0] < UNI_PADS ? e[0] * 2 + (e[1] & 1) : 2 * UNI_PADS + (e[0] - UNI_PADS); }
int uni_pad_key(int pad, int layer) { return pad * 2 + (layer & 1); }
int uni_knob_key(int slot) { return 2 * UNI_PADS + slot; }

static int entry_ok(const uint8_t *e)
{
    for (int i = 0; i < UNI_ENT; i++) if (e[i] > 127) return 0;
    if (e[0] < UNI_PADS) {
        int type = e[2] >> 4;
        return (e[1] >> 1) <= UNI_COL_GREY && type >= UT_NOTE && type < UT_N;
    }
    if (e[0] >= UNI_PADS + UNI_KNOBS) return 0;
    return e[1] >= UK_CC && e[1] < UK_N && e[2] <= 15 && e[5] == 0;
}

int uni_check(const uint8_t *r, uint32_t len)
{
    if (!r || !uni_len_ok(len) || r[0] != UNI_VER) return UR_INVALID;
    for (int i = 1; i <= UNI_NAME; i++) if (r[i] < 0x20 || r[i] > 0x7E) return UR_INVALID;
    uint8_t seen[(UNI_KEYS + 7) / 8];
    memset(seen, 0, sizeof seen);
    for (uint32_t o = UNI_HDR; o < len; o += UNI_ENT) {
        if (!entry_ok(r + o)) return UR_INVALID;
        int k = uni_key(r + o);
        if (seen[k >> 3] >> (k & 7) & 1) return UR_INVALID;
        seen[k >> 3] |= (uint8_t)(1u << (k & 7));
    }
    return UR_OK;
}

uint16_t uni_sum(const uint8_t *r, uint32_t len)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < len; i++) s += r[i];
    return (uint16_t)(s & 0x3FFF);
}

int uni_find(const uint8_t *r, uint32_t len, int key)
{
    if (!r || !uni_len_ok(len)) return -1;
    for (uint32_t o = UNI_HDR, i = 0; o < len; o += UNI_ENT, i++) { UNI_OP(); if (uni_key(r + o) == key) return (int)i; }
    return -1;
}

void uni_index(const uint8_t *r, uint32_t len, uint8_t ix[UNI_KEYS])
{
    memset(ix, 255, UNI_KEYS);
    if (!r || !uni_len_ok(len)) return;
    for (uint32_t o = UNI_HDR, i = 0; o < len && i < 255; o += UNI_ENT, i++) {
        UNI_OP();
        if (r[o] < UNI_PADS + UNI_KNOBS) ix[uni_key(r + o)] = (uint8_t)i;
    }
}

uni_pad_t uni_pad_of(const uint8_t *e)
{
    uni_pad_t p = {(uint8_t)(e[2] >> 4), (uint8_t)(e[2] & 15), (uint8_t)(e[3] & 127), (uint8_t)(e[4] & 127),
                   (uint8_t)(e[5] & 127), (uint8_t)(e[1] >> 1)};
    if (p.type >= UT_N) p.type = UT_OFF;
    if (p.col > UNI_COL_GREY) p.col = UNI_COL_GREY;
    return p;
}
uni_knob_t uni_knob_of(const uint8_t *e)
{
    uni_knob_t k = {(uint8_t)(e[1] < UK_N ? e[1] : UK_NONE), (uint8_t)(e[2] & 15), (uint8_t)(e[3] & 127), (uint8_t)(e[4] & 127)};
    return k;
}
void uni_pad_enc(uint8_t e[UNI_ENT], int pad, int layer, uni_pad_t p)
{
    e[0] = (uint8_t)(pad & 63); e[1] = (uint8_t)((layer & 1) | ((p.col > UNI_COL_GREY ? UNI_COL_GREY : p.col) << 1));
    e[2] = (uint8_t)(((p.type % UT_N) << 4) | (p.ch & 15)); e[3] = p.num & 127; e[4] = p.on & 127; e[5] = p.off & 127;
}
void uni_knob_enc(uint8_t e[UNI_ENT], int slot, uni_knob_t k)
{
    e[0] = (uint8_t)(UNI_PADS + (slot & 15)); e[1] = (uint8_t)(k.type % UK_N); e[2] = k.ch & 15; e[3] = k.cc & 127;
    e[4] = k.init & 127; e[5] = 0;
}
uni_pad_t uni_pad_blank(void) { uni_pad_t p = {UT_OFF, 0, 36, 100, 0, 0}; return p; }
uni_knob_t uni_knob_blank(void) { uni_knob_t k = {UK_NONE, 0, 1, 64}; return k; }

int uni_press(uni_pad_t p, uint8_t *tog, uint8_t m[3], int *n)
{
    uint8_t ch = p.ch & 15, vel = p.on ? p.on : 1;   /* (a note-on with velocity 0 would be a note-off) */
    *n = 0;
    switch (p.type) {
    case UT_NOTE: m[0] = (uint8_t)(0x90 | ch); m[1] = p.num; m[2] = vel; *n = 3; return UREL_NOTE;
    case UT_LATCH: return UREL_NONE;               /* (uni_latch_press: it must remember what it sent) */
    case UT_HOLD: m[0] = (uint8_t)(0xB0 | ch); m[1] = p.num; m[2] = p.on; *n = 3; return UREL_CC;
    case UT_TOGGLE:
        *tog = !*tog;
        m[0] = (uint8_t)(0xB0 | ch); m[1] = p.num; m[2] = *tog ? p.on : p.off; *n = 3;
        return UREL_NONE;
    case UT_PRESS: m[0] = (uint8_t)(0xB0 | ch); m[1] = p.num; m[2] = p.on; *n = 3; return UREL_NONE;
    case UT_PC: m[0] = (uint8_t)(0xC0 | ch); m[1] = p.num; *n = 2; return UREL_NONE;
    default: return UREL_NONE;
    }
}

void uni_latch_init(uni_latch_t L[UNI_LATCH_MAX]) { for (int i = 0; i < UNI_LATCH_MAX; i++) L[i].key1 = 0; }
static int latch_at(const uni_latch_t L[UNI_LATCH_MAX], int key1) { for (int i = 0; i < UNI_LATCH_MAX; i++) if (L[i].key1 == key1) return i; return -1; }
int uni_latch_on(const uni_latch_t L[UNI_LATCH_MAX], int key) { return key >= 0 && latch_at(L, key + 1) >= 0; }
int uni_latch_press(uni_latch_t L[UNI_LATCH_MAX], int key, uni_pad_t p, uint8_t m[3])
{
    int i = latch_at(L, key + 1);
    if (i >= 0) {                                 /* latched: exactly the note it sent goes off */
        m[0] = (uint8_t)(0x80 | L[i].ch); m[1] = L[i].note; m[2] = 0;
        L[i].key1 = 0;
        return 3;
    }
    i = latch_at(L, 0);
    if (i < 0) return 0;                          /* the list is full: nothing (the caller says so) */
    L[i].key1 = (uint8_t)(key + 1); L[i].note = p.num & 127; L[i].ch = p.ch & 15;
    m[0] = (uint8_t)(0x90 | L[i].ch); m[1] = L[i].note; m[2] = p.on ? p.on : 1;
    return 3;
}
int uni_latch_release(uni_latch_t L[UNI_LATCH_MAX], int pad, uint8_t *out, int cap)
{
    int n = 0;
    for (int i = 0; i < UNI_LATCH_MAX; i++) {
        if (!L[i].key1 || (pad >= 0 && (L[i].key1 - 1) >> 1 != pad)) continue;
        if (n + 3 > cap) break;
        out[n++] = (uint8_t)(0x80 | L[i].ch); out[n++] = L[i].note; out[n++] = 0;
        L[i].key1 = 0;
    }
    return n;
}

int uni_knob_turn(uni_knob_t k, uint8_t *val, int d, uint8_t m[3])
{
    if (k.type == UK_NONE || !d) return 0;
    int v = *val + d;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (v == *val) return 0;
    *val = (uint8_t)v;
    if (k.type == UK_PB) {                        /* 0..127 -> 14 bits; 64 = 8192 (centre), 127 = 16383 */
        uint32_t b = v <= 64 ? (uint32_t)v * 128u : 8192u + (uint32_t)(v - 64) * 8191u / 63u;
        m[0] = (uint8_t)(0xE0 | (k.ch & 15)); m[1] = (uint8_t)(b & 127); m[2] = (uint8_t)(b >> 7); return 3;
    }
    m[0] = (uint8_t)(0xB0 | (k.ch & 15)); m[1] = k.cc & 127; m[2] = (uint8_t)v; return 3;
}

int uni_perform_next(int mode, int before, int m_fader, int m_daw, int has_daw)
{
    if (mode == m_fader) return has_daw ? m_daw : before;
    if (mode == m_daw) return before;
    return m_fader;
}

int uni_note_next(int mode, int m_note, int m_uni)
{
    return mode == m_note ? m_uni : m_note;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void uni_edit_pad(uni_pad_t *p, int kpage, int knob, int d, int layer)
{
    if (kpage == 0) {
        if (knob == 0) p->type = (uint8_t)clampi(p->type + d, 0, UT_N - 1);
        else if (knob == 1) p->ch = (uint8_t)clampi(p->ch + d, 0, 15);
        else if (knob == 2) p->num = (uint8_t)clampi(p->num + d, 0, 127);
        else p->on = (uint8_t)clampi(p->on + d, 0, 127);
    } else {
        if (knob == 0) p->off = (uint8_t)clampi(p->off + d, 0, 127);
        else if (knob == 1 && layer == UL_BASE) p->col = (uint8_t)clampi(p->col + d, 0, UNI_COL_GREY);
    }
}

void uni_edit_knob(uni_knob_t *k, int knob, int d)
{
    if (knob == 0) k->type = (uint8_t)clampi(k->type + d, 0, UK_N - 1);
    else if (knob == 1) k->ch = (uint8_t)clampi(k->ch + d, 0, 15);
    else if (knob == 2) k->cc = (uint8_t)clampi(k->cc + d, 0, 127);
    else k->init = (uint8_t)clampi(k->init + d, 0, 127);
}

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
static char *put_n(char *o, int v) { char t[4]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *o++ = t[--n]; *o = 0; return o; }
static char *put_note(char *o, int n)
{
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    o = put(o, NM[n % 12]);
    int oct = n / 12 - 1;
    if (oct < 0) { *o++ = '-'; oct = -oct; }
    return put_n(o, oct);
}

void uni_pad_lines(uni_pad_t p, int kpage, int layer, char L[4][20])
{
    char *o;
    for (int i = 0; i < 4; i++) L[i][0] = 0;
    int note = p.type == UT_NOTE || p.type == UT_LATCH;
    if (kpage == 0) {
        o = put(L[0], "1 type "); put(o, UNI_TYPE_NAME[p.type % UT_N]);
        if (p.type == UT_OFF) { put(L[1], "(pad does nothing)"); put(L[3], "KNOB MODE: page 2"); return; }
        o = put(L[1], "2 ch "); put_n(o, p.ch + 1);
        if (note) { o = put(L[2], "3 note "); o = put_note(o, p.num); o = put(o, " ("); o = put_n(o, p.num); put(o, ")"); }
        else { o = put(L[2], p.type == UT_PC ? "3 prog " : "3 CC "); put_n(o, p.num); }
        if (p.type == UT_PC) put(L[3], "KNOB MODE: page 2");
        else { o = put(L[3], note ? "4 velocity " : "4 on value "); put_n(o, note && !p.on ? 1 : p.on); }
    } else {
        int uses_off = p.type == UT_HOLD || p.type == UT_TOGGLE;
        o = put(L[0], "1 off value "); if (uses_off) put_n(o, p.off); else put(o, "-");
        o = put(L[1], "2 colour "); put(o, layer == UL_BASE ? UNI_COL_NAME[p.col % UNI_COLS] : "(base)");
        put(L[3], "KNOB MODE: page 1");
    }
}

void uni_knob_lines(uni_knob_t k, char L[4][20])
{
    char *o;
    for (int i = 0; i < 4; i++) L[i][0] = 0;
    o = put(L[0], "1 type "); put(o, UK_NAME[k.type % UK_N]);
    if (k.type == UK_NONE) { put(L[1], "(knob does nothing)"); return; }
    o = put(L[1], "2 ch "); put_n(o, k.ch + 1);
    if (k.type == UK_CC) { o = put(L[2], "3 CC "); put_n(o, k.cc); }
    else put(L[2], "3 -");
    o = put(L[3], "4 start "); put_n(o, k.init);
}

void uni_knob_row(uni_knob_t k, int slot, char out[20])
{
    char *o = out;
    *o++ = (char)('1' + (slot >> 2 & 3)); *o++ = (char)('A' + (slot & 3)); *o++ = ' '; *o = 0;
    if (k.type == UK_NONE) { put(o, "--"); return; }
    if (k.type == UK_PB) o = put(o, "PB");
    else { o = put(o, "CC"); o = put_n(o, k.cc); }
    o = put(o, " ch"); put_n(o, k.ch + 1);
}

/* ---- patches: a cursor walks the output entries ONCE ----
 * Output order: kept base entries in order (a replaced one in place, a deleted one left out), then the slots whose key
 * the base doesn't hold. Init marks which keys the base holds and which have a slot (two bitmaps, O(entries + slots));
 * from then on each step is O(1) for a key with no slot, O(slots) for one with a slot (at most UNI_SLOTS of those). */

static int slot_on(const uni_patch_t *s, int page, unsigned mask) { return s->state != US_FREE && s->page == page && (mask >> s->state & 1); }
static int bit(const uint8_t *b, int k) { return b[k >> 3] >> (k & 7) & 1; }
/* slot i is not the first counting slot of its key (main.c keeps one per key; this keeps len, read and sum agreeing anyway) */
static int dup_before(const uni_patch_t *ps, int i, int page, unsigned mask)
{
    for (int j = 0; j < i; j++) { UNI_OP(); if (slot_on(&ps[j], page, mask) && ps[j].key == ps[i].key) return 1; }
    return 0;
}

void uni_cur_init(uni_cur_t *c, const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask)
{
    memset(c, 0, sizeof *c);
    if (!base || !uni_len_ok(blen)) { base = 0; blen = 0; }
    c->base = base; c->blen = blen; c->ps = ps; c->n = n; c->page = page; c->mask = mask;
    for (uint32_t o = UNI_HDR; o < blen; o += UNI_ENT) {
        UNI_OP();
        int k = uni_key(base + o);
        if (k < UNI_KEYS) c->has[k >> 3] |= (uint8_t)(1u << (k & 7));
    }
    for (int i = 0; i < n; i++) {
        UNI_OP();
        if (slot_on(&ps[i], page, mask) && ps[i].key < UNI_KEYS) c->slot[ps[i].key >> 3] |= (uint8_t)(1u << (ps[i].key & 7));
    }
    c->bo = UNI_HDR; c->si = 0; c->idx = -1; c->e = 0;
}

/* the next output entry into c->e (0 at the end) */
static int cur_next(uni_cur_t *c)
{
    while (c->bo < c->blen) {
        const uint8_t *b = c->base + c->bo;
        c->bo += UNI_ENT;
        UNI_OP();
        int k = uni_key(b);
        if (k < UNI_KEYS && bit(c->slot, k)) {
            const uni_patch_t *s = 0;
            for (int i = 0; i < c->n && !s; i++) { UNI_OP(); if (slot_on(&c->ps[i], c->page, c->mask) && c->ps[i].key == k) s = &c->ps[i]; }
            if (s && s->del) continue;
            c->e = s ? s->e : b;
        } else c->e = b;
        c->idx++;
        return 1;
    }
    while (c->si < c->n) {
        const uni_patch_t *s = &c->ps[c->si++];
        UNI_OP();
        if (slot_on(s, c->page, c->mask) && !s->del && s->key < UNI_KEYS && !bit(c->has, s->key)
            && !dup_before(c->ps, c->si - 1, c->page, c->mask)) { c->e = s->e; c->idx++; return 1; }
    }
    c->e = 0;
    return 0;
}

void uni_cur_read(uni_cur_t *c, uint32_t off, uint8_t *dst, uint32_t k)
{
    for (uint32_t i = 0; i < k; i++, off++) {
        if (off < UNI_HDR) { dst[i] = c->base ? c->base[off] : (off == 0 ? UNI_VER : ' '); continue; }
        int want = (int)((off - UNI_HDR) / UNI_ENT);
        if (want < c->idx || (c->idx >= 0 && !c->e)) {   /* behind the cursor (or it ran off the end): start over */
            c->bo = UNI_HDR; c->si = 0; c->idx = -1; c->e = 0;
        }
        while (c->idx < want) if (!cur_next(c)) break;
        dst[i] = (c->idx == want && c->e) ? c->e[(off - UNI_HDR) % UNI_ENT] : 0;
    }
}

uint32_t uni_patched_len(const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask)
{
    uni_cur_t c;
    uni_cur_init(&c, base, blen, ps, n, page, mask);
    int32_t cnt = (int32_t)(c.blen > UNI_HDR ? (c.blen - UNI_HDR) / UNI_ENT : 0);
    for (int i = 0; i < n; i++) {                 /* each counting slot: a delete of a stored key -1, a new key +1 */
        UNI_OP();
        const uni_patch_t *s = &ps[i];
        if (!slot_on(s, page, mask) || s->key >= UNI_KEYS) continue;
        if (dup_before(ps, i, page, mask)) continue;   /* (only the first slot of a key counts, as in the cursor) */
        if (bit(c.has, s->key)) { if (s->del) cnt--; }
        else if (!s->del) cnt++;
    }
    return UNI_HDR + UNI_ENT * (uint32_t)cnt;
}

void uni_patched_read(const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask,
                      uint32_t off, uint8_t *dst, uint32_t k)
{
    uni_cur_t c;
    uni_cur_init(&c, base, blen, ps, n, page, mask);
    uni_cur_read(&c, off, dst, k);
}

uint16_t uni_patched_sum(const uint8_t *base, uint32_t blen, const uni_patch_t *ps, int n, int page, unsigned mask)
{
    uni_cur_t c;
    uni_cur_init(&c, base, blen, ps, n, page, mask);
    uint32_t s = 0;
    for (uint32_t i = 0; i < UNI_HDR; i++) s += c.base ? c.base[i] : (i == 0 ? UNI_VER : ' ');
    while (cur_next(&c)) for (int i = 0; i < UNI_ENT; i++) s += c.e[i];
    return (uint16_t)(s & 0x3FFF);
}

int uni_begin_check(int page, uint32_t len, uint32_t used_others)
{
    if (page < 0 || page >= UNI_PAGES || !uni_len_ok(len)) return UR_BAD;
    if (used_others + len > UNI_BUDGET) return UR_FULL;
    return UR_OK;
}

int uni_chunk_check(uint32_t off, uint32_t n, uint32_t done, uint32_t len)
{
    if (off != done) return UR_ORDER;
    if (n == 0 || n > UNI_CHUNK || off + n > len) return UR_BAD;
    if (n != UNI_CHUNK && off + n != len) return UR_BAD;   /* every chunk but the last is full (whole flash words) */
    return UR_OK;
}
