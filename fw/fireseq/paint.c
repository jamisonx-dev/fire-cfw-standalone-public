/* PAINT mode's rules (see paint.h). Pure C. */
#include "paint.h"

/* A first guess, to be tuned by eye on the Fire (the curve squares them: 64 = a quarter as bright as 127). */
const uint8_t PAINT_RGB[PAINT_COLS][3] = {
    {0, 0, 0},                                      /* 0 eraser */
    {127, 0, 0}, {127, 30, 0}, {127, 90, 0}, {70, 127, 0}, {0, 127, 0},          /* red orange yellow lime green */
    {0, 127, 50}, {0, 127, 127}, {0, 60, 127}, {0, 0, 127}, {50, 0, 127},         /* mint cyan sky blue purple */
    {127, 0, 127}, {127, 10, 40}, {127, 50, 25}, {50, 15, 0}, {127, 127, 127},    /* magenta pink peach brown white */
};
const char *const PAINT_NAME[PAINT_COLS] = {
    "eraser", "red", "orange", "yellow", "lime", "green", "mint", "cyan", "sky", "blue", "purple",
    "magenta", "pink", "peach", "brown", "white",
};

int paint_get(const uint8_t *cv, int pad)
{
    if (pad < 0 || pad >= PAINT_PADS) return 0;
    return (cv[pad >> 1] >> ((pad & 1) * 4)) & 0x0F;
}

void paint_set(uint8_t *cv, int pad, int c)
{
    if (pad < 0 || pad >= PAINT_PADS) return;
    int sh = (pad & 1) * 4;
    cv[pad >> 1] = (uint8_t)((cv[pad >> 1] & ~(0x0F << sh)) | ((c & 0x0F) << sh));
}

int paint_tap(uint8_t *cv, int pad, int cur)
{
    int c = paint_get(cv, pad) == cur ? 0 : cur;
    paint_set(cv, pad, c);
    return c;
}

int paint_swatch(int pad)
{
    if (pad < 0 || pad >= PAINT_PADS) return 0;
    int block = (pad / 16 / 2) * 8 + (pad % 16) / 2;     /* 0-15: two rows of 8 blocks */
    return (block + 1) % PAINT_COLS;                      /* block 0 = red (1) ... block 15 = the eraser (0) */
}

void paint_pad_rgb(const uint8_t *cv, int pad, int cur, bool palette, uint8_t rgb[3])
{
    int c = palette ? paint_swatch(pad) : paint_get(cv, pad);
    for (int j = 0; j < 3; j++) {
        uint8_t v = PAINT_RGB[c][j];
        if (palette && c == 0) v = PAINT_ERASER_RGB;
        if (palette && c != cur) v = (uint8_t)(v / 2);
        rgb[j] = v;
    }
}

void paint_btn_init(paint_btn_t *b) { b->row = -1; b->done = 0; b->t = 0; }

int paint_btn(paint_btn_t *b, int row, bool down, uint32_t now, int *slot)
{
    if (row < 0 || row >= PAINT_SLOTS) return PB_NONE;
    if (down) {
        if (b->row < 0) { b->row = (int8_t)row; b->done = 0; b->t = now; }
        return PB_NONE;
    }
    if (b->row != row) return PB_NONE;               /* a button that was not the one being held */
    int r = b->done ? PB_NONE : now - b->t >= PAINT_HOLD_MS ? PB_SAVE : PB_LOAD;
    *slot = row;
    b->row = -1;
    return r;
}

int paint_btn_tick(paint_btn_t *b, uint32_t now, int *slot)
{
    if (b->row < 0 || b->done || now - b->t < PAINT_HOLD_MS) return PB_NONE;
    b->done = 1;
    *slot = b->row;
    return PB_SAVE;
}
