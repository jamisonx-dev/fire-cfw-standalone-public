#include <string.h>
#include "draw.h"
#include "screens.h"

static char *put_int(char *o, int v)
{
    char t[12]; int n = 0;
    if (v < 0) { *o++ = '-'; v = -v; }
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *o++ = t[--n];
    *o = 0;
    return o;
}
static char *put_str(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static void play_icon(int x, int y) { for (int i = 0; i < 4; i++) draw_vline(x + i, y + i, y + 6 - i); }
static void stop_icon(int x, int y) { draw_fill(x, y + 1, x + 4, y + 5, 1); }
static int row_y(int r) { return SCR_BODY_Y + r * SCR_ROW_H; }

void scr_header(const scr_head_t *h)
{
    char b[24];
    int lw = (int)strlen(h->left) * 6;
    draw_text(0, 0, h->left, 1);
    put_int(b, h->bpm);
    draw_text(lw <= 78 ? 84 : 104, 0, b, 1);                   /* the tempo goes where it fits */
    if (h->star) draw_text(122, 0, "*", 1);

    if (h->playing) play_icon(0, 10); else stop_icon(0, 10);
    int x = 10;
    if (h->ctx && *h->ctx) { draw_text(x, 10, h->ctx, 1); x += (int)strlen(h->ctx) * 6 + 6; }
    int right = 128;                                            /* right-hand tags, right to left */
    if (h->clk) { right -= 24; draw_text(right, 10, h->clk == 1 ? "out" : "ext", 1); right -= 6; }
    if (h->swing != 50) { right -= 24; b[0] = 's'; b[1] = 'w'; put_int(b + 2, h->swing); draw_text(right, 10, b, 1); right -= 6; }
    int room = (right - x) / 6;
    if (h->status && *h->status && room > 0) draw_text_n(x, 10, h->status, room, 1);
    draw_hline(0, 127, 19);
}

/* one step cell, 5 px wide at x, in the row whose top is y */
static void cell(int x, int y, int lv, int beat, int muted)
{
    if (lv == 0) {
        if (beat) draw_fill(x + 2, y + 3, x + 3, y + 4, 1); else draw_pixel(x + 2, y + 3, 1);
    } else if (muted) {                                         /* a muted track's steps are outlines */
        if (lv == 1) draw_rect(x + 1, y + 2, x + 3, y + 4);
        else if (lv == 2) draw_rect(x, y + 1, x + 4, y + 5);
        else draw_rect(x, y, x + 4, y + 6);
    } else if (lv == 1) draw_fill(x + 1, y + 2, x + 3, y + 4, 1);
    else if (lv == 2) draw_fill(x, y + 1, x + 4, y + 5, 1);
    else draw_fill(x, y, x + 4, y + 6, 1);
}

#define STEP_X0 20
#define STEP_W 6

void scr_tracks(const char *const name[4], const uint8_t lv[4][16], const uint8_t muted[4], const int8_t head[4])
{
    for (int r = 0; r < 4; r++) {
        int y = row_y(r);
        draw_text(0, y, name[r], 1);
        for (int c = 0; c < 16; c++)
            if (lv[r][c] != 255) cell(STEP_X0 + c * STEP_W, y, lv[r][c], c % 4 == 0, muted[r]);
        if (muted[r]) draw_text(120, y, "M", 1);
        if (head[r] >= 0 && head[r] < 16) {                     /* this track's playhead: a lane round its column */
            draw_vline(STEP_X0 + head[r] * STEP_W - 1, y - 1, y + 8);
            draw_vline(STEP_X0 + head[r] * STEP_W + 5, y - 1, y + 8);
        }
    }
}

static void bar(int x0, int x1, int y, int num, int den)
{
    draw_rect(x0, y, x1, y + 7);
    int w = (x1 - x0 - 2) * (num < 0 ? 0 : num > den ? den : num) / den;
    if (w > 0) draw_fill(x0 + 1, y + 1, x0 + w, y + 6, 1);
}

void scr_faders(const scr_fader_t f[4])
{
    char b[24], *o;
    for (int r = 0; r < 4; r++) {
        int y = row_y(r);
        if (f[r].used == 1) {
            o = put_str(b, "F"); o = put_int(o, f[r].num); o = put_str(o, " CC"); put_int(o, f[r].cc);
            draw_text(0, y, b, 1);
            bar(60, 108, y, f[r].val, 127);
            put_int(b, f[r].val);
            draw_text(128 - (int)strlen(b) * 6, y, b, 1);
        } else if (f[r].used == 0) {
            draw_text(0, y, "+ row button: add", 1);
        }
    }
}

void scr_step_bars(const scr_step_t *s)
{
    char b[24];
    int y = row_y(0);
    draw_text(0, y, "1A ratchet", 1);
    for (int i = 0; i < 4; i++) {
        if (i < s->ratchet) draw_fill(66 + i * 9, y, 72 + i * 9, y + 6, 1);
        else draw_rect(66 + i * 9, y, 72 + i * 9, y + 6);
    }
    y = row_y(1);
    draw_text(0, y, "1B chance%", 1);
    bar(64, 104, y, s->chance, 100);
    put_int(b, s->chance); draw_text(106, y, b, 1);
    if (s->mel) { y = row_y(2); draw_text(0, y, "1C note", 1); draw_text(66, y, s->note, 1); }
    y = row_y(3);
    if (s->hold == 1) { draw_text(0, y, "1D gate", 1); draw_text(66, y, "tie", 1); }
    else if (s->hold == 2) { draw_text(0, y, "1D gate", 1); draw_text(66, y, "slide", 1); }
    else { draw_text(0, y, "1D gate%", 1); bar(64, 104, y, s->gate, 100); put_int(b, s->gate); draw_text(106, y, b, 1); }
}

void scr_popup(const char *title, int value, const char *unit, int v, int lo, int hi, bool star)
{
    char b[12];
    draw_text(0, 0, title, 1);
    if (star) draw_text(122, 0, "*", 1);
    put_int(b, value);
    draw_text(8, 14, b, 4);
    draw_text(8 + (int)strlen(b) * 24 + 4, 34, unit, 1);
    bar(6, 121, 52, v - lo, hi - lo > 0 ? hi - lo : 1);
}

#define MENU_ROWS 6
void scr_menu(const char *title, const char *note, const scr_menu_row_t *rows, int n, int sel, bool edit, bool star)
{
    char b[24];
    draw_text(0, 0, title, 1);
    if (note && *note) draw_text(120 - (int)strlen(note) * 6, 0, note, 1);
    if (star) draw_text(122, 0, "*", 1);
    draw_hline(0, 127, 8);
    int top = sel - 2;
    if (top > n - MENU_ROWS) top = n - MENU_ROWS;
    if (top < 0) top = 0;
    int right = n > MENU_ROWS ? 121 : 125;                      /* the rows end here (a scroll bar takes the edge) */
    for (int r = 0; r < MENU_ROWS && top + r < n; r++) {
        int i = top + r, y = 10 + r * 9;
        const char *v = rows[i].value;
        if (i == sel && edit) { char *o = put_str(b, "<"); o = put_str(o, v); put_str(o, ">"); v = b; }
        int vx = right - 2 - (int)strlen(v) * 6;
        if (vx >= 2 + (int)strlen(rows[i].name) * 6 + 2)        /* a value too wide to clear its label (e.g. "<heat smooth>"  */
            draw_text(2, y, rows[i].name, 1);                   /*  being edited) shows alone, never drawn over the label */
        draw_text(vx, y, v, 1);
        if (i == sel) draw_invert(0, y - 1, right - 1, y + 8);
    }
    if (n > MENU_ROWS) {                                        /* scroll bar */
        int th = 52 * MENU_ROWS / n, ty = 10 + (52 - th) * top / (n - MENU_ROWS);
        draw_vline(125, 10, 61); draw_fill(124, ty, 126, ty + th - 1, 1);
    }
}
