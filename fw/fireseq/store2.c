#include <string.h>
#include "store2.h"

#define MAGIC 0x3343524Fu                           /* "ORC3" */
#define ALIGN 256u
#define HDR 32u

typedef struct {
    uint32_t magic;
    uint16_t kind;
    uint16_t len;
    uint32_t seq;
    uint32_t crc;
    uint32_t commit;
    uint32_t reserved[3];
} hdr_t;

static int32_t newest_off[S2_KINDS];                /* offset of the newest valid record per kind, -1 none */
static uint32_t newest_seq[S2_KINDS];
static uint32_t end_off;                            /* first free byte (256-aligned) */
static uint32_t max_seq;

/* CRC-32, bitwise (no 1 KB table): records are checked once at boot, and a save feeds it a few
   bytes per step, so speed doesn't matter. */
static uint32_t crc_add(uint32_t c, const uint8_t *p, uint32_t n)
{
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
    }
    return c;
}
static uint32_t crc32(const uint8_t *p, uint32_t n) { return ~crc_add(0xFFFFFFFFu, p, n); }

static uint32_t align_up(uint32_t v) { return (v + ALIGN - 1) & ~(ALIGN - 1); }

static bool blank_from(uint32_t off, uint32_t n)
{
    const uint32_t *w = (const uint32_t *)(store_area + off);
    for (uint32_t i = 0; i < n / 4; i++) if (w[i] != 0xFFFFFFFFu) return false;
    return true;
}

void store2_scan(void)
{
    for (int k = 0; k < S2_KINDS; k++) { newest_off[k] = -1; newest_seq[k] = 0; }
    max_seq = 0;
    uint32_t off = 0;
    uint32_t end = 0;                               /* just past the last block that isn't blank */
    while (off + HDR <= S2_AREA_SIZE) {
        const hdr_t *h = (const hdr_t *)(store_area + off);
        if (h->magic == 0xFFFFFFFFu && blank_from(off, HDR)) {
            /* a blank block: the free space starts here, unless something was written after it
               (a save cancelled before its first word left a hole: keep looking past it) */
            if (blank_from(off, S2_AREA_SIZE - off)) break;
            off += ALIGN;
            continue;
        }
        end = off + ALIGN;
        if (h->magic != MAGIC || h->len == 0xFFFFu || h->len > S2_AREA_SIZE - off - HDR) {
            off += ALIGN;                           /* foreign data or a header cut off mid-write: skip a block */
            continue;
        }
        if (h->seq > max_seq && h->seq != 0xFFFFFFFFu) max_seq = h->seq;
        if (h->kind < S2_KINDS && h->commit == 0 && crc32((const uint8_t *)h + HDR, h->len) == h->crc &&
            (newest_off[h->kind] < 0 || h->seq > newest_seq[h->kind])) {
            newest_off[h->kind] = (int32_t)off;
            newest_seq[h->kind] = h->seq;
        }
        off = align_up(off + HDR + h->len);
        end = off;
    }
    end_off = end > S2_AREA_SIZE ? S2_AREA_SIZE : end;
}

bool store2_has_any(void)
{
    for (int k = 0; k < S2_KINDS; k++) if (newest_off[k] >= 0) return true;
    return false;
}

bool store2_read(int kind, void *buf, uint32_t max)
{
    if (kind < 0 || kind >= S2_KINDS || newest_off[kind] < 0) return false;
    const hdr_t *h = (const hdr_t *)(store_area + newest_off[kind]);
    uint32_t n = h->len < max ? h->len : max;
    memcpy(buf, (const uint8_t *)h + HDR, n);
    memset((uint8_t *)buf + n, 0, max - n);
    return true;
}

const uint8_t *store2_ptr(int kind, uint32_t *len)
{
    if (kind < 0 || kind >= S2_KINDS || newest_off[kind] < 0) return 0;
    const hdr_t *h = (const hdr_t *)(store_area + newest_off[kind]);
    *len = h->len;
    return (const uint8_t *)h + HDR;
}

uint32_t store2_free_bytes(void) { return S2_AREA_SIZE - end_off; }

/* ---------- writing: one record at a time, in small steps ----------
 * Order: the header with its CRC and commit words still blank, then the payload (read from the
 * caller's live data a word at a time, CRC'd as it goes, each word read back), then the CRC word,
 * then commit = 0. Until the commit word is written the record doesn't count, so a power cut (or a
 * store2_cancel because the data changed underneath) leaves the previous version in force. */

