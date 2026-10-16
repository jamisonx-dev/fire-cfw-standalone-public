/* Pad light shows for the Fire's 4 x 16 RGB pads: the ORC's 12 "pretty lights" effects ported from
 * orc1_fire.py (same maths, same pace), plus two VU-bar modes driven by data instead of time.
 * Pure C, no hardware: also compiles on the Pi / PC for lights_test (checks it against the Python).
 *
 * Every effect is a pure function of (row, col, t), so one frame can be drawn a few columns at a
 * time (lights_cols) between sequencer work; a frame is only committed to the LEDs when complete.
 * Row 0 is the TOP row, as in the ORC. */
#ifndef LIGHTS_H
#define LIGHTS_H
#include <stdint.h>

#define LT_ROWS 4
#define LT_COLS 16
#define LT_FX 12                    /* the time-driven shows, ported from the ORC: modes 0-10 (11 was the ORC's fake VU: removed, kept as a hole) */
#define LT_M_HITS 12                /* VU bars that jump when the sequencer plays a track */
#define LT_M_HOST 13                /* VU bars driven by the host (the Pi) over MIDI */
#define LT_M_TEXT 14                /* a scrolling message in a 3x4 font (set with lights_set_text) */
#define LT_MODES 15
#define LT_PICK 13                  /* modes 0..12 can be picked; 13 in a setting = CYCLE (not HOST) */

extern const char *const LT_NAME[LT_MODES];

/* Pace of a mode (the numbers the ORC's Pretty Lights Lab settled on). */
float lights_scale(int mode);
/* The speed setting 0..127 as a factor: 2^((v-64)/32), x0.25 .. x4 (table + shifts, no powf). */
float lights_speed_factor(int v);
/* Fast sine (the chip's library sinf is slow software); exported so the host test can check it. */
float lights_fsin(float x);

/* Draw columns c0 .. c1-1 of mode at effect-time t into out[row * 16 + col] = {r, g, b} 0..255. */
void lights_cols(int mode, float t, int c0, int c1, uint8_t out[][3]);

/* HITS: track t (0-23) played at level lv (1 soft, 2 normal, 3 accent). */
void lights_hit(int track, int lv);
/* HOST: 16 column levels 0..127 from the host (each a bar target; the bars fall by themselves). */
void lights_host(const uint8_t lv[16]);
/* TEXT: the message that scrolls across the pads (printable ASCII, lower case shown as upper, up to
 * LT_TEXT_MAX characters; anything else is a space). Setting it restarts the scroll from the right edge. */
#define LT_TEXT_MAX 32
void lights_set_text(const char *s, int n);
const char *lights_text(void);                      /* the current message (for the screen) */
/* How the VU bars (TRACKS, PI VU) look and move (a setting; the owner picks by eye):
 *   0 snappy: a level goes straight to the bar and the bar falls in a steady line; the top pad is partly lit
 *   1 smooth: the bar FOLLOWS the level (60 ms up / 160 ms down), the peak dot holds 0.4 s; partly lit top pad
 *   2 whole:  like smooth, but only whole pads (with hysteresis), never a partly lit one
 *   3 heat:   snappy movement, drawn like a heat-sensitive sheet: a pad's colour is how DEEP inside the bar it is, so the
 *             base is hot (red) and the tip cools through green, blue and purple to black (colour = fine height)
 *   4 heat smooth: the same colours with smooth movement */
void lights_set_bar_style(int s);
void lights_heat_rgb(float warmth, uint8_t *rgb);
/* Heat flip (heat looks only): 0 = hot at the base, cooling to the tip. 1 = FLIPPED: cool at the base, hot at the tip: a
 * pad's warmth is the height its covered part reaches (the bottom pad blue .. the top pad red; the partly covered tip
 * pad slides along the scale as the bar rises). */
void lights_set_heat_flip(int on);   /* the heat-sheet colour for warmth 0..1 (0 = black) */
/* Fall the VU bars by dt seconds (once per frame). */
void lights_step(float dt);
void lights_clear_bars(void);

/* One colour channel 0..255 -> pad LED level 0..7. The pad driver is a blink duty (linear), the eye
 * is not, so the value is squared first. smooth: a fixed per-pad threshold (spatial dither), which gives
 * slow fades their in-between brightness across the grid. It never changes from frame to frame (a
 * pad flipping between two levels 30 times a second is a strobe), so `frame` is unused. */
int lights_level(int v, int row, int col, unsigned frame, int smooth);

#endif
