#include "ctl.h"


ctl_host_t ctl_host_parse(const uint8_t *p, unsigned n)
{
    ctl_host_t h = {HC_NONE, 0, 0, 0};
    if (!p || !n) return h;
    switch (p[0]) {
    case 0x00: if (n == 1) h.cmd = HC_HELLO; break;
    case 0x01: if (n == 17) { h.cmd = HC_LEVELS; h.data = p + 1; h.len = 16; } break;
    case 0x02: if (n == 2 && p[1] < 2) { h.cmd = HC_LIGHTS; h.arg = p[1]; } break;
    case 0x06: if (n == 1 || (n == 2 && p[1] < 2)) { h.cmd = HC_CRASH; h.arg = n == 2 ? p[1] : 0; } break;
    case 0x07: if (n == 1 || (n == 2 && p[1] == 1)) { h.cmd = HC_STATUS; h.arg = n == 2; } break;   /* 07 01: + reset the loop max */
    case 0x08: if (n == 1) h.cmd = HC_STACK; break;
    case 0x0A:
        if (n >= 3 && p[1] < 64 && p[2] >= 1 && p[2] <= CTL_SURF_MAX && p[1] + p[2] <= 64 && n == 3u + 3u * p[2]) {
            int ok = 1;
            for (unsigned i = 3; i < n; i++) if (p[i] > 7) ok = 0;
            if (ok) { h.cmd = HC_COLORS; h.arg = p[1]; h.data = p + 3; h.len = p[2]; }
        }
        break;
    case 0x0B:
        if (n == 2 && p[1] == 0) { h.cmd = HC_SURFACE; h.arg = 0; }
        else if (n == 3 && p[1] == 1 && p[2] >= 1 && p[2] <= 16) { h.cmd = HC_SURFACE; h.arg = p[2]; }
        break;
    case 0x0D: if (n == 2 && p[1] < 32) { h.cmd = HC_QUEUE; h.arg = p[1]; } break;
    case 0x0E: if (n == 2 && p[1] < 2) { h.cmd = HC_BARS; h.arg = p[1]; } break;
    case 0x10: if (n == 4 && p[1] < 8 && p[2] < 128 && p[3] < 128) { h.cmd = HC_UBEGIN; h.arg = p[1]; h.len = (unsigned)(p[2] << 7 | p[3]); } break;
    case 0x11:
        if (n >= 4 && n <= 3 + 24 && p[1] < 128 && p[2] < 128) {
            int ok = 1;
            for (unsigned i = 3; i < n; i++) if (p[i] > 127) ok = 0;
            if (ok) { h.cmd = HC_UCHUNK; h.arg = p[1] << 7 | p[2]; h.data = p + 3; h.len = n - 3; }
        }
        break;
    case 0x12: if (n == 3 && p[1] < 128 && p[2] < 128) { h.cmd = HC_UCOMMIT; h.arg = p[1] << 7 | p[2]; } break;
    case 0x13: if (n == 4 && p[1] < 8 && p[2] < 128 && p[3] < 128) { h.cmd = HC_UREAD; h.arg = p[1]; h.len = (unsigned)(p[2] << 7 | p[3]); } break;
    case 0x14: if (n == 1) h.cmd = HC_ULIST; break;
    case 0x15: if (n == 2 && p[1] < 8) { h.cmd = HC_UCLEAR; h.arg = p[1]; } break;
    case 0x1A: if (n == 2 && p[1] <= CTL_DAW_ROWS) { h.cmd = HC_DAWN; h.arg = p[1]; } break;   /* v0.31d the DAW page */
    case 0x1B: if (n >= 7 && p[1] < CTL_DAW_ROWS) { h.cmd = HC_DAWROW; h.arg = p[1]; h.data = p + 2; h.len = n - 2; } break;
    case 0x1C: if (n == 4 && p[1] < CTL_DAW_ROWS) { h.cmd = HC_DAWLVL; h.arg = p[1]; h.len = (unsigned)((p[2] & 0x7F) << 7 | (p[3] & 0x7F)); } break;   /* v0.31f 14-bit */
    case 0x20: if (n >= 2) { h.cmd = HC_INPUT; h.arg = p[1]; h.data = p + 2; h.len = n - 2; } break;   /* v0.31r remote input: remote.c checks the rest */
    case 0x21: if (n == 2 && p[1] <= 2) { h.cmd = HC_HOSTM; h.arg = p[1]; } break;   /* v0.33 ORC mode off / on / still here */
    case 0x24: if (n == 2 && p[1] >= 10 && p[1] <= 30) { h.cmd = HC_GAMMA; h.arg = p[1]; } break;   /* v0.33 pad colour curve x 10 */
    case 0x25: if (n == 2 && (p[1] == 0 || (p[1] >= 12 && p[1] <= 64))) { h.cmd = HC_FINEW; h.arg = p[1]; } break;   /* v0.34 the short LED slot: ask (0) / set its share */
    case 0x09:
        if (n == 6) {
            uint32_t a = 0;
            int ok = 1;
            for (int i = 4; i >= 0; i--) { if (p[1 + i] & 0x80) ok = 0; a = (a << 7) | (p[1 + i] & 0x7F); }
            if (ok && ctl_peek_ok(a)) { h.cmd = HC_PEEK; h.data = p + 1; h.len = 5; h.arg = (int)a; }
        }
        break;
    default: break;
    }
    return h;
}

