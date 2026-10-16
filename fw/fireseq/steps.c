#include <string.h>
#include "steps.h"

#define NBLK (ST_PATTERNS * ST_TRACKS)

uint8_t steps_len[ST_PATTERNS][ST_TRACKS];
static uint32_t map[ST_PATTERNS][ST_TRACKS][4];     /* bit s = step s is lit */
static uint16_t off[NBLK + 1];                      /* pool offset of each (pattern, track) block; off[NBLK] = bytes used */
static uint8_t pool[ST_POOL];
uint16_t steps_saved[ST_PATTERNS];                  /* data bytes of each pattern's newest SAVED record (set by main.c) */

/* field offsets inside an entry */
enum { D_B = 0, D_COND = 1, D_LOCK = 2 };
enum { M_B = 0, M_NOTE = 1, M_COND = 2, M_LOCK = 3, M_CH2 = 4, M_CH3 = 5 };

static inline int blk(int p, int t) { return p * ST_TRACKS + t; }
static inline bool mel(int t) { return t >= ST_MEL_FIRST; }
static inline uint32_t esize(int t) { return mel(t) ? ST_MEL_SIZE : ST_DRUM_SIZE; }
static inline bool ok(int p, int t, int s) { return p >= 0 && p < ST_PATTERNS && t >= 0 && t < ST_TRACKS && s >= 0 && s < ST_MAXSTEPS; }
static inline bool lit(int p, int t, int s) { return map[p][t][s >> 5] >> (s & 31) & 1u; }

