/* The save area's clean-up: see compact.h. */
#include "compact.h"
#include "uni.h"

bool compact_page_ok(const uint8_t *r, uint32_t len)
{
    return r && uni_len_ok(len) && r[0] == UNI_VER && uni_check(r, len) == UR_OK;
}

typedef struct { const compact_t *c; uint32_t off; } page_src_t;
static uint8_t *scratch_at(const compact_t *c, uint32_t i) { return i < c->sa_len ? c->sa + i : c->sb + (i - c->sa_len); }
static void page_reader(const void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    const page_src_t *s = ctx;
    for (uint32_t i = 0; i < n; i++) dst[i] = *scratch_at(s->c, s->off + off + i);
}

int compact_run(const compact_t *c, uint32_t *kept)
{
    uint32_t len[UNI_PAGES], used = 0;
    const uint8_t *src[UNI_PAGES];
    page_src_t ps[UNI_PAGES];
    for (int p = 0; p < UNI_PAGES; p++) {           /* which pages count, and their room: before anything is touched */
        src[p] = store2_ptr(CP_KIND_UNI + p, &len[p]);
        if (!compact_page_ok(src[p], len[p])) { src[p] = 0; len[p] = 0; continue; }
        ps[p].c = c; ps[p].off = used;
        used += len[p];
    }
    *kept = used;
    if (used > c->sa_len + c->sb_len) return CP_REFUSED;   /* (the budget makes this impossible; never erase pages we can't keep) */
    for (int p = 0; p < UNI_PAGES; p++)
        for (uint32_t i = 0; i < len[p]; i++) *scratch_at(c, ps[p].off + i) = src[p][i];
    bool ok = store2_erase() && store2_append(S2_KIND_GLOBAL, c->global, c->global_len);
    for (int p = 0; ok && p < c->npat; p++)
        if (c->pat_keep(p)) ok = store2_append_src(CP_KIND_PAT + p, c->pat_len(p), c->pat_reader, &c->pat_ids[p]);
    for (int p = 0; ok && p < UNI_PAGES; p++)
        if (len[p]) ok = store2_append_src(CP_KIND_UNI + p, len[p], page_reader, &ps[p]);
    for (int s = 0; ok && c->paint && s < CP_PAINTS; s++)   /* v0.37 the paintings, from their RAM copies */
        if (c->paint_has >> s & 1) ok = store2_append(CP_KIND_PAINT + s, c->paint + s * CP_PAINT_LEN, CP_PAINT_LEN);
    if (ok && c->crash) ok = store2_append(CP_KIND_CRASH, c->crash, CP_CRASH_LEN);   /* v0.39 the last crash, from RAM */
    return ok ? CP_OK : CP_FAIL;
}
