/* PAINT mode's rules (paint.c, THE code main.c runs), on the host.
 *   gcc -O2 -Wall -Wextra -I. -fsanitize=bounds -fsanitize-undefined-trap-on-error paint_test.c paint.c -o pt && ./pt */
#include <stdio.h>
#include <string.h>
#include "paint.h"

static int fails, checks;
#define EXPECTED 21
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
    uint8_t cv[PAINT_LEN];
    memset(cv, 0, sizeof cv);
    /* the canvas: 4 bits a pad, neighbours untouched */
    int ok = 1;
    for (int pad = 0; pad < PAINT_PADS; pad++) paint_set(cv, pad, (pad * 7 + 3) % PAINT_COLS);
    for (int pad = 0; pad < PAINT_PADS; pad++) if (paint_get(cv, pad) != (pad * 7 + 3) % PAINT_COLS) ok = 0;
    CHECK(ok, "every pad keeps its own colour (64 pads x 4 bits)");
    CHECK(sizeof cv == 32 && PAINT_LEN == 32, "a painting is 32 bytes");
    memset(cv, 0, sizeof cv);
    paint_set(cv, 5, 9);
    CHECK(cv[2] == 0x90 && paint_get(cv, 4) == 0 && paint_get(cv, 6) == 0, "pad 5 = the high nibble of byte 2, its neighbours stay black");
    struct { uint8_t lo[4], cv[PAINT_LEN], hi[4]; } g;   /* guard bytes round a canvas (a pointer hides its end from the sanitizer) */
    memset(&g, 0, sizeof g); memset(g.lo, 0xFF, 4); memset(g.hi, 0xFF, 4);
    paint_set(g.cv, 64, 3); paint_set(g.cv, 65, 3); paint_set(g.cv, -1, 3); paint_set(g.cv, -2, 3);
    int zero = 1; for (int i = 0; i < PAINT_LEN; i++) if (g.cv[i]) zero = 0;
    for (int i = 0; i < 4; i++) if (g.lo[i] != 0xFF || g.hi[i] != 0xFF) zero = 0;
    CHECK(zero && paint_get(g.cv, 64) == 0 && paint_get(g.cv, 65) == 0 && paint_get(g.cv, -1) == 0 && paint_get(g.cv, -2) == 0,
          "pads off the grid are ignored (nothing written, nothing read past the canvas)");
    paint_set(cv, 5, 2); paint_set(cv, 4, 0x1F);
    CHECK(paint_get(cv, 4) == 0x0F && paint_get(cv, 5) == 2, "a colour past 15 never spills into the next pad");
    /* tap: paint, again = wipe */
    memset(cv, 0, sizeof cv);
    CHECK(paint_tap(cv, 10, 3) == 3 && paint_get(cv, 10) == 3, "a tap paints the picked colour");
    CHECK(paint_tap(cv, 10, 3) == 0 && paint_get(cv, 10) == 0, "a tap on the same colour wipes it");
    paint_set(cv, 10, 4);
    CHECK(paint_tap(cv, 10, 3) == 3, "a tap on another colour paints over it");
    CHECK(paint_tap(cv, 11, 0) == 0 && paint_tap(cv, 10, 0) == 0 && paint_get(cv, 10) == 0, "the eraser picked: a tap makes it black");
    /* the palette: 2 x 2 blocks, red top left, the eraser bottom right, every colour once */
    int seen[PAINT_COLS] = {0}, blocks = 1;
    for (int pad = 0; pad < PAINT_PADS; pad++) {
        int c = paint_swatch(pad);
        seen[c]++;
        int r = pad / 16, col = pad % 16;
        if (paint_swatch((r ^ 1) * 16 + col) != c || paint_swatch(r * 16 + (col ^ 1)) != c) blocks = 0;
    }
    int each = 1; for (int c = 0; c < PAINT_COLS; c++) if (seen[c] != 4) each = 0;
    CHECK(each && blocks, "16 colours, each a 2 x 2 block");
    CHECK(paint_swatch(0) == 1 && paint_swatch(63) == 0 && paint_swatch(2) == 2 && paint_swatch(32) == 9, "red top left, eraser bottom right, rows of 8");
    /* what a pad shows */
    uint8_t rgb[3];
    memset(cv, 0, sizeof cv); paint_set(cv, 7, 3);
    paint_pad_rgb(cv, 7, 1, false, rgb);
    CHECK(!memcmp(rgb, PAINT_RGB[3], 3), "the canvas view shows the pad's colour");
    paint_pad_rgb(cv, 8, 3, false, rgb);
    CHECK(!rgb[0] && !rgb[1] && !rgb[2], "a black pad is dark");
    paint_pad_rgb(cv, 0, 1, true, rgb);
    CHECK(!memcmp(rgb, PAINT_RGB[1], 3), "the palette view: the picked colour at full");
    paint_pad_rgb(cv, 2, 1, true, rgb);
    CHECK(rgb[0] == PAINT_RGB[2][0] / 2 && rgb[1] == PAINT_RGB[2][1] / 2 && rgb[2] == PAINT_RGB[2][2] / 2, "the palette view: the others at half");
    paint_pad_rgb(cv, 63, 1, true, rgb);
    uint8_t r2[3]; paint_pad_rgb(cv, 63, 0, true, r2);
    CHECK(rgb[0] && rgb[0] == rgb[1] && rgb[1] == rgb[2] && r2[0] == PAINT_ERASER_RGB && rgb[0] < r2[0], "the eraser's block is a visible grey, brighter when picked");
    /* the row buttons: tap = load, hold = save once */
    paint_btn_t b; paint_btn_init(&b);
    int s = -1;
    paint_btn(&b, 2, true, 1000, &s);
    CHECK(paint_btn_tick(&b, 1999, &s) == PB_NONE && paint_btn(&b, 2, false, 1500, &s) == PB_LOAD && s == 2, "a short tap loads that slot");
    paint_btn(&b, 1, true, 5000, &s); s = -1;
    int t1 = paint_btn_tick(&b, 5000 + PAINT_HOLD_MS - 1, &s), t2 = paint_btn_tick(&b, 5000 + PAINT_HOLD_MS, &s), s2 = s;
    int t3 = paint_btn_tick(&b, 9000, &s), up = paint_btn(&b, 1, false, 9000, &s);
    CHECK(t1 == PB_NONE && t2 == PB_SAVE && s2 == 1 && t3 == PB_NONE && up == PB_NONE, "a hold saves once, when it reaches 1 s, and its release loads nothing");
    paint_btn(&b, 0, true, 100, &s);
    paint_btn(&b, 3, true, 200, &s);
    int other = paint_btn(&b, 3, false, 300, &s);
    CHECK(other == PB_NONE && paint_btn(&b, 0, false, 400, &s) == PB_LOAD && s == 0, "a second button pressed meanwhile is ignored");
    paint_btn(&b, 3, true, 20000, &s); s = -1;
    CHECK(paint_btn(&b, 3, false, 20000 + PAINT_HOLD_MS + 50, &s) == PB_SAVE && s == 3, "a hold let go before any tick saw it still saves");
    paint_btn_init(&b);
    CHECK(paint_btn(&b, 2, false, 10, &s) == PB_NONE && paint_btn(&b, 4, true, 10, &s) == PB_NONE && b.row == -1,
          "a release with no press, or a button past slot 4, does nothing");
    printf("paint_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