static uint32_t popc(uint32_t v) { v = v - ((v >> 1) & 0x55555555u); v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u); return (((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24; }

/* lit steps of (p, t) before step s */
static uint32_t rank(int p, int t, int s)
{
    uint32_t r = 0;
    for (int w = 0; w < (s >> 5); w++) r += popc(map[p][t][w]);
    if (s & 31) r += popc(map[p][t][s >> 5] & ((1u << (s & 31)) - 1u));
    return r;
}
static uint32_t count4(const uint32_t *m) { return popc(m[0]) + popc(m[1]) + popc(m[2]) + popc(m[3]); }
static uint32_t live_size(int p) { return (uint32_t)(off[blk(p, ST_TRACKS)] - off[blk(p, 0)]); }

/* Room check: each pattern counts as the larger of its live size and its saved record, so the
   records in flash always fit back into the pool at boot (even if a shrink isn't saved yet).
   true if pattern p can become new_size bytes. */
static bool fits(int p, uint32_t new_size)
{
    uint32_t total = 0;
    for (int q = 0; q < ST_PATTERNS; q++) {
        uint32_t live = q == p ? new_size : live_size(q), sv = steps_saved[q];
        total += live > sv ? live : sv;
    }
    return total <= ST_POOL;
}

/* open n bytes at pool position pos; blocks after block k move up */
static void open_gap(uint32_t pos, uint32_t n, int k)
{
    memmove(pool + pos + n, pool + pos, off[NBLK] - pos);
    for (int j = k + 1; j <= NBLK; j++) off[j] = (uint16_t)(off[j] + n);
}
static void close_gap(uint32_t pos, uint32_t n, int k)
{
    memmove(pool + pos, pool + pos + n, off[NBLK] - pos - n);
    for (int j = k + 1; j <= NBLK; j++) off[j] = (uint16_t)(off[j] - n);
}

static void pack(int t, const step_t *v, uint8_t *e)
{
    if (mel(t)) { e[M_B] = v->b; e[M_NOTE] = v->note; e[M_COND] = v->cond; e[M_LOCK] = v->lock; e[M_CH2] = (uint8_t)v->ch2; e[M_CH3] = (uint8_t)v->ch3; }
    else { e[D_B] = v->b; e[D_COND] = v->cond; e[D_LOCK] = v->lock; }
}
static void unpack(int t, const uint8_t *e, step_t *v)
{
    memset(v, 0, sizeof *v);
    if (mel(t)) { v->b = e[M_B]; v->note = e[M_NOTE]; v->cond = e[M_COND]; v->lock = e[M_LOCK]; v->ch2 = (int8_t)e[M_CH2]; v->ch3 = (int8_t)e[M_CH3]; }
    else { v->b = e[D_B]; v->cond = e[D_COND]; v->lock = e[D_LOCK]; }
}

uint32_t steps_pattern_size(int p) { return p >= 0 && p < ST_PATTERNS ? live_size(p) : 0; }

void steps_clear_all(void)
{
    memset(steps_len, 0, sizeof steps_len);
    memset(map, 0, sizeof map);
    memset(off, 0, sizeof off);
}

void steps_clear_pattern(int p)
{
    if (p < 0 || p >= ST_PATTERNS) return;
    uint32_t a = off[blk(p, 0)], n = off[blk(p, ST_TRACKS)] - a;
    close_gap(a, n, blk(p, ST_TRACKS) - 1);
    for (int t = 1; t < ST_TRACKS; t++) off[blk(p, t)] = (uint16_t)a;
    memset(map[p], 0, sizeof map[p]);
}

bool steps_read(int p, int t, int s, step_t *o)
{
    if (!ok(p, t, s) || !lit(p, t, s)) { memset(o, 0, sizeof *o); return false; }
    unpack(t, pool + off[blk(p, t)] + rank(p, t, s) * esize(t), o);
    return true;
}

bool steps_write(int p, int t, int s, const step_t *v)
{
    if (!ok(p, t, s)) return false;
    int k = blk(p, t);
    uint32_t e = esize(t), pos = off[k] + rank(p, t, s) * e;
    if (!(v->b & 3)) {                              /* off: remove the step */
        if (lit(p, t, s)) { close_gap(pos, e, k); map[p][t][s >> 5] &= ~(1u << (s & 31)); }
        return true;
    }
    if (!lit(p, t, s)) {
        if (off[NBLK] + e > ST_POOL || !fits(p, live_size(p) + e)) return false;
        open_gap(pos, e, k);
        map[p][t][s >> 5] |= 1u << (s & 31);
    }
    pack(t, v, pool + pos);
    return true;
}

uint8_t steps_get(int p, int t, int s) { step_t v; steps_read(p, t, s, &v); return v.b; }
uint8_t steps_note(int p, int t, int s) { step_t v; steps_read(p, t, s, &v); return mel(t) ? v.note : 0; }

bool steps_set(int p, int t, int s, uint8_t b, uint8_t note)
{
    step_t v;
    steps_read(p, t, s, &v);                        /* a lit step keeps its other fields */
    v.b = b; v.note = note;
    return steps_write(p, t, s, &v);
}

bool steps_set_byte(int p, int t, int s, uint8_t b) { return steps_set(p, t, s, b, steps_note(p, t, s)); }

bool steps_copy_pattern(int src, int dst)
{
    if (src < 0 || src >= ST_PATTERNS || dst < 0 || dst >= ST_PATTERNS) return false;
    if (src == dst) return true;
    uint32_t ssz = off[blk(src, ST_TRACKS)] - off[blk(src, 0)];
    uint32_t dsz = off[blk(dst, ST_TRACKS)] - off[blk(dst, 0)];
    if (off[NBLK] - dsz + ssz > ST_POOL || !fits(dst, ssz)) return false;
    steps_clear_pattern(dst);
    uint32_t at = off[blk(dst, 0)];
    open_gap(at, ssz, blk(dst, ST_TRACKS) - 1);     /* only the blocks after dst move */
    uint32_t sa = off[blk(src, 0)];                 /* (after the gap: src may have moved) */
    memcpy(pool + at, pool + sa, ssz);
    for (int t = 1; t < ST_TRACKS; t++) off[blk(dst, t)] = (uint16_t)(at + off[blk(src, t)] - sa);
    memcpy(map[dst], map[src], sizeof map[dst]);
    memcpy(steps_len[dst], steps_len[src], sizeof steps_len[dst]);
    return true;
}

bool steps_track_empty(int t)
{
    if (t < 0 || t >= ST_TRACKS) return true;
    for (int p = 0; p < ST_PATTERNS; p++) if (count4(map[p][t])) return false;
    return true;
}

uint32_t steps_used(void) { return off[NBLK]; }

/* ---------- saved records ---------- */

uint32_t steps_record_len(int p) { return ST_REC_HDR + (uint32_t)(off[blk(p, ST_TRACKS)] - off[blk(p, 0)]); }

void steps_record_read(int p, uint32_t o, uint8_t *dst, uint32_t n)
{
    uint32_t len = steps_record_len(p);
    for (uint32_t i = 0; i < n; i++, o++) {
        uint8_t v;
        if (o >= len) v = 0xFF;
        else if (o == 0) v = 2;                     /* format */
        else if (o == 1) v = ST_DRUM_SIZE;
        else if (o == 2) v = ST_MEL_SIZE;
        else if (o == 3) v = 0;
        else if (o < 4 + ST_TRACKS) v = steps_len[p][o - 4];
        else if (o < ST_REC_HDR) { uint32_t m = o - 4 - ST_TRACKS; v = (uint8_t)(map[p][m / 16][m % 16 / 4] >> (8 * (m % 4))); }
        else v = pool[off[blk(p, 0)] + o - ST_REC_HDR];
        dst[i] = v;
    }
}

/* Load lengths + maps + entries of size dsz (drum) / msz (melodic): each entry's first
   min(stored, current) bytes are kept and the rest zeroed (fields are only ever appended). */
static bool load_body(int p, const uint8_t *lens, const uint8_t *maps, const uint8_t *d, uint32_t dlen, uint32_t dsz, uint32_t msz)
{
    if (!dsz || !msz) return false;
    uint32_t m[ST_TRACKS][4], have = 0, need = 0;
    for (int t = 0; t < ST_TRACKS; t++) {
        for (int w = 0; w < 4; w++) {
            const uint8_t *q = maps + t * 16 + w * 4;
            m[t][w] = (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24;
        }
        have += count4(m[t]) * (mel(t) ? msz : dsz);
        need += count4(m[t]) * esize(t);
    }
    if (have != dlen) return false;                 /* maps and data disagree */
    for (uint32_t i = 0, t = 0; t < ST_TRACKS; t++) {   /* every stored step must be lit (level != 0) */
        uint32_t sz = mel((int)t) ? msz : dsz, c = count4(m[t]);
        for (uint32_t j = 0; j < c; j++, i += sz) if (!(d[i] & 3)) return false;
    }
    uint32_t cur = off[blk(p, ST_TRACKS)] - off[blk(p, 0)];
    if (off[NBLK] - cur + need > ST_POOL || !fits(p, need)) return false;
    steps_clear_pattern(p);
    uint32_t at = off[blk(p, 0)];
    open_gap(at, need, blk(p, ST_TRACKS) - 1);
    memset(pool + at, 0, need);
    uint32_t src = 0, dstp = at;
    for (int t = 0; t < ST_TRACKS; t++) {
        off[blk(p, t)] = (uint16_t)dstp;
        uint32_t sz = mel(t) ? msz : dsz, e = esize(t), c = count4(m[t]), k = sz < e ? sz : e;
        for (uint32_t j = 0; j < c; j++, src += sz, dstp += e) memcpy(pool + dstp, d + src, k);
    }
    memcpy(map[p], m, sizeof m);
    for (int t = 0; t < ST_TRACKS; t++) steps_len[p][t] = lens[t] <= ST_MAXSTEPS ? lens[t] : 0;
    return true;
}

bool steps_record_load(int p, const uint8_t *rec, uint32_t len)
{
    if (p < 0 || p >= ST_PATTERNS || len < ST_REC_HDR || rec[0] != 2) return false;
    return load_body(p, rec + 4, rec + 4 + ST_TRACKS, rec + ST_REC_HDR, len - ST_REC_HDR, rec[1], rec[2]);
}

bool steps_record_load_v1(int p, const uint8_t *rec, uint32_t len)
{
    if (p < 0 || p >= ST_PATTERNS || len < ST_REC_HDR_V1) return false;
    return load_body(p, rec, rec + ST_TRACKS, rec + ST_REC_HDR_V1, len - ST_REC_HDR_V1, 1, 2);
}

/* ---------- old full-size saves ---------- */

bool steps_convert_full(int p, const uint8_t *r, uint32_t len)
{
    steps_clear_pattern(p);
    memset(steps_len[p], 0, sizeof steps_len[p]);
    for (int t = 0; t < ST_MEL_FIRST; t++) {
        if (len >= 16u + (uint32_t)(t + 1) * ST_MAXSTEPS) {
            steps_len[p][t] = r[t] <= ST_MAXSTEPS ? r[t] : 0;
            for (int s = 0; s < ST_MAXSTEPS; s++) {
                uint8_t b = r[16 + t * ST_MAXSTEPS + s];
                if ((b & 3) && !steps_set(p, t, s, b, 0)) return false;
            }
        }
    }
    if (len >= 4120u)                               /* v0.9 melodic part: len_m @2064, step_m @2072, note_m @3096 */
        for (int i = 0; i < (ST_TRACKS - ST_MEL_FIRST); i++) {
            steps_len[p][ST_MEL_FIRST + i] = r[2064 + i] <= ST_MAXSTEPS ? r[2064 + i] : 0;
            for (int s = 0; s < ST_MAXSTEPS; s++) {
                uint8_t b = r[2072 + i * ST_MAXSTEPS + s];
                if ((b & 3) && !steps_set(p, ST_MEL_FIRST + i, s, b, r[3096 + i * ST_MAXSTEPS + s])) return false;
            }
        }
    return true;
}
