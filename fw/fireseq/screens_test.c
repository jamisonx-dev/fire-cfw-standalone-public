/* Renders the screens.c screens with sample data to scr_N.raw (128x64, one byte per pixel) and checks
 * that nothing draws outside the body area it owns.
 *   gcc -O1 -Wall -I. screens_test.c screens.c draw.c -o screens_test && ./screens_test */
#include <stdio.h>
#include <string.h>
#include "draw.h"
#include "screens.h"

static int fails, checks;
#define EXPECTED 23                              /* the number of checks a full run makes: a drift fails the run */
#define CHECK(c, msg) do { checks++; if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static void save(int n)
{
    char name[32]; snprintf(name, sizeof name, "scr_%d.raw", n);
    FILE *f = fopen(name, "wb");
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) fputc((fb[(y >> 3) * FB_W + x] >> (y & 7) & 1) ? 255 : 0, f);
    fclose(f);
}
static int lit(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) n += fb[(y >> 3) * FB_W + x] >> (y & 7) & 1;
    return n;
}

int main(void)
{
    static const char *const NAME[4] = {"T1", "T2", "T3", "M1"};
    static const char *const NAME16[4] = {"T13", "T14", "T15", "T16"};
    static const char *PAT[4] = {"3000300030003000", "0000200000002000", "1020102010201020", "0000000300000200"};
    uint8_t lv[4][16], muted[4] = {0, 0, 1, 0};
    for (int r = 0; r < 4; r++) for (int c = 0; c < 16; c++) lv[r][c] = (uint8_t)(PAT[r][c] - '0');

    /* 1: playing, mid-bar, a status message */
    draw_clear();
    scr_head_t h = {"PAT 3>4", 120, 50, 0, true, true, "P1/4", "rec>T4"};
    scr_header(&h); scr_tracks(NAME, lv, muted, (int8_t[4]){6, 6, 6, 6});
    save(1);
    CHECK(lit(0, 0, 127, 19) > 0 && lit(0, 20, 127, 63) > 0, "header and body both drawn");

    /* 2: stopped, swing + external clock, a track shorter than the page, long track names */
    uint8_t lv2[4][16]; memcpy(lv2, lv, sizeof lv2);
    for (int c = 8; c < 16; c++) lv2[3][c] = 255;               /* M1 is 8 steps long */
    draw_clear();
    scr_head_t h2 = {"PAT 12", 96, 58, 2, false, false, "P2/8", "song off"};
    scr_header(&h2); scr_tracks(NAME16, lv2, (uint8_t[4]){0, 0, 0, 0}, (int8_t[4]){-1, -1, -1, -1});
    save(2);
    CHECK(lit(0, 0, 127, 63) > 0, "second main view drawn");

    /* 3: song mode (long left text pushes the tempo right), clock out */
    draw_clear();
    scr_head_t h3 = {"S3/12 P5 2/4>S4", 128, 50, 1, true, false, "P1/2", "copy>P7"};
    scr_header(&h3); scr_tracks(NAME, lv, muted, (int8_t[4]){15, 3, 15, 7});   /* polymeter: each track's own position */
    save(3);

    /* 4: faders */
    scr_fader_t f[4] = {{1, 1, 7, 100}, {1, 2, 74, 64}, {1, 3, 1, 12}, {1, 4, 11, 127}};
    draw_clear();
    scr_head_t h4 = {"PAT 3", 120, 50, 0, false, false, "FADERS", "F1-4/16"};
    scr_header(&h4); scr_faders(f);
    save(4);
    scr_fader_t f2[4] = {{1, 14, 7, 100}, {1, 15, 7, 64}, {1, 16, 7, 12}, {0, 0, 0, 0}};
    draw_clear(); scr_header(&h4); scr_faders(f2);
    save(5);

    /* 6, 7: a held melodic step; a held drum step (TIE) */
    draw_clear();
    scr_head_t h6 = {"PAT 3", 120, 50, 0, false, false, "M1 step 5", "pg1"};
    scr_step_t st = {3, 50, true, "C#2 (37)", 0, 75};
    scr_header(&h6); scr_step_bars(&st);
    save(6);
    draw_clear();
    scr_head_t h7 = {"PAT 3", 120, 50, 0, true, false, "T2 step 9", "pg1"};
    scr_step_t st2 = {1, 100, false, "", 1, 0};
    scr_header(&h7); scr_step_bars(&st2);
    save(7);

    /* 8, 9: popups */
    draw_clear(); scr_popup("TEMPO", 120, "bpm", 120, 40, 300, true); save(8);
    draw_clear(); scr_popup("SWING", 58, "%", 58, 50, 75, false); save(9);
    CHECK(lit(0, 0, 127, 63) > 100, "popup drawn");

    /* 10-13: menu pages */
    scr_menu_row_t top[3] = {{"Autosave", "ON"}, {"Save now", ""}, {"Lights", ">"}};
    draw_clear(); scr_menu("MENU", "BRW=back", top, 3, 2, false, false); save(10);
    CHECK(lit(125, 10, 126, 61) == 0, "no scroll bar for a short page");
    scr_menu_row_t lights[6] = {{"Start lights", ""}, {"Show", "SPARKLE"}, {"Speed", "x1.00"}, {"Idle start", "1m"}, {"Smooth", "on"}, {"Cost", "412us"}};
    draw_clear(); scr_menu("MENU/LIGHTS", "BRW=back", lights, 6, 1, false, false); save(11);
    draw_clear(); scr_menu("MENU/LIGHTS", "SEL=set", lights, 6, 2, true, true); save(12);
    int inv = lit(0, 28, 119, 36);                                  /* the selected row (index 2 -> y 28..36) is mostly lit */
    CHECK(inv > 400, "selected row is inverted");
    CHECK(fb[(32 >> 3) * FB_W + 124] >> (32 & 7) & 1, "the inverted bar reaches the edge when there is no scroll bar");
    draw_clear(); scr_menu("MENU", "saving...", top, 3, 1, false, false); save(13);
    /* editing a value too wide to clear its label ("<heat smooth>" is 78 px, "Bar look" ends at x=50): the label is left
       off, so the row's left part is the plain inverted bar (all lit); a value that fits keeps its label */
    scr_menu_row_t bars[6] = {{"Show", "PI VU"}, {"Speed", "x1.00"}, {"Idle start", "1m"}, {"Smooth", "on"}, {"Heat flip", "off"}, {"Bar look", "heat smooth"}};
    draw_clear(); scr_menu("MENU/LIGHTS", "SEL=set", bars, 6, 5, true, false); save(14);
    CHECK(lit(2, 55, 43, 62) == 42 * 8, "an edited value that would hit its label is drawn alone (no text over text)");
    draw_clear(); scr_menu("MENU/LIGHTS", "SEL=set", bars, 6, 3, true, false);
    CHECK(lit(2, 37, 37, 44) < 36 * 8, "an edited value that fits keeps its label");
    scr_menu_row_t many[8] = {{"a", "1"}, {"b", "2"}, {"c", "3"}, {"d", "4"}, {"e", "5"}, {"f", "6"}, {"g", "7"}, {"h", "8"}};
    draw_clear(); scr_menu("LONG", "", many, 8, 7, false, false);
    CHECK(lit(124, 10, 126, 61) > 0, "scroll bar for a long page");

    /* the playhead: a lane either side of the column, present only when there is one */
    draw_clear(); scr_tracks(NAME, lv, muted, (int8_t[4]){-1, -1, -1, -1});
    int none = lit(0, 21, 127, 62);
    draw_clear(); scr_tracks(NAME, lv, muted, (int8_t[4]){-1, 6, -1, -1});
    CHECK(lit(0, 21, 127, 62) > none, "playhead adds pixels");
    CHECK(fb[(40 >> 3) * FB_W + (20 + 6 * 6 - 1)] >> (40 & 7) & 1, "row 2's playhead lane at the left of its column");
    CHECK(!(fb[(25 >> 3) * FB_W + (20 + 6 * 6 - 1)] >> (25 & 7) & 1), "no lane on a row without a playhead");

    /* per-track volume bars: a fuller level lights more of the bar, a muted row shows M, nothing spills past the rule */
    draw_clear(); scr_levels(NAME, (uint8_t[4]){0, 32, 64, 127}, (uint8_t[4]){0, 0, 0, 0});
    int l0 = lit(24, 22, 82, 29), l1 = lit(24, 32, 82, 39), l3 = lit(24, 52, 82, 59);
    CHECK(l0 < l1 && l1 < l3, "the volume bar lights more as the level rises");
    draw_clear(); scr_levels(NAME, (uint8_t[4]){127, 127, 127, 127}, (uint8_t[4]){0, 1, 0, 0});
    CHECK(lit(88, 32, 94, 39) > 0 && lit(88, 22, 94, 29) == 0 && lit(0, 0, 127, 19) == 0,
          "a muted track shows M on its own row, and the header area stays untouched");

    /* v0.31 UNIVERSAL: knob rows (label, bar, value; the last-turned row marked), and 4 text lines cut at 19 characters */
    {
        const char *lab[4] = {"1A CC74 ch1", "1B PB ch16", "1C --", "1D CC127 ch16"};
        int val[4] = {127, 64, 0, 0}, used[4] = {1, 1, 0, 1};
        draw_clear(); scr_uni_knobs(lab, val, used, 1); save(15);
        CHECK(lit(0, 0, 127, 19) == 0 && lit(0, 32, 1, 38) == 14 && lit(0, 22, 1, 28) == 0 && lit(84, 42, 127, 49) == 0,
              "UNIVERSAL knob rows: below the header, the mark only on its row, no bar / value for an unassigned knob");
        CHECK(lit(85, 23, 104, 28) > lit(85, 33, 104, 38) && lit(85, 33, 104, 38) > 0 && lit(106, 22, 109, 29) == 0,
              "a fuller value lights more bar; a gap between the bar and a 3-digit value");
        const char *ln[4] = {"1 type note latch", "2 ch 16", "a line that is far too long to fit", 0};
        draw_clear(); scr_lines(ln); save(16);
        CHECK(lit(0, 0, 127, 21) == 0 && lit(114, 42, 127, 49) == 0 && lit(0, 42, 113, 49) > 0 && lit(0, 52, 127, 63) == 0,
              "text lines: in the body, cut at 19 characters, a missing line draws nothing");
    }

    /* v0.31d: the DAW page = the fader rows with the host's names (8 characters at most, left of the bar at x 60) */
    {
        scr_fader_t df[4] = {{1, 6, 0, 100}, {1, 7, 0, 0}, {1, 8, 0, 64}, {1, 9, 0, 127}};
        const char *dn[4] = {"WWWWWWWWWWWW", "", "Bass", "MASTER"};   /* a 12-character name (only 8 may show), an empty one */
        draw_clear();
        scr_head_t hd = {"PAT 3", 120, 50, 1, true, false, "DAW", "F6-9/9"};
        scr_header(&hd); scr_fader_rows(df, dn);
        save(17);
        CHECK(lit(0, 22, 47, 29) > 0 && lit(48, 20, 59, 63) == 0, "DAW names: at most 8 characters (48 px), nothing between them and the bar");
        uint8_t got[sizeof fb];
        memcpy(got, fb, sizeof fb);
        draw_clear(); draw_text(0, 32, "row 7", 1);
        int same7 = 1;
        for (int y = 32; y < 40; y++) for (int x = 0; x < 59; x++)
            if ((got[(y >> 3) * FB_W + x] >> (y & 7) & 1) != (fb[(y >> 3) * FB_W + x] >> (y & 7) & 1)) same7 = 0;
        memcpy(fb, got, sizeof fb);
        CHECK(lit(60, 22, 108, 29) > lit(60, 42, 108, 49) && lit(61, 33, 106, 38) == 0 && lit(0, 32, 30, 39) > 0 && same7,
              "DAW rows: the bar follows the level (row 2 at 0 is empty); an empty name shows 'row 7'");
        CHECK(lit(110, 52, 127, 59) > 0 && lit(110, 22, 127, 29) > 0 && lit(0, 0, 127, 19) > 0, "DAW rows: the value at the right (100, 127), the header drawn");
        scr_fader_t ff[4] = {{1, 1, 7, 100}, {1, 2, 74, 64}, {1, 3, 1, 12}, {1, 4, 11, 127}};
        uint8_t a[sizeof fb], b2[sizeof fb];
        draw_clear(); scr_faders(ff); memcpy(a, fb, sizeof fb);
        draw_clear(); scr_fader_rows(ff, 0); memcpy(b2, fb, sizeof fb);
        CHECK(!memcmp(a, b2, sizeof fb), "FADERS draws exactly as before (scr_faders = the rows with no names)");
    }

    /* a header never writes below its rule, a status too long for the room is cut */
    draw_clear();
    scr_head_t hl = {"PAT 3>4", 120, 58, 2, true, true, "P1/4", "a very long status line indeed"};
    scr_header(&hl);
    CHECK(lit(0, 20, 127, 63) == 0, "header stays above y=20");
    CHECK(lit(0, 10, 127, 18) > 0, "header line two drawn");
    printf("screens_test: %s (%d checks%s)\n", fails ? "FAILED" : "all passed", checks, checks == EXPECTED ? "" : " -- EXPECTED differs!");
    return (fails || checks != EXPECTED) ? 1 : 0;
}
