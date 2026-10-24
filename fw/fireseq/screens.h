/* The OLED screens that are pictures rather than text: the compact header, the step view with a
 * playhead, fader bars, a held step's settings as bars, and the big-number popup. Pure drawing into
 * the framebuffer (draw.h), no hardware and no sequencer state: main.c fills in the plain values, and
 * screens_test.c renders them to images on the Pi / PC so they can be looked at before flashing.
 *
 * Layout (128 x 64): header y 0..19 (two text lines + a rule), body rows at y = 22, 32, 42, 52. */
#ifndef SCREENS_H
#define SCREENS_H
#include <stdint.h>
#include <stdbool.h>

#define SCR_BODY_Y 22
#define SCR_ROW_H 10

typedef struct {
    const char *left;       /* line 1, left: "PAT 3>4" or the song text */
    int bpm, swing;         /* swing 50 = none (not shown) */
    int clk;                /* 0 internal (not shown), 1 clock out, 2 external */
    bool playing, star;     /* star = unsaved changes */
    const char *ctx;        /* line 2, after the play icon */
    const char *status;     /* line 2, after ctx; cut to whatever room the right-hand tags leave */
} scr_head_t;
void scr_header(const scr_head_t *h);

/* The step view: 4 tracks x the 16 steps of the current page.
 * lv[r][c]: 0 off, 1 soft, 2 normal, 3 accent, 255 = past the track's end (nothing drawn).
 * Off steps on a beat (every 4th) show as a bigger dot. head[r] = column of the step that just played
 * on that row's track (-1 none; tracks can have different lengths): drawn as a lane either side of it. */
void scr_tracks(const char *const name[4], const uint8_t lv[4][16], const uint8_t muted[4], const int8_t head[4]);

/* Faders: up to 4 rows. used 1 = a fader (cc, val), 0 = the "+ add" row, -1 = empty. */
typedef struct { int used; int num, cc, val; } scr_fader_t;
void scr_faders(const scr_fader_t f[4]);

/* Per-track volume (the STEP faders): 4 rows, a name, a bar of the 0-127 level, a percentage; muted rows show M. */
void scr_levels(const char *const name[4], const uint8_t val[4], const uint8_t muted[4]);

/* A held step's first knob page: K1 ratchet 1-4, K2 chance %, K3 note (melodic), K4 gate % or TIE / SLIDE. */
typedef struct {
    int ratchet, chance;            /* 1-4; 25 / 50 / 75 / 100 */
    bool mel; const char *note;     /* melodic track: "C#2 (37)" */
    int hold;                       /* 0 = gate, 1 = TIE, 2 = SLIDE */
    int gate;                       /* % when hold == 0 */
} scr_step_t;
void scr_step_bars(const scr_step_t *s);

/* A big number for a moment after a change (tempo, swing). v in lo..hi draws the bar. */
void scr_popup(const char *title, int value, const char *unit, int v, int lo, int hi, bool star);

/* A menu page: a title, a note on the right (a hint or a toast), up to 6 rows visible. The selected row is
 * inverted; while editing its value shows in <>. A scroll bar appears when there are more than 6 rows. */
typedef struct { const char *name; const char *value; } scr_menu_row_t;
void scr_menu(const char *title, const char *note, const scr_menu_row_t *rows, int n, int sel, bool edit, bool star);

#endif
