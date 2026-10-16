/* What the "oled" firmware shows. Pure drawing (host-testable). */
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "screen.h"

char screen_lines[SCREEN_TEXT_LINES][SCREEN_TEXT_COLS + 1];

static void fmt_u(char *out, unsigned v)
{
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *out++ = tmp[--n];
    *out = 0;
}

void screen_render(unsigned uptime_s, const char *last_midi)
{
    draw_clear();
    draw_rect(0, 0, FB_W - 1, FB_H - 1);
    draw_text(6, 4, "ORC-1", 2);                  /* 12x16 cells */
    draw_text(72, 4, "custom", 1);
    draw_text(72, 12, "firmware", 1);
    draw_hline(1, FB_W - 2, 22);

    char buf[24] = "up ";
    fmt_u(buf + 3, uptime_s);
    strcat(buf, "s");
    draw_text(4, 26, buf, 1);
    draw_text(4, 36, "midi:", 1);
    draw_text_n(36, 36, last_midi, 15, 1);

    /* Lines set by the host (SysEx F0 7D 01 <line> <text> F7) */
    for (int i = 0; i < SCREEN_TEXT_LINES; i++)
        draw_text_n(4, 46 + i * 8, screen_lines[i], SCREEN_TEXT_COLS, 1);
}
