/* DAW mode's protocol translator: Mackie Control + HUI (see daw.h). Pure C, no hardware: daw_test.c runs it on the Pi.
 * Every byte value here is from the DAW-mode research notes (kept outside this repo); the tags there say how well each is sourced. */
#include <string.h>
#include "daw.h"

daw_state_t daw_st;
#define S daw_st

#define ZP(z, p) ((uint8_t)((z) << 3 | (p)))
#define NO 0xFF
/* Non-strip buttons, in daw.h's id order from DB_BANK_L. MCU: research 1.4. HUI: research 2.1 (zone, port). */
const uint8_t DAW_MCU_NOTE[DB_COUNT - DB_GLOBAL] = {
    46, 47, 48, 49,                         /* bank left / right, channel left / right */
    91, 92, 93, 94, 95,                     /* rewind, fast fwd, stop, play, record */
    96, 97, 98, 99, 100, 101, 86,           /* up, down, left, right, zoom, scrub, cycle (loop) */
    70, 71, 72, 73, 80, 81,                 /* shift, option, control, alt, save, undo */
    74, 75, 76, 77, 78,                     /* automation read, write, trim, touch, latch */
    89, 50, 52, 53, 84,                     /* click, flip, name/value, SMPTE/beats, markers */
    54, 55, 56, 57, 58, 59, 60, 61,         /* F1-F8 */
    113, 114, 115,                          /* SMPTE, BEATS, rude solo lights */
    40, 41, 42, 43, 44,                     /* assign: track, send, pan, plug-in, EQ */
};
const uint8_t DAW_HUI_ZP[DB_COUNT - DB_GLOBAL] = {
    ZP(0x0A, 1), ZP(0x0A, 3), ZP(0x0A, 0), ZP(0x0A, 2),
    ZP(0x0E, 1), ZP(0x0E, 2), ZP(0x0E, 3), ZP(0x0E, 4), ZP(0x0E, 5),
    ZP(0x0D, 4), ZP(0x0D, 0), ZP(0x0D, 1), ZP(0x0D, 3), NO, ZP(0x0D, 5), ZP(0x0F, 3),
    ZP(0x08, 1), ZP(0x08, 5), ZP(0x08, 0), ZP(0x08, 4), ZP(0x08, 7), ZP(0x08, 3),
    ZP(0x18, 2), ZP(0x18, 4), ZP(0x18, 0), ZP(0x18, 5), ZP(0x18, 1),
    ZP(0x1D, 2), NO, NO, NO, NO,
    ZP(0x1B, 0), ZP(0x1B, 1), ZP(0x1B, 2), ZP(0x1B, 3), ZP(0x1B, 4), ZP(0x1B, 5), ZP(0x1B, 6), ZP(0x1B, 7),
    ZP(0x16, 0), ZP(0x16, 2), ZP(0x16, 3),
    NO, ZP(0x0B, 7), ZP(0x0B, 2), NO, NO,   /* send = send A, pan = pan; HUI has no track / plug-in / EQ assign */
};
/* HUI strip zone ports, research 2.1: rec 7, solo 3, mute 2, select 1, v-sel 5, auto 4, insert 6 (0 = fader touch) */
const uint8_t DAW_HUI_STRIP_PORT[7] = {7, 3, 2, 1, 5, 4, 6};
#define HUI_TOUCH_PORT 0

/* ---------------- state ---------------- */

void daw_init(int mode)
{
    memset(&S, 0, sizeof S);
    memset(S.text, ' ', sizeof S.text);
    memset(S.digit, ' ', sizeof S.digit);
    S.mode = (uint8_t)(mode == DAW_MCU || mode == DAW_HUI ? mode : DAW_AUTO);
    S.proto = S.mode;
    S.zone = S.hi_strip = 0xFF;
}

int daw_proto(void) { return S.proto; }
const char *daw_text(void) { return S.text; }
uint8_t daw_ring(int pot) { return pot >= 0 && pot < 8 ? S.ring[pot] : 0; }

int daw_led(int id)
{
    if (id < 0 || id >= DB_COUNT) return 0;
    uint8_t m = (uint8_t)(1u << (id & 7));
    return S.led_on[id >> 3] & m ? 2 : S.led_blink[id >> 3] & m ? 1 : 0;
}

