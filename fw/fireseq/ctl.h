/* Control decisions pulled out of main.c as PURE functions (no hardware, no globals) so the host can test them:
 * main.c is bare-metal and cannot be imported, and the 2026-10-13 review found every one of these rules wrong or
 * untested. ctl_test.c checks each; main.c just calls them.
 *   - the Show list (and what an old saved value means)
 *   - parsing a host-protocol SysEx (7D 'ORC' <cmd> ...)
 *   - when the pads may start a light show by themselves (idle)
 *   - the light-frame timer (unsigned: it must survive a long freeze and the 71-minute us wrap)
 *   - the PI VU show's behaviour when the Pi stops sending, a pinned effect vs CYCLE, host "stop"
 *   - the lazy save of settings changed while stopped */
#ifndef CTL_H
#define CTL_H
#include <stdint.h>

/* ---- the Show setting (G.lt_mode) ---- */
#define SHOW_CYCLE 13                /* stays 13: saved settings already say so */
#define SHOW_HOST 14                 /* the Pi's audio VU */
#define SHOW_TEXT 15                 /* the scrolling message */
#define SHOW_VALUES 16               /* stored values run 0-15 (11 is retired: the ORC's fake VU) */
#define SHOW_N 15                    /* how many shows the list holds */
extern const uint8_t SHOW_ORDER[SHOW_N];
int ctl_show_pos(int v);             /* where v sits in SHOW_ORDER (0 for anything not in it) */
int ctl_show_step(int v, int d);     /* next (d > 0) / previous show, wrapping (SELECT while lights run) */
int ctl_show_clamp(int v, int d);    /* the same without wrapping (the menu) */
int ctl_show_sanitize(int v);        /* a stored value -> a valid one: mod 16, and the retired 11 -> SPARKLE (1) */
const char *ctl_show_name(int v);    /* "SPARKLE", "PI VU", "TEXT", "CYCLE" ... */

/* ---- host protocol (the Pi -> the Fire): F0 7D 'O' 'R' 'C' <cmd> <data...> F7 ---- */
enum { HC_NONE = -1, HC_HELLO = 0, HC_LEVELS = 1, HC_LIGHTS = 2, HC_SPEED = 3, HC_EFFECT = 4, HC_TEXT = 5, HC_CRASH = 6, HC_STATUS = 7, HC_STACK = 8, HC_PEEK = 9, HC_COLORS = 10, HC_SURFACE = 11 };
typedef struct { int cmd; int arg; const uint8_t *data; unsigned len; } ctl_host_t;
/* p/n = the payload after 7D 'ORC' (cmd + data). cmd = HC_NONE when the length or a value is wrong. */
ctl_host_t ctl_host_parse(const uint8_t *p, unsigned n);
/* 09 <address: 5 x 7 bits, low first> = read one 32-bit word (debugging, read only). Allowed: our flash, RAM, the
 * peripherals and the core's system registers, word-aligned. 1 = allowed (a bad address would bus-fault the Fire). */
int ctl_peek_ok(uint32_t addr);
/* SURFACE (v0.19), the ORC drawing its own controls on the pads:
 *   0A <first pad 0-63> <count 1-10> <r g b> x count   pad colours, each 0-7 (data = the triples, arg = first, len = count)
 *   0B 00 / 0B 01 <channel 1-16>                        surface mode off / on (arg = 0 or the channel) */
#define CTL_SURF_MAX 10
/* A whole SysEx as collected (F0 ... F7, `len` bytes, buffer of `cap`): 1 + the payload when it is a complete ORC message
 * that fits the buffer, 0 otherwise (a message that filled the buffer was cut short and is dropped, never half-used). */
int ctl_sysex_payload(const uint8_t *sx, unsigned len, unsigned cap, const uint8_t **p, unsigned *n);

/* ---- idle lights ---- */
typedef struct {
    int idle_s;                      /* the setting, seconds (0 = off) */
    int lt_on, menu_on, playing, ext_playing;
    int held;                        /* a step / row / PATTERN button / live pad is held down */
    int editing;                     /* the song editor, the pattern picker or REC armed */
    int modifier;                    /* SHIFT or ALT held */
    uint32_t now_ms, last_input_ms;
} ctl_idle_t;
int ctl_idle_should_start(const ctl_idle_t *in);

/* ---- the light-frame timer (microseconds, unsigned: right across the 71-minute wrap and after a long freeze) ---- */
#define CTL_FRAME_US 33000u
int ctl_frame_due(uint32_t now_us, uint32_t last_us);

/* ---- the Pi's VU show ---- */
#define CTL_HOST_QUIET_MS 3000u
enum { HT_NONE = 0, HT_FALLBACK = 1, HT_OFF = 2 };
/* The host has stopped sending levels: when the show was picked as PI VU, fall back to another show and wait for it;
 * a host-started show (not the setting) ends. */
int ctl_host_timeout(int lt_on, int lt_host, int show_is_host, uint32_t now_ms, uint32_t host_ms);
/* A levels frame arrived: should the PI VU bars take the pads back (the show IS PI VU, lights are on, currently a fallback)? */
int ctl_host_reengage(int lt_on, int lt_host, int show_is_host);
/* Host "lights off" (02 00): does it end the running show? (host-driven bars, or any show the host started) */
int ctl_host_stop_ends(int lt_on, int lt_host, int by_host);

/* ---- CYCLE ---- */
#define CTL_CYCLE_MS 20000u
int ctl_cycle_due(int show, int lt_cur, int pinned, uint32_t now_ms, uint32_t since_ms);   /* lt_cur is a time-driven mode < 12 */

/* ---- settings changed while stopped (tempo, swing, ...): saved this long after the LAST such change (and at STOP) ---- */
#define CTL_LAZY_SAVE_MS 20000u
uint32_t ctl_lazy_arm(uint32_t now_ms);                     /* the new due time (never 0) */
int ctl_lazy_due(uint32_t lazy_due, uint32_t now_ms);       /* 1 once it is time (0 = not armed) */

#endif
