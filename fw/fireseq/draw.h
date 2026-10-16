/* Framebuffer drawing for the Fire's 128x64 SSD1306.
 * Pure C, no hardware: also compiles on the Pi for draw_test (renders the screen to a file). */
#ifndef DRAW_H
#define DRAW_H
#include <stdint.h>

#define FB_W 128
#define FB_H 64

/* SSD1306 horizontal mode layout: byte = page*128 + x, bit = y % 8. */
extern uint8_t fb[FB_W * FB_H / 8];

void draw_clear(void);
void draw_pixel(int x, int y, int on);
void draw_hline(int x0, int x1, int y);
void draw_vline(int x, int y0, int y1);
void draw_rect(int x0, int y0, int x1, int y1);
void draw_fill(int x0, int y0, int x1, int y1, int on);
void draw_invert(int x0, int y0, int x1, int y1);   /* flip every pixel in the box (a selected menu row) */
/* Text at pixel x,y (top-left), scale 1 = 6x8 cells. Returns x after the text. */
int draw_text(int x, int y, const char *s, int scale);
int draw_text_n(int x, int y, const char *s, int n, int scale);

#endif
