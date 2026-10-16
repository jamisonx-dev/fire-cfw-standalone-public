#include <string.h>
#include "store.h"

#define MAGIC 0x4643524Fu                           /* "ORCF" */
#define VERSION 2u

/* Record layout (1024 bytes, written as 32-bit words):
 *   u32 magic, u16 version, u16 payload_len, u32 seq, u32 crc (over payload), payload..., 0xFF padding */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t len;
    uint32_t seq;
    uint32_t crc;
} rec_hdr_t;

#define NREC (STORE_AREA_SIZE / STORE_REC_SIZE)

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
    }
    return ~c;
}

static bool slot_blank(uint32_t i)
{
    const uint32_t *w = (const uint32_t *)(store_area + i * STORE_REC_SIZE);
    for (uint32_t k = 0; k < STORE_REC_SIZE / 4; k++) if (w[k] != 0xFFFFFFFFu) return false;
    return true;
}

static bool slot_valid(uint32_t i, const rec_hdr_t **hdr)
{
    const rec_hdr_t *h = (const rec_hdr_t *)(store_area + i * STORE_REC_SIZE);
    if (h->magic != MAGIC || h->len == 0 || h->len > STORE_REC_SIZE - sizeof *h) return false;
    if (crc32((const uint8_t *)(h + 1), h->len) != h->crc) return false;
    *hdr = h;
    return true;
}

/* Newest valid record (highest seq) and the first slot after the last used one. */
static void scan(int *newest, uint32_t *next_free, uint32_t *newest_seq)
{
    *newest = -1; *newest_seq = 0; *next_free = NREC;
    for (uint32_t i = 0; i < NREC; i++) {
        const rec_hdr_t *h;
        if (slot_valid(i, &h) && (*newest < 0 || h->seq > *newest_seq)) { *newest = (int)i; *newest_seq = h->seq; }
    }
    /* records are appended in order, so the free space is the run of blank slots at the end */
    for (int32_t i = NREC - 1; i >= 0; i--) {
        if (!slot_blank((uint32_t)i)) break;
        *next_free = (uint32_t)i;
    }
}

bool store_load(store_data_t *out)
{
    int newest; uint32_t next, seq;
    scan(&newest, &next, &seq);
    if (newest < 0) return false;
    const rec_hdr_t *h = (const rec_hdr_t *)(store_area + (uint32_t)newest * STORE_REC_SIZE);
    memset(out, 0, sizeof *out);
    uint32_t n = h->len < sizeof *out ? h->len : sizeof *out;   /* newer/older layouts: take what fits */
    memcpy(out, h + 1, n);
    return true;
}

uint32_t store_free_records(void)
{
    int newest; uint32_t next, seq;
    scan(&newest, &next, &seq);
    return NREC - next;
}

store_result_t store_save(const store_data_t *d, bool allow_erase)
{
    int newest; uint32_t next, seq;
    scan(&newest, &next, &seq);
    if (next >= NREC) {
        if (!allow_erase) return STORE_NEED_ERASE;
        if (!store_hw_erase()) return STORE_FAIL;
        next = 0;
    }
    static uint32_t buf[STORE_REC_SIZE / 4];
    memset(buf, 0xFF, sizeof buf);
    rec_hdr_t *h = (rec_hdr_t *)buf;
    h->magic = MAGIC;
    h->version = VERSION;
    h->len = (uint16_t)sizeof *d;
    h->seq = seq + 1;
    memcpy(h + 1, d, sizeof *d);
    h->crc = crc32((const uint8_t *)(h + 1), h->len);
    /* write the payload first and the header (magic) last: a cut-off save never looks valid */
    uint32_t hw = (uint32_t)(sizeof *h + sizeof *d + 3) / 4;
    if (!store_hw_program(next * STORE_REC_SIZE + 16, buf + 4, hw - 4)) return STORE_FAIL;
    if (!store_hw_program(next * STORE_REC_SIZE, buf, 4)) return STORE_FAIL;
    const rec_hdr_t *chk;
    return slot_valid(next, &chk) ? STORE_OK : STORE_FAIL;
}