int ctl_queue_pat(int n, int song, int song_len, int npat, int running, int cur, int *queued)
{
    if (n < 0 || n >= (song ? song_len : npat)) return CQ_REFUSE;
    if (!running) return CQ_NOW;
    *queued = (!song && n == cur) ? -1 : n;          /* (song mode: a slot is queued even if it is the one playing) */
    return CQ_QUEUE;
}

int ctl_bar_msg(uint8_t *out, uint32_t g, int page, int pat, int slot)
{
    if (page <= 0 || g % (uint32_t)page) return 0;
    out[0] = 0xF0; out[1] = 0x7D; out[2] = 0x4F; out[3] = 0x52; out[4] = 0x43; out[5] = 0x0F;
    out[6] = (uint8_t)(pat & 0x7F); out[7] = (uint8_t)(slot & 0x7F); out[8] = (uint8_t)((g / (uint32_t)page) & 0x7F);
    out[9] = 0xF7;
    return CTL_BAR_LEN;
}

int ctl_peek_ok(uint32_t a)
{
    if (a & 3) return 0;
    return (a >= 0x08000000u && a < 0x08020000u)       /* flash (bootloader, settings, app) */
        || (a >= 0x20000000u && a < 0x20010000u)       /* RAM */
        || (a >= 0x40000000u && a < 0x40008000u)       /* APB1 */
        || (a >= 0x40010000u && a < 0x40015800u)       /* APB2 */
        || (a >= 0x40020000u && a < 0x40026800u)       /* AHB1: GPIO, CRC, RCC, flash regs, DMA1/2 */
        || (a >= 0x50000000u && a < 0x50040000u)       /* AHB2: USB OTG FS */
        || (a >= 0xE000E000u && a < 0xE000F000u);      /* SCB, NVIC, SysTick, MPU, FPU */
}

int ctl_sysex_payload(const uint8_t *sx, unsigned len, unsigned cap, const uint8_t **p, unsigned *n)
{
    if (len < 7 || len >= cap) return 0;            /* too short; or it filled the buffer (cut short): drop it */
    if (sx[0] != 0xF0 || sx[len - 1] != 0xF7) return 0;
    if (sx[1] != 0x7D || sx[2] != 0x4F || sx[3] != 0x52 || sx[4] != 0x43) return 0;
    *p = sx + 5;
    *n = len - 6;
    return 1;
}

int ctl_frame_due(uint32_t now_us, uint32_t last_us) { return (uint32_t)(now_us - last_us) >= CTL_FRAME_US; }

int ctl_host_timeout(int lt_on, int lt_host, int show_is_host, uint32_t now_ms, uint32_t host_ms)
{
    if (!lt_on || !lt_host) return HT_NONE;
    if ((uint32_t)(now_ms - host_ms) <= CTL_HOST_QUIET_MS) return HT_NONE;
    return show_is_host ? HT_FALLBACK : HT_OFF;
}

int ctl_host_reengage(int lt_on, int lt_host, int show_is_host) { return lt_on && !lt_host && show_is_host; }

int ctl_host_stop_ends(int lt_on, int lt_host, int by_host) { return lt_on && (lt_host || by_host); }

uint32_t ctl_lazy_arm(uint32_t now_ms)
{
    uint32_t d = now_ms + CTL_LAZY_SAVE_MS;
    return d ? d : 1;
}

int ctl_lazy_due(uint32_t lazy_due, uint32_t now_ms) { return lazy_due != 0 && (int32_t)(now_ms - lazy_due) >= 0; }

int ctl_row_press(int *held, int *turned, uint8_t *consumed, int row)
{
    int commit = -1;
    if (*held >= 0 && *held != row && !*turned) {   /* a row is already down and untouched: its tap counts now */
        commit = *held;
        *consumed = (uint8_t)(*consumed | (1u << *held));
    }
    *held = row;
    *turned = 0;
    return commit;
}

int ctl_row_release(int *held, int *turned, uint8_t *consumed, int row)
{
    if (*consumed & (1u << row)) { *consumed = (uint8_t)(*consumed & ~(1u << row)); return 0; }
    if (*held != row) return 0;                     /* not the row the knobs belong to */
    *held = -1;
    return !*turned;
}

