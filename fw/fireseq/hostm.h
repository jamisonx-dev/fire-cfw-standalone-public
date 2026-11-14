/* ORC mode = HOST mode (v0.33): the ORC owns the Fire's pads, buttons, knobs, SELECT and screen, while the Fire's own
 * sequencer keeps playing underneath. A toggle (BROWSER > ORC mode, or the host asks), never a replug.
 *
 * The ORC's Fire handler already draws for Akai's firmware, so the Fire takes those two messages as they are:
 *   in   F0 47 7F 43 65 <len> {pad, r, g, b}...  F7   pad colours (0-127 each; a curve -> a brightness, split over the
 *                                                     long and the short LED slot: about 200 steps, v0.34)
 *        F0 47 7F 43 0E <len> b0 b1 c0 c1 <bits> F7   screen bitmap (Akai's 7-bit packing, any window)
 * What the player does goes back as OUR SysEx, never as notes: the sequencer's own notes are on the same USB port, and
 * a pad sent as a note could not be told from them (the v0.15 host mode sent notes; it also stopped the sequencer).
 *   out  F0 7D 'ORC' 22 <note> <vel> F7     a pad (Akai's notes 54-117, 54 = top left) or a button (Akai's note)
 *                                            pressed (vel 100 pads / 127 buttons) or let go (vel 0)
 *        F0 7D 'ORC' 23 <k> <rel> F7        knob k 0-3 or SELECT (k = 4) turned: 1..63 up, 127..65 down
 *        F0 7D 'ORC' 21 <0|1> F7            ORC mode went off / on (also the answer to every 21 from the host)
 *   in   F0 7D 'ORC' 21 <0|1|2> F7          off / on / "still here" (the keep-alive: HM_QUIET_MS without one and the
 *                                            Fire goes back to its sequencer by itself, so it can never be stranded)
 *        F0 7D 'ORC' 24 <curve x 10> F7     the pad colour curve, 10-30 (10 = straight, 20 = squared); not saved
 * SHIFT + BROWSER on the Fire always leaves (HM_EXIT: the one combination the ORC never sees).
 * Pure C, no hardware: hostm_test.c checks it on the Pi. */
#ifndef HOSTM_H
#define HOSTM_H
#include <stdint.h>
#include <stdbool.h>

#define HM_OLED_DONE 1              /* hm_byte: a complete screen message just ended (flush the screen) */
#define HM_GAMMA_X10 20             /* the pad curve at power-on (2.0) */
#define HM_QUIET_MS  5000u          /* no keep-alive from the host for this long = leave ORC mode */
#define HM_BTN_SOFT  3              /* fine level of the resting buttons */

#define HM_W_DEFAULT 34             /* the short slot's share of a chain pass, in 1/256 (measured on the chip: t4_gap_us / 500) */

/* fb = the 1024-byte SSD1306 framebuffer, leds / fine = led_level[] / led_fine[] (levels 0-max_fine in each slot). */
void hm_init(uint8_t *fb, volatile uint8_t *leds, volatile uint8_t *fine, int max_fine);
void hm_reset(void);                /* forget a half-received message (entering ORC mode) */
int hm_byte(uint8_t b);             /* one incoming MIDI byte (not real-time); returns HM_* flags */
/* 0-127 colour -> the two slots' levels. The curve gives the brightness wanted (curve 20: (v / 127) squared); the long
   slot takes as many whole steps as fit, the short slot the rest in steps a tenth the size. Nothing is forced on: a
   brightness under half the smallest step is dark (the host decides what is worth showing). */
void hm_duty(uint8_t v, uint8_t *m, uint8_t *f);
void hm_set_weight(int w);          /* the short slot's share in 1/256, 12-64 (host 25): match it to the measured slot */
int hm_weight(void);
void hm_set_gamma(int x10);         /* 10-30; later colours use it (the host repaints) */
extern const uint8_t HM_INV[56];    /* (for hostm_test) */
void hm_buttons(void);              /* the resting button look: mode / transport buttons soft, ALT + the red ones bright */

/* A chain input event (index | 0x80 pressed, from fire_event) -> out = {note, vel}. Returns 1, or 0 for an input with
   no meaning to the ORC. */
int hm_input(int ev, uint8_t out[2]);
uint8_t hm_rel(int d);              /* detents -> one relative value (clamped to +-63) */

/* SHIFT + BROWSER: is this event the way out? (shift_held = SHIFT is down now) */
int hm_exit(int ev, int shift_held);
/* Has the host been quiet too long? (unsigned: right across the 49-day wrap) */
int hm_quiet(uint32_t now_ms, uint32_t seen_ms);

#endif