static int set_led(int id, int state, daw_ev_t *ev)
{
    uint8_t m = (uint8_t)(1u << (id & 7));
    S.led_on[id >> 3] = (uint8_t)(state == 2 ? S.led_on[id >> 3] | m : S.led_on[id >> 3] & ~m);
    S.led_blink[id >> 3] = (uint8_t)(state == 1 ? S.led_blink[id >> 3] | m : S.led_blink[id >> 3] & ~m);
    ev->type = DE_LED; ev->idx = (uint8_t)id; ev->val = (uint16_t)state;
    return 1;
}

/* Lock Auto to what the host speaks; a forced mode only accepts its own. Returns 1 if p is (now) the protocol. */
static int speak(int p, daw_ev_t *ev, int *locked)
{
    *locked = 0;
    if (S.proto == p) return 1;
    if (S.mode != DAW_AUTO) return 0;               /* a forced protocol never changes */
    if (S.proto != DAW_AUTO) {                      /* Auto, re-locking to the other one: forget what the old one said */
        memset(S.led_on, 0, sizeof S.led_on); memset(S.led_blink, 0, sizeof S.led_blink);
        memset(S.ring, 0, sizeof S.ring); memset(S.text, ' ', sizeof S.text); memset(S.digit, ' ', sizeof S.digit);
        S.zone = S.hi_strip = 0xFF; S.touched = 0;
    }
    S.proto = (uint8_t)p;
    S.pings = 0;
    *locked = 1;
    if (ev) { ev->type = DE_LOCK; ev->idx = (uint8_t)p; ev->val = 0; }
    return 1;
}

static void out3(uint8_t a, uint8_t b, uint8_t c) { uint8_t m[3] = {a, b, c}; daw_out(m, 3); }

/* ---------------- in: SysEx ---------------- */

static void text_put(int at, uint8_t c)
{
    if (at < 0 || at >= DAW_TEXT) return;
    S.text[at] = (char)(c >= 0x20 && c <= 0x7E ? c : ' ');
    if (S.tlo > at) S.tlo = (uint8_t)at;
    if (S.thi < at + 1) S.thi = (uint8_t)(at + 1);
}

static void mcu_reply(uint8_t cmd, const char *data, int n, const uint8_t *extra, int ne)
{
    uint8_t m[6 + 7 + 4 + 1];
    static const uint8_t H[] = {0xF0, 0x00, 0x00, 0x66};
    memcpy(m, H, 4);
    m[4] = S.dev ? S.dev : 0x14; m[5] = cmd;
    int k = 6;
    for (int i = 0; i < n; i++) m[k++] = (uint8_t)data[i];
    for (int i = 0; i < ne; i++) m[k++] = extra[i];
    m[k++] = 0xF7;
    daw_out(m, k);
}

static const uint8_t CHALLENGE[4] = {0x01, 0x02, 0x03, 0x04};

static void sx_byte(uint8_t b, daw_ev_t *ev, int *got)
{
    uint8_t p = S.sx_pos;
    if (S.sx_pos < 255) S.sx_pos++;
    if (S.sx == 2) return;
    if (p >= 1 && p <= 3) {                         /* 00 00 66 = Mackie */
        static const uint8_t M[3] = {0x00, 0x00, 0x66};
        if (b != M[p - 1]) S.sx = 2;
        return;
    }
    if (p == 4) {                                   /* device id: 14 MCU, 10 Logic Control, 05 HUI */
        int lk, want = b == 0x14 || b == 0x10 ? DAW_MCU : b == 0x05 ? DAW_HUI : 0;
        if (!want || !speak(want, ev, &lk)) { S.sx = 2; return; }
        if (want == DAW_MCU) S.dev = b;
        if (lk) *got = 1;
        return;
    }
    if (S.proto == DAW_MCU) {
        if (p == 5) { S.cmd = b; return; }
        if (S.cmd == 0x12) {                        /* LCD: 12 <offset> <ASCII ...> */
            if (p == 6) { S.tpos = b; return; }
            if (S.tpos < DAW_MCU_LCD) text_put(S.tpos++, b);    /* (past the end: dropped, the rest still shows) */
        }
        return;
    }
    /* HUI: F0 00 00 66 05 00 <cmd> ... */
    if (p == 5) { if (b != 0x00) S.sx = 2; return; }
    if (p == 6) { S.cmd = b; return; }
    if (S.cmd == 0x10) {                            /* scribble strip: 10 <ch 0-8> <4 chars> */
        if (p == 7) { if (b > 8) S.sx = 2; else S.tpos = (uint8_t)(DAW_HUI_STRIP + b * 4); return; }
        if (p <= 11) text_put(S.tpos++, b);
        else S.sx = 2;
    } else if (S.cmd == 0x11) {                     /* timecode: 11 <8 digits, frames units first> */
        if (p > 14) { S.sx = 2; return; }
        int k = p - 7;
        S.digit[k] = (uint8_t)(((b & 0x0F) < 10 ? '0' + (b & 0x0F) : ' ') | (b & 0x10 ? 0x80 : 0));
        if (S.tlo > k) S.tlo = (uint8_t)k;          /* (digits reuse the text range while this SysEx lasts) */
        if (S.thi < k + 1) S.thi = (uint8_t)(k + 1);
    } else if (S.cmd == 0x12) {                     /* main display: 12 { <zone 0-7> <10 chars> } ... */
        if (p == 255) { S.sx = 2; return; }
        int k = (p - 7) % 11;
        if (k == 0) {
            if (b > 7) { S.sx = 2; return; }
            S.tpos = (uint8_t)((b / 4) * 40 + (b % 4) * 10);
        } else text_put(S.tpos++, b);
    } else S.sx = 2;                                /* anything else from the host: ignored */
}

