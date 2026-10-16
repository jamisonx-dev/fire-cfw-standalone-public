#include <string.h>
#include "draw.h"
#include "font5x7.h"

uint8_t fb[FB_W * FB_H / 8];

void draw_clear(void) { memset(fb, 0, sizeof fb); }

void draw_pixel(int x, int y, int on)
{
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) return;
    uint8_t *b = &fb[(y >> 3) * FB_W + x];
    if (on) *b |= (uint8_t)(1u << (y & 7));
    else    *b &= (uint8_t)~(1u << (y & 7));
}

void draw_hline(int x0, int x1, int y) { for (int x = x0; x <= x1; x++) draw_pixel(x, y, 1); }
void draw_vline(int x, int y0, int y1) { for (int y = y0; y <= y1; y++) draw_pixel(x, y, 1); }

void draw_rect(int x0, int y0, int x1, int y1)
{
    draw_hline(x0, x1, y0); draw_hline(x0, x1, y1);
    draw_vline(x0, y0, y1); draw_vline(x1, y0, y1);
}

void draw_fill(int x0, int y0, int x1, int y1, int on)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) draw_pixel(x, y, on);
}

void draw_invert(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= FB_W || y < 0 || y >= FB_H) continue;
            fb[(y >> 3) * FB_W + x] ^= (uint8_t)(1u << (y & 7));
        }
}

static void draw_char(int x, int y, char c, int scale)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *g = font5x7[c - 32];
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 8; row++)
            if (g[col] >> row & 1)
                draw_fill(x + col * scale, y + row * scale, x + col * scale + scale - 1, y + row * scale + scale - 1, 1);
}

int draw_text_n(int x, int y, const char *s, int n, int scale)
{
    for (int i = 0; i < n && s[i]; i++) {
        draw_char(x, y, s[i], scale);
        x += 6 * scale;
    }
    return x;
}

int draw_text(int x, int y, const char *s, int scale) { return draw_text_n(x, y, s, 1 << 30, scale); }
