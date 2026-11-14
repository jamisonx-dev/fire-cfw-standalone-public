/* PAINT mode (v0.37): a very simple paint box on the pads, "for kids and art's sake" (the owner, 2026-11-12).
 *
 * Pick a colour (hold SHIFT: the pads show the palette as 16 blocks of 2 x 2; tap one), tap pads to paint them, tap a pad
 * that already has that colour to wipe it. Four paintings are kept in flash, one per MUTE row button: tap = load, hold
 * PAINT_HOLD_MS = save. SHIFT + STOP clears the canvas, SHIFT + BROWSER leaves (main.c).
 *
 * A painting is 64 pads x one colour number (0-15) = 4 bits a pad = PAINT_LEN bytes, the same bytes in RAM and in flash.
 * The colours are 0-127 per channel and go through the ORC-mode brightness curve (hostm.c hm_duty), so they look the way
 * ORC mode's do.
 *
 * Pure C: no hardware, so paint_test.c runs it on the Pi. */
#ifndef PAINT_H
#define PAINT_H
#include <stdint.h>
#include <stdbool.h>

#define PAINT_COLS 16                       /* the palette: 0 = black (the eraser), 1-15 colours */
#define PAINT_PADS 64
#define PAINT_LEN (PAINT_PADS / 2)          /* one painting: 2 pads a byte (pad 2k = low nibble, 2k+1 = high) */
#define PAINT_SLOTS 4                       /* saved paintings: one per MUTE row button */
#define PAINT_HOLD_MS 1000                  /* hold a row button this long = save */
#define PAINT_ERASER_RGB 24                 /* the eraser's block in the palette view: dim white, so it can be seen */

extern const uint8_t PAINT_RGB[PAINT_COLS][3];   /* 0-127 per channel */
extern const char *const PAINT_NAME[PAINT_COLS];

int paint_get(const uint8_t *cv, int pad);
void paint_set(uint8_t *cv, int pad, int c);
/* A tap on a pad: paint it `cur`, or wipe it (black) when it already is `cur`. Returns the pad's new colour. */
int paint_tap(uint8_t *cv, int pad, int cur);
/* The palette view: the colour a pad picks. 2 x 2 blocks, red top left, the eraser bottom right. */
int paint_swatch(int pad);
/* What a pad shows (0-127 per channel). The palette view: the picked colour full, the rest half; else the canvas. */
void paint_pad_rgb(const uint8_t *cv, int pad, int cur, bool palette, uint8_t rgb[3]);

/* The row buttons: tap = load, hold = save (one button at a time; a second pressed meanwhile is ignored). */
typedef struct { int8_t row; uint8_t done; uint32_t t; } paint_btn_t;
enum { PB_NONE, PB_LOAD, PB_SAVE };
void paint_btn_init(paint_btn_t *b);
int paint_btn(paint_btn_t *b, int row, bool down, uint32_t now, int *slot);   /* PB_LOAD on a short tap's release */
int paint_btn_tick(paint_btn_t *b, uint32_t now, int *slot);                  /* PB_SAVE once, when the hold is long enough */

#endif