static int sx_end(daw_ev_t *ev)
{
    int was = S.sx;
    S.sx = 0;
    if (was != 1 || S.sx_pos < 6) return 0;
    if (S.proto == DAW_MCU) {
        switch (S.cmd) {
        case 0x00: mcu_reply(0x01, DAW_SERIAL, 7, CHALLENGE, 4); break;     /* device query -> connection query */
        case 0x02: mcu_reply(0x03, DAW_SERIAL, 7, 0, 0); break;             /* host's reply -> confirmation */
        case 0x13: mcu_reply(0x14, DAW_VERSION, 5, 0, 0); break;            /* version request -> version */
        case 0x12: goto text;
        default: return 0;
        }
        ev->type = DE_HELLO; ev->idx = S.cmd; ev->val = 0;
        return 1;
    }
    if (S.cmd == 0x11) {
        if (S.thi <= S.tlo) return 0;
        ev->type = DE_DIGIT; ev->idx = S.tlo; ev->val = (uint16_t)(S.thi - S.tlo);
        return 1;
    }
text:
    if (S.thi <= S.tlo) return 0;
    ev->type = DE_TEXT; ev->idx = S.tlo; ev->val = S.thi;
    return 1;
}

/* ---------------- in: channel messages ---------------- */

static int mcu_note_id(int n)
{
    if (n < 40) return n;
    for (int i = 0; i < DB_COUNT - DB_GLOBAL; i++) if (DAW_MCU_NOTE[i] == n) return DB_GLOBAL + i;
    return -1;
}

static int hui_id(int zone, int port)
{
    if (zone < 8) {
        for (int g = 0; g < 7; g++) if (DAW_HUI_STRIP_PORT[g] == port) return g * 8 + zone;
        return -1;                                  /* port 0 = fader touch: no light */
    }
    uint8_t zp = ZP(zone, port);
    for (int i = 0; i < DB_COUNT - DB_GLOBAL; i++) if (DAW_HUI_ZP[i] == zp) return DB_GLOBAL + i;
    return -1;
}

static int level_ev(int strip, int fine, daw_ev_t *ev)
{
    ev->type = DE_LEVEL; ev->idx = (uint8_t)strip; ev->val = (uint16_t)fine;
    return 1;
}

static int mcu_msg(uint8_t st, uint8_t a, uint8_t v, daw_ev_t *ev)
{
    int ch = st & 0x0F, kind = st & 0xF0;
    if (kind == 0xE0) return ch < DAW_STRIPS ? level_ev(ch, v << 7 | a, ev) : 0;
    if (kind == 0x90 || kind == 0x80) {
        if (ch) return 0;
        int id = mcu_note_id(a);
        if (id < 0) return 0;
        return set_led(id, kind == 0x80 || v == 0 ? 0 : v == 1 ? 1 : 2, ev);
    }
    if (kind == 0xB0) {
        if (ch == 0 && a >= 48 && a <= 55) {        /* V-pot ring */
            S.ring[a - 48] = v;
            ev->type = DE_RING; ev->idx = (uint8_t)(a - 48); ev->val = v;
            return 1;
        }
        if ((ch == 0 || ch == 15) && a >= 64 && a <= 75) {    /* 7-segment: 64 = rightmost */
            int c = v & 0x3F;
            S.digit[a - 64] = (uint8_t)((c < 0x20 ? c + 0x40 : c) | (v & 0x40 ? 0x80 : 0));
            ev->type = DE_DIGIT; ev->idx = (uint8_t)(a - 64); ev->val = 1;
            return 1;
        }
    }
    return 0;                                       /* meters (D0) and the rest: not shown */
}