static struct {
    bool active, failed;
    int kind;
    uint32_t off, len, seq, pos, crc;               /* pos: words written so far */
    s2_reader rd;
    const void *ctx;
    hdr_t hdr;
} W;

static uint32_t total_words(void) { return HDR / 4 + (W.len + 3) / 4 + 2; }   /* + CRC word + commit word */

bool store2_begin_src(int kind, uint32_t len, s2_reader rd, const void *ctx)
{
    if (W.active || kind < 0 || kind >= S2_KINDS || len > S2_MAX_RECORD) return false;
    if (end_off + HDR + len > S2_AREA_SIZE) return false;
    W.kind = kind; W.off = end_off; W.len = len; W.seq = max_seq + 1; W.pos = 0;
    W.rd = rd; W.ctx = ctx; W.crc = 0xFFFFFFFFu;
    W.failed = false;
    memset(&W.hdr, 0xFF, sizeof W.hdr);
    W.hdr.magic = MAGIC; W.hdr.kind = (uint16_t)kind; W.hdr.len = (uint16_t)len; W.hdr.seq = W.seq;
    /* claim the space now, so the next record goes after it even if this one is cut off */
    end_off = align_up(end_off + HDR + len);
    if (W.seq > max_seq) max_seq = W.seq;
    W.active = true;
    return true;
}

static void mem_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { memcpy(dst, (const uint8_t *)ctx + off, n); }

/* From memory that must not change until the record is done (or the caller cancels). */
bool store2_begin(int kind, const void *data, uint32_t len) { return store2_begin_src(kind, len, mem_reader, data); }

bool store2_busy(void) { return W.active; }
int store2_busy_kind(void) { return W.active ? W.kind : -1; }

/* The record stays uncommitted = never counts. If nothing was written yet, its space is given back
   (a blank hole would otherwise look like the end of the log to the next boot's scan). */
void store2_cancel(void)
{
    if (W.active && W.pos == 0 && end_off == align_up(W.off + HDR + W.len)) end_off = W.off;
    W.active = false;
}

static bool program(uint32_t off, uint32_t word)
{
    if (!store_hw_program(off, &word, 1)) return false;
    uint32_t back;
    memcpy(&back, store_area + off, 4);
    return back == word;
}

/* Program up to max_words words. Returns true while there is more to do. */
bool store2_step(uint32_t max_words)
{
    if (!W.active) return false;
    uint32_t hw = HDR / 4, pw = (W.len + 3) / 4, n = 0;
    while (n < max_words && W.pos < total_words()) {
        uint32_t i = W.pos, word, off;
        if (i < hw) {                               /* header, CRC + commit still blank */
            word = ((const uint32_t *)&W.hdr)[i];
            off = W.off + 4 * i;
        } else if (i < hw + pw) {                   /* payload */
            uint32_t at = 4 * (i - hw), k = W.len - at < 4 ? W.len - at : 4;
            uint8_t b[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            W.rd(W.ctx, at, b, k);
            W.crc = crc_add(W.crc, b, k);
            memcpy(&word, b, 4);
            off = W.off + HDR + at;
        } else if (i == hw + pw) {                  /* CRC of what was actually written */
            word = ~W.crc;
            off = W.off + 12;
        } else {                                    /* commit: only now does the record count */
            word = 0;
            off = W.off + 16;
        }
        if (!program(off, word)) {
            W.failed = true; W.active = false;
            if (W.pos == 0 && end_off == align_up(W.off + HDR + W.len) && blank_from(W.off, HDR)) end_off = W.off;
            return false;
        }
        W.pos++; n++;
    }
    if (W.pos < total_words()) return true;
    W.active = false;
    newest_off[W.kind] = (int32_t)W.off;            /* every word was read back: no rescan needed */
    newest_seq[W.kind] = W.seq;
    return false;
}

bool store2_last_failed(void) { return W.failed; }

bool store2_append_src(int kind, uint32_t len, s2_reader rd, const void *ctx)
{
    if (!store2_begin_src(kind, len, rd, ctx)) return false;
    while (store2_step(64)) {}
    return !W.failed;
}

bool store2_append(int kind, const void *data, uint32_t len) { return store2_append_src(kind, len, mem_reader, data); }

bool store2_erase(void)
{
    W.active = false;
    if (!store_hw_erase()) return false;
    store2_scan();                                  /* empty index */
    return true;
}

bool store2_compact(const int *kinds, const void *const *items, const uint32_t *lens, int n)
{
    if (!store2_erase()) return false;
    for (int i = 0; i < n; i++)
        if (!store2_append(kinds[i], items[i], lens[i])) return false;
    return true;
}