int ctl_step_displace(int held_pad, int was_lit, int turned) { return held_pad >= 0 && was_lit && !turned; }

int ctl_trk_level(uint8_t stored) { return stored == 0 ? 127 : (stored > 127 ? 127 : stored - 1); }

uint8_t ctl_trk_store(int level)
{
    if (level >= 127) return 0;
    if (level < 0) level = 0;
    return (uint8_t)(level + 1);
}

int ctl_trk_vel(int vel, int level)
{
    if (vel <= 0) return vel;
    if (level >= 127) return vel;
    if (level < 0) level = 0;
    int r = (vel * level + 63) / 127;
    return r < 1 ? 1 : r;
}

int ctl_trk_pad_level(int col)
{
    if (col < 0) col = 0;
    if (col > 15) col = 15;
    return (col * 127 + 7) / 15;
}

int ctl_tap(ctl_tap_t *s, uint32_t now_ms)
{
    if (s->n > 0) {
        uint32_t gap = (uint32_t)(now_ms - s->t[s->n - 1]);
        if (gap > CTL_TAP_GAP_MS) { s->n = 0; s->probation = 0; }   /* a pause: a new count, nothing old to protect */
        else if (gap < CTL_TAP_MIN_MS) return 0;                    /* contact bounce */
        else if (s->n >= 2) {                                       /* far from the running tempo: a change or a missed tap */
            uint32_t mean = (uint32_t)(s->t[s->n - 1] - s->t[0]) / (uint32_t)(s->n - 1);
            if (gap * 100u > mean * 130u || gap * 100u < mean * 70u) { s->n = 0; s->probation = 1; }
        }
    }
    if (s->n == CTL_TAP_MAX) {                                      /* window full: drop the oldest tap */
        for (int i = 1; i < CTL_TAP_MAX; i++) s->t[i - 1] = s->t[i];
        s->n--;
    }
    s->t[s->n++] = now_ms;
    if (s->n < 2) return 0;
    if (s->probation) { if (s->n < 3) return 0; s->probation = 0; }   /* a restart needs a 3rd tap before it changes the tempo */
    uint32_t span = (uint32_t)(s->t[s->n - 1] - s->t[0]);
    int bpm = (int)((60000u * (uint32_t)(s->n - 1) + span / 2) / span);
    return bpm < 40 ? 40 : bpm > 300 ? 300 : bpm;
}

int ctl_fader_tip(int level)
{
    if (level < 0) level = 0;
    if (level > 127) level = 127;
    return (level * 15 + 63) / 127;
}

int ctl_fader_pad(int col, int level)
{
    int tip = ctl_fader_tip(level);
    return col < tip ? 2 : col == tip ? 1 : 0;
}

uint32_t ctl_fader_follow(uint8_t *val, const uint8_t *ch, const uint8_t *cc, int n, int status, int num, int value)
{
    if ((status & 0xF0) != 0xB0 || num < 0 || num > 127) return 0;
    int c = (status & 15) + 1;                      /* faders store channels 1-16 */
    uint8_t v = (uint8_t)(value < 0 ? 0 : value > 127 ? 127 : value);
    if (n > 32) n = 32;
    uint32_t changed = 0;
    for (int f = 0; f < n; f++)
        if (ch[f] == c && cc[f] == num && val[f] != v) { val[f] = v; changed |= 1u << f; }
    return changed;
}

/* ---- fader sets + the DAW page (v0.31d) ---- */
int ctl_fset_max_top(int n, int add_row, int cap, int view)
{
    int rows = n + (add_row && n < cap ? 1 : 0);
    return rows > view ? rows - view : 0;
}

int ctl_fset_scroll(int *top, int d, int max_top)
{
    int was = *top, t = was + d;
    if (t > max_top) t = max_top;
    if (t < 0) t = 0;
    *top = t;
    if (d && t == was) return d < 0 ? -1 : 1;
    return 0;
}

int ctl_daw_bank_top(int edge, int max_top)
{
    return edge > 0 ? 0 : max_top;
}

int ctl_fset_set(uint8_t *val, int f, int v)
{
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (val[f] == v) return 0;
    val[f] = (uint8_t)v;
    return 1;
}

void ctl_daw_count(ctl_daw_t *d, int n)
{
    if (n < 0) n = 0;
    if (n > CTL_DAW_ROWS) n = CTL_DAW_ROWS;
    d->n = (uint8_t)n;
    for (int r = n; r < CTL_DAW_ROWS; r++) {
        d->val[r] = 0; d->fine[r] = 0; d->rgb[r][0] = d->rgb[r][1] = d->rgb[r][2] = 0;
        for (int i = 0; i <= CTL_DAW_NAME; i++) d->name[r][i] = 0;
    }
}

