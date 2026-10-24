#include "ctl.h"
#include "lights.h"

const uint8_t SHOW_ORDER[SHOW_N] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, SHOW_HOST, SHOW_TEXT, SHOW_CYCLE};

int ctl_show_pos(int v)
{
    for (int i = 0; i < SHOW_N; i++) if (SHOW_ORDER[i] == v) return i;
    return 0;
}

int ctl_show_step(int v, int d)
{
    int p = ctl_show_pos(v) + (d > 0 ? 1 : -1);
    if (p < 0) p = SHOW_N - 1;
    if (p >= SHOW_N) p = 0;
    return SHOW_ORDER[p];
}

int ctl_show_clamp(int v, int d)
{
    int p = ctl_show_pos(v) + (d > 0 ? 1 : -1);
    if (p < 0) p = 0;
    if (p > SHOW_N - 1) p = SHOW_N - 1;
    return SHOW_ORDER[p];
}

int ctl_show_sanitize(int v)
{
    v = (int)((unsigned)v % SHOW_VALUES);
    return v == 11 ? 1 : v;
}

const char *ctl_show_name(int v)
{
    return v == SHOW_CYCLE ? "Cycle" : v == SHOW_HOST ? "Pi VU" : v == SHOW_TEXT ? "Text" : LT_NAME[(v >= 0 && v < LT_MODES) ? v : 0];
}

ctl_host_t ctl_host_parse(const uint8_t *p, unsigned n)
{
    ctl_host_t h = {HC_NONE, 0, 0, 0};
    if (!p || !n) return h;
    switch (p[0]) {
    case 0x00: if (n == 1) h.cmd = HC_HELLO; break;
    case 0x01: if (n == 17) { h.cmd = HC_LEVELS; h.data = p + 1; h.len = 16; } break;
    case 0x02: if (n == 2 && p[1] < 2) { h.cmd = HC_LIGHTS; h.arg = p[1]; } break;
    case 0x03: if (n == 2 && p[1] < 128) { h.cmd = HC_SPEED; h.arg = p[1]; } break;
    case 0x04: if (n == 2 && p[1] < LT_PICK && p[1] != 11) { h.cmd = HC_EFFECT; h.arg = p[1]; } break;   /* 0-12 except the retired 11 */
    case 0x05: h.cmd = HC_TEXT; h.data = p + 1; h.len = n - 1; break;
    case 0x06: if (n == 1 || (n == 2 && p[1] < 2)) { h.cmd = HC_CRASH; h.arg = n == 2 ? p[1] : 0; } break;
    case 0x07: if (n == 1) h.cmd = HC_STATUS; break;
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

int ctl_idle_should_start(const ctl_idle_t *in)
{
    if (in->idle_s <= 0 || in->lt_on || in->menu_on || in->playing || in->ext_playing) return 0;
    if (in->held || in->editing || in->modifier) return 0;
    return (uint32_t)(in->now_ms - in->last_input_ms) > (uint32_t)in->idle_s * 1000u;
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

int ctl_cycle_due(int show, int lt_cur, int pinned, uint32_t now_ms, uint32_t since_ms)
{
    return show == SHOW_CYCLE && !pinned && lt_cur >= 0 && lt_cur < LT_FX && (uint32_t)(now_ms - since_ms) > CTL_CYCLE_MS;
}

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
