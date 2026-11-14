/* v0.31r remote input: the rules (remote.h). Pure: no hardware, tested on the host by remote_test.c. */
#include <string.h>
#include "remote.h"

#define TURN_MAX 1000                /* detents waiting per turner, at most (a runaway script can't wind up forever) */

int rin_valid_input(int idx) { return idx >= 0 && idx < RIN_INPUTS && !(idx >= 69 && idx <= 71); }

static unsigned q_used(const rin_t *r) { return (unsigned)(uint8_t)(r->qw - r->qr) % RIN_QUEUE; }
static unsigned q_free(const rin_t *r) { return RIN_QUEUE - 1 - q_used(r); }
static void q_push(rin_t *r, uint8_t e) { r->q[r->qw % RIN_QUEUE] = e; r->qw = (uint8_t)((r->qw + 1) % RIN_QUEUE); }

static unsigned held_count(const rin_t *r)
{
    unsigned c = 0;
    for (int i = 0; i < RIN_INPUTS; i++) c += (unsigned)rin_held(r, i);
    return c;
}

int rin_held(const rin_t *r, int idx)
{
    if (idx < 0 || idx >= RIN_INPUTS) return 0;
    return r->held[idx >> 3] >> (idx & 7) & 1;
}

/* Release every held input (a release event each) and drop waiting turns. The queue always has room: a press is only
   taken while the queue can still fit a release for everything held (rin_cmd). */
static void release_all(rin_t *r)
{
    for (int i = 0; i < RIN_INPUTS; i++)
        if (rin_held(r, i)) {
            r->held[i >> 3] &= (uint8_t)~(1u << (i & 7));
            if (q_free(r)) q_push(r, (uint8_t)i);
        }
    memset(r->turn, 0, sizeof r->turn);
}

int rin_cmd(rin_t *r, int sub, const uint8_t *d, unsigned n, uint32_t now)
{
    if (sub == RIN_ARM) {
        if (n != 1 || d[0] > 1) return RIN_BAD;
        if (d[0]) { r->armed = 1; r->last_ms = now; }
        else { if (r->armed) release_all(r); r->armed = 0; }
        return RIN_OK;
    }
    if (sub != RIN_PRESS && sub != RIN_TURN && sub != RIN_RELEASE_ALL) return RIN_BAD;
    if (!r->armed) return RIN_NOT_ARMED;
    if (sub == RIN_RELEASE_ALL) {
        if (n) return RIN_BAD;
        r->last_ms = now;
        release_all(r);
        return RIN_OK;
    }
    if (sub == RIN_PRESS) {
        if (n != 2 || !rin_valid_input(d[0]) || d[1] > 1) return RIN_BAD;
        r->last_ms = now;
        int i = d[0], down = d[1];
        if (rin_held(r, i) == down) return RIN_OK;   /* already so: nothing happens (a real button can't press twice) */
        if (down) {
            if (q_free(r) < held_count(r) + 2) return RIN_FULL;   /* its press + a release for everything then held */
            r->held[i >> 3] |= (uint8_t)(1u << (i & 7));
            q_push(r, (uint8_t)(i | 0x80));
        } else {
            r->held[i >> 3] &= (uint8_t)~(1u << (i & 7));
            q_push(r, (uint8_t)i);                    /* room kept for it by the press */
        }
        return RIN_OK;
    }
    /* RIN_TURN: <turner 0-4> <dir 0 = anticlockwise / 1 = clockwise> <count 1-127> */
    if (n != 3 || d[0] >= RIN_TURNERS || d[1] > 1 || d[2] < 1 || d[2] > 127) return RIN_BAD;
    r->last_ms = now;
    int v = r->turn[d[0]] + (d[1] ? d[2] : -d[2]);
    if (v > TURN_MAX || v < -TURN_MAX) return RIN_FULL;
    r->turn[d[0]] = (int16_t)v;
    return RIN_OK;
}

int rin_tick(rin_t *r, uint32_t now)
{
    if (!r->armed || now - r->last_ms < RIN_QUIET_MS) return 0;   /* unsigned: right across the ms wrap */
    release_all(r);
    r->armed = 0;
    return 1;
}

int rin_event(rin_t *r)
{
    if (r->qr == r->qw) return -1;
    int e = r->q[r->qr % RIN_QUEUE];
    r->qr = (uint8_t)((r->qr + 1) % RIN_QUEUE);
    return e;
}

int rin_turn_take(rin_t *r, int k)
{
    if (k < 0 || k >= RIN_TURNERS) return 0;
    int v = r->turn[k];
    r->turn[k] = 0;
    return v;
}

int rin_turn_pending(const rin_t *r, int k) { return (k < 0 || k >= RIN_TURNERS) ? 0 : r->turn[k]; }