static int hui_msg(uint8_t st, uint8_t a, uint8_t v, daw_ev_t *ev)
{
    if (st != 0xB0) return 0;
    if (a <= 0x07) { S.hi_strip = a; S.hi_val = v; return level_ev(a, v << 7, ev); }
    if (a >= 0x20 && a <= 0x27) {
        if (S.hi_strip != a - 0x20) return 0;       /* a bottom half with no top half: wait for a whole one */
        return level_ev(a - 0x20, S.hi_val << 7 | v, ev);
    }
    if (a >= 0x10 && a <= 0x17) {
        S.ring[a - 0x10] = v;
        ev->type = DE_RING; ev->idx = (uint8_t)(a - 0x10); ev->val = v;
        return 1;
    }
    if (a == 0x0C) { S.zone = v; return 0; }
    if (a == 0x2C) {
        if (S.zone > 0x1D) return 0;
        int id = hui_id(S.zone, v & 7);
        return id < 0 ? 0 : set_led(id, v & 0x40 ? 2 : 0, ev);
    }
    return 0;
}

static const uint8_t NDATA[8] = {2, 2, 2, 2, 1, 1, 2, 0};   /* 8x-Ex: data bytes per message */

/* v0.36b: which protocol a host -> surface channel message belongs to, if only one sends it (0 = can't tell).
   MCU: pitch bend = a fader; any note (90 00 00 is handled by the caller); CC 30-37 rings, 40-4B time display.
   HUI: CC 0C / 2C = a light's zone / port, 00-07 / 20-27 = a fader's two halves, 10-17 = a ring. */
static int auto_guess(uint8_t status, uint8_t a)
{
    uint8_t hi = status & 0xF0;
    if (hi == 0xE0 || hi == 0x90) return DAW_MCU;
    if (hi != 0xB0) return 0;
    if ((a >= 0x30 && a <= 0x37) || (a >= 0x40 && a <= 0x4B)) return DAW_MCU;
    if (a == 0x0C || a == 0x2C || a <= 0x07 || (a >= 0x20 && a <= 0x27) || (a >= 0x10 && a <= 0x17)) return DAW_HUI;
    return 0;
}

int daw_byte(uint8_t b, daw_ev_t *ev)
{
    int got = 0, lk;
    ev->type = DE_NONE;
    if (b >= 0xF8) return 0;                        /* real-time: never interrupts anything */
    if (b == 0xF0) { S.sx = 1; S.sx_pos = 1; S.cmd = 0xFF; S.tlo = 0xFF; S.thi = 0; S.status = 0; return 0; }
    if (b == 0xF7) return S.sx ? sx_end(ev) : 0;
    if (b & 0x80) {                                 /* a status byte: ends any SysEx (unfinished = dropped) */
        S.sx = 0;
        S.status = b < 0xF0 ? b : 0;
        S.nd = 0;
        return 0;
    }
    if (S.sx) { sx_byte(b, ev, &got); return got; }
    if (!S.status) return 0;
    if (S.nd == 0) S.d1 = b;
    if (++S.nd < NDATA[(S.status >> 4) - 8]) return 0;
    S.nd = 0;                                       /* complete; the next data byte starts another (running status) */
    uint8_t a = S.d1, v = b;
    if (NDATA[(S.status >> 4) - 8] == 1) { a = b; v = 0; }
    if (S.status == 0x90 && a == 0 && v == 0 && S.proto != DAW_MCU) {      /* HUI ping (or, unlocked, MCU's REC 1 off) */
        if (S.proto == DAW_AUTO && ++S.pings < 2) return 0;   /* one alone proves nothing: wait for the next */
        if (!speak(DAW_HUI, 0, &lk)) return 0;
        out3(0x90, 0x00, 0x7F);
        ev->type = DE_PING; ev->idx = 0; ev->val = 0;
        return 1;
    }
    S.pings = 0;                                    /* v0.38b: anything else between two 90 00 00 and they are not "in a row" */
    if (S.proto == DAW_AUTO) {                      /* v0.36b: lock on any message only one protocol sends */
        int want = auto_guess(S.status, a);
        if (!want) return 0;
        speak(want, 0, &lk);
    }
    if (S.proto == DAW_MCU) return mcu_msg(S.status, a, v, ev);
    if (S.proto == DAW_HUI) return hui_msg(S.status, a, v, ev);
    return 0;
}

/* ---------------- out ---------------- */