void ctl_daw_rgb(int r, int g, int b, uint8_t out[3])
{
    int c[3] = {r, g, b}, mx = 0;
    for (int i = 0; i < 3; i++) { c[i] = c[i] < 0 ? 0 : c[i] > 127 ? 127 : c[i]; if (c[i] > mx) mx = c[i]; }
    for (int i = 0; i < 3; i++) out[i] = (uint8_t)(mx ? (c[i] * 7 + mx / 2) / mx : 6);
}

void ctl_daw_row(ctl_daw_t *d, int row, const uint8_t *data, unsigned len)
{
    if (row < 0 || row >= CTL_DAW_ROWS || len < 5) return;
    ctl_daw_rgb(data[0], data[1], data[2], d->rgb[row]);
    ctl_daw_level(d, row, (data[3] & 0x7F) << 7 | (data[4] & 0x7F));
    unsigned k = len - 5;
    if (k > CTL_DAW_NAME) k = CTL_DAW_NAME;
    for (unsigned i = 0; i < k; i++) { uint8_t ch = data[5 + i]; d->name[row][i] = (char)(ch >= 0x20 && ch <= 0x7E ? ch : '?'); }
    for (unsigned i = k; i <= CTL_DAW_NAME; i++) d->name[row][i] = 0;
}

int ctl_daw_level(ctl_daw_t *d, int row, int fine)
{
    if (row < 0 || row >= CTL_DAW_ROWS) return 0;
    fine = fine < 0 ? 0 : fine > 16383 ? 16383 : fine;
    if (d->fine[row] == fine) return 0;
    d->fine[row] = (uint16_t)fine;
    d->val[row] = (uint8_t)(fine >> 7);
    return 1;
}

int ctl_daw_tap_fine(int level)
{
    level = level < 0 ? 0 : level > 127 ? 127 : level;
    return (level * 16383 + 63) / 127;
}

int ctl_daw_knob_step(uint32_t dt_ms)
{
    return dt_ms >= 120 ? 1 : dt_ms >= 60 ? 8 : dt_ms >= 30 ? 32 : 128;
}

int ctl_daw_level_msg(uint8_t *out, int row, int fine)
{
    out[0] = 0xF0; out[1] = 0x7D; out[2] = 0x4F; out[3] = 0x52; out[4] = 0x43; out[5] = 0x1D;
    out[6] = (uint8_t)(row & 0x7F); out[7] = (uint8_t)((fine >> 7) & 0x7F); out[8] = (uint8_t)(fine & 0x7F); out[9] = 0xF7;
    return CTL_DAW_LEVEL_LEN;
}

int ctl_daw_bank_msg(uint8_t *out, int dir, int count)
{
    out[0] = 0xF0; out[1] = 0x7D; out[2] = 0x4F; out[3] = 0x52; out[4] = 0x43; out[5] = 0x1E;
    out[6] = (uint8_t)(dir > 0 ? 1 : 0); out[7] = (uint8_t)(count < 1 ? 1 : count > 127 ? 127 : count); out[8] = 0xF7;
    return CTL_DAW_BANK_LEN;
}

/* ---- the roller / looper (v0.29) ---- */
const char *const CTL_ROLL_NAME[CTL_ROLL_CODES] = {"1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/48", "1/64"};
static const uint8_t ROLL_LOOP[CTL_ROLL_CODES] = {16, 8, 4, 2, 1, 1, 1, 1};   /* loop length in steps */
static const uint8_t ROLL_HITS[CTL_ROLL_CODES] = {0, 0, 0, 0, 0, 2, 3, 4};    /* 0 = the step's own ratchet */

uint32_t ctl_roll_g(uint32_t g, uint32_t anchor, int code)
{
    if (code < 0 || code >= CTL_ROLL_CODES || g < anchor) return g;
    return anchor + (g - anchor) % ROLL_LOOP[code];
}

int ctl_roll_hits(int code) { return code >= 0 && code < CTL_ROLL_CODES ? ROLL_HITS[code] : 0; }

int ctl_roll_code(const ctl_roll_pad_t *pads, int n, int track)
{
    int mine = -1, all = -1;
    uint32_t ms = 0, as = 0;
    for (int i = 0; i < n; i++) {
        const ctl_roll_pad_t *p = &pads[i];
        if (p->code < 0 || p->code >= CTL_ROLL_CODES) continue;
        if (p->track == track && (mine < 0 || p->seq > ms)) { mine = p->code; ms = p->seq; }
        else if (p->track < 0 && (all < 0 || p->seq > as)) { all = p->code; as = p->seq; }
    }
    return mine >= 0 ? mine : all;
}
