/* v0.31r REMOTE INPUT: the host presses the Fire's buttons / pads and turns its knobs / SELECT (host command 20,
 * USER_GUIDE "Remote input"). For troubleshooting: a Pi script can play the Fire for hours the way hands do, so a
 * crash that only comes during hands-on use can be reproduced without the owner's hands.
 *
 * An injected input goes through EXACTLY the code a real one does: fire_event() hands out its press / release events
 * (real ones first), fire_held() reports it held, the knob / SELECT takes add its detents. Only the chain scan itself
 * (TIM4) is skipped: TIM4 is not touched by this at all. Everything here runs in the main loop (host commands are
 * parsed there), so it needs no interrupt locking.
 *
 * Safety: off at power-on. Nothing is accepted until the host arms it (20 00 01). Armed, it ends by itself when the
 * host goes quiet for RIN_QUIET_MS (no 20 command: a crashed or killed script), and on 20 00 00: either way every input
 * it holds is released first (release events are sent), so a SHIFT can never stay held by a dead script. */
#ifndef REMOTE_H
#define REMOTE_H
#include <stdint.h>

#define RIN_INPUTS   88            /* = IN_COUNT (fire_map.h): input indices 0-87 (69-71 do not exist) */
#define RIN_TURNERS  5             /* knobs 0-3, 4 = SELECT */
#define RIN_SELECT   4
#define RIN_QUEUE    16            /* events waiting (power of two). RAM is full (v0.31h left 4 B over the stack reserve):
                                      16 = a script can hold 7 inputs at once with nothing drained; the main loop drains
                                      it every pass, so a script never comes near that */
#define RIN_QUIET_MS 5000u

/* results, as answered in 20 <sub> <result> <armed> */
enum { RIN_OK = 0, RIN_NOT_ARMED = 1, RIN_BAD = 2, RIN_FULL = 3 };
/* sub-commands */
enum { RIN_ARM = 0, RIN_PRESS = 1, RIN_TURN = 2, RIN_RELEASE_ALL = 3 };

typedef struct {
    uint8_t armed;
    uint32_t last_ms;               /* the last 20 command (any) */
    uint8_t held[(RIN_INPUTS + 7) / 8];
    uint8_t q[RIN_QUEUE];
    uint8_t qw, qr;                 /* (RIN_QUEUE <= 256) */
    int16_t turn[RIN_TURNERS];      /* detents waiting per knob / SELECT */
} rin_t;

int rin_valid_input(int idx);       /* a real input index (0-68, 72-87) */
/* One host command 20 <sub> <data...>: sub + the bytes after it. now = ms_ticks. Returns RIN_*. */
int rin_cmd(rin_t *r, int sub, const uint8_t *d, unsigned n, uint32_t now);
/* Call every main-loop pass: the quiet timeout (releases everything + disarms). Returns 1 when it fired. */
int rin_tick(rin_t *r, uint32_t now);
int rin_event(rin_t *r);            /* the next injected event (index | 0x80 pressed), -1 = none */
int rin_held(const rin_t *r, int idx);
int rin_turn_take(rin_t *r, int k); /* detents waiting for knob k (4 = SELECT), and clears them */
int rin_turn_pending(const rin_t *r, int k);
#endif