int daw_announce(void)
{
    if (S.proto != DAW_MCU) return 0;
    mcu_reply(0x01, DAW_SERIAL, 7, CHALLENGE, 4);
    return 1;
}

static void hui_port(uint8_t zone, uint8_t port, int on)
{
    out3(0xB0, 0x0F, zone);
    out3(0xB0, 0x2F, (uint8_t)((on ? 0x40 : 0x00) | port));
}

int daw_button(int id, int down)
{
    if (id < 0 || id >= DB_COUNT) return 0;
    if (S.proto == DAW_MCU) {
        int n = id < DB_GLOBAL ? (id < DB_AUTO ? id : NO) : DAW_MCU_NOTE[id - DB_GLOBAL];
        if (n == NO || (id >= DB_LED_SMPTE && id <= DB_LED_RUDE)) return 0;
        out3(0x90, (uint8_t)n, down ? 0x7F : 0x00);
        return 1;
    }
    if (S.proto == DAW_HUI) {
        int zp = id < DB_GLOBAL ? ZP(id & 7, DAW_HUI_STRIP_PORT[id >> 3]) : DAW_HUI_ZP[id - DB_GLOBAL];
        if (zp == NO || (id >= DB_LED_SMPTE && id <= DB_LED_RUDE)) return 0;
        hui_port((uint8_t)(zp >> 3), (uint8_t)(zp & 7), down);
        return 1;
    }
    return 0;
}

static void touch(int s, int on)
{
    if (S.proto == DAW_MCU) out3(0x90, (uint8_t)(104 + s), on ? 0x7F : 0x00);
    else hui_port((uint8_t)s, HUI_TOUCH_PORT, on);
    if (on) S.touched |= (uint16_t)(1u << s);
    else S.touched &= (uint16_t)~(1u << s);
}

int daw_fader(int strip, int fine, uint32_t now_ms)
{
    if (S.proto == DAW_AUTO || strip < 0 || strip >= DAW_STRIPS) return 0;
    if (S.proto == DAW_HUI && strip >= 8) return 0;    /* HUI has no master fader */
    fine = fine < 0 ? 0 : fine > 16383 ? 16383 : fine;
    if (!(S.touched >> strip & 1)) touch(strip, 1);
    S.touch_ms[strip] = (uint16_t)now_ms;
    if (S.proto == DAW_MCU) out3((uint8_t)(0xE0 | strip), (uint8_t)(fine & 0x7F), (uint8_t)(fine >> 7));
    else { out3(0xB0, (uint8_t)strip, (uint8_t)(fine >> 7)); out3(0xB0, (uint8_t)(0x20 + strip), (uint8_t)(fine & 0x7F)); }
    return 1;
}

void daw_tick(uint32_t now_ms)
{
    for (int s = 0; s < DAW_STRIPS; s++)
        if ((S.touched >> s & 1) && (uint16_t)((uint16_t)now_ms - S.touch_ms[s]) >= DAW_TOUCH_MS) touch(s, 0);
}

void daw_release_all(void)
{
    for (int s = 0; s < DAW_STRIPS; s++) if (S.touched >> s & 1) touch(s, 0);
}

/* Relative values. MCU (research 1.3): clockwise = n, counter = 0x40 | n. HUI (2.3, HUI-Test): the other way round,
   clockwise = 0x40 + n, counter = n. */
static uint8_t rel(int d)
{
    int n = d < 0 ? -d : d;
    if (n > 63) n = 63;
    int cw = d > 0;
    if (S.proto == DAW_HUI) cw = !cw;
    return (uint8_t)(cw ? n : 0x40 | n);
}

int daw_vpot(int pot, int detents)
{
    if (S.proto == DAW_AUTO || pot < 0 || pot > 7 || !detents) return 0;
    out3(0xB0, (uint8_t)((S.proto == DAW_MCU ? 16 : 0x40) + pot), rel(detents));
    return 1;
}

int daw_jog(int detents)
{
    if (S.proto == DAW_AUTO || !detents) return 0;
    out3(0xB0, S.proto == DAW_MCU ? 60 : 0x0D, rel(detents));
    return 1;
}

static int press(int id)
{
    if (S.proto == DAW_AUTO) return 0;
    daw_release_all();                              /* the strips are about to be other tracks */
    return daw_button(id, 1) && daw_button(id, 0);
}
int daw_bank(int dir) { return dir ? press(dir < 0 ? DB_BANK_L : DB_BANK_R) : 0; }
int daw_channel(int dir) { return dir ? press(dir < 0 ? DB_CH_L : DB_CH_R) : 0; }
