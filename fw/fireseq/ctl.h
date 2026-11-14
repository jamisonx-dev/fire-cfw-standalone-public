/* Control decisions pulled out of main.c as PURE functions (no hardware, no globals) so the host can test them:
 * main.c is bare-metal and cannot be imported, and the 2026-10-13 review found every one of these rules wrong or
 * untested. ctl_test.c checks each; main.c just calls them.
 *   - parsing a host-protocol SysEx (7D 'ORC' <cmd> ...)
 *   - the light-frame timer (unsigned: it must survive a long freeze and the 71-minute us wrap)
 *   - the PI VU bars' behaviour when the Pi stops sending, host "stop"
 *   - the lazy save of settings changed while stopped */
#ifndef CTL_H
#define CTL_H
#include <stdint.h>


/* ---- host protocol (the Pi -> the Fire): F0 7D 'O' 'R' 'C' <cmd> <data...> F7 ---- */
enum { HC_NONE = -1, HC_HELLO = 0, HC_LEVELS = 1, HC_LIGHTS = 2, HC_CRASH = 6, HC_STATUS = 7, HC_STACK = 8, HC_PEEK = 9, HC_COLORS = 10, HC_SURFACE = 11,
       HC_QUEUE = 13, HC_BARS = 14,
       HC_UBEGIN = 16, HC_UCHUNK = 17, HC_UCOMMIT = 18, HC_UREAD = 19, HC_ULIST = 20, HC_UCLEAR = 21,
       HC_DAWN = 26, HC_DAWROW = 27, HC_DAWLVL = 28,
       HC_INPUT = 32,
       HC_HOSTM = 33, HC_GAMMA = 36, HC_FINEW = 37 };   /* v0.33 ORC mode (hostm.h): 21 <0|1|2> off / on / still here; 24 <10-30> pad curve (22 / 23 are Fire -> host). v0.31r 20: remote input (remote.h): arg = sub-command, data/len = its bytes */
/* The DAW fader page (v0.31d, see "the DAW fader page" below):
 *   1A <n 0-9>                                the page has n rows (0 = no page)                     arg = n
 *   1B <row 0-8> <R> <G> <B> <level> <name 0-8 bytes>   one row in full                         arg = row, data = R.., len = n - 2
 *   1C <row 0-8> <level>                      a level only                                          arg = row, len = level (clamped to 127)
 * A row >= 9 or an n > 9 is ignored; R / G / B / level above 127 are clamped (a 7-bit frame never has them). */
/* UNIVERSAL pages (v0.31, uni.h; 14-bit numbers = 2 bytes, high 7 bits first):
 *   10 <page 0-7> <len hi> <len lo>          begin writing a page of len bytes (9 + 6 x entries)   arg = page, len = len
 *   11 <off hi> <off lo> <data 1-24>          the next chunk (24 bytes, the last one may be shorter) arg = offset, data/len
 *   12 <sum hi> <sum lo>                      commit: sum = all its bytes added, 14 bits             arg = sum
 *   13 <page 0-7> <off hi> <off lo>           read 24 bytes of a page from off                       arg = page, len = offset
 *   14                                        the page list
 *   15 <page 0-7>                             clear a page                                           arg = page */
typedef struct { int cmd; int arg; const uint8_t *data; unsigned len; } ctl_host_t;
/* p/n = the payload after 7D 'ORC' (cmd + data). cmd = HC_NONE when the length or a value is wrong. */
ctl_host_t ctl_host_parse(const uint8_t *p, unsigned n);
/* 09 <address: 5 x 7 bits, low first> = read one 32-bit word (debugging, read only). Allowed: our flash, RAM, the
 * peripherals and the core's system registers, word-aligned. 1 = allowed (a bad address would bus-fault the Fire). */
int ctl_peek_ok(uint32_t addr);
/* SURFACE (v0.19), the ORC drawing its own controls on the pads:
 *   0A <first pad 0-63> <count 1-10> <r g b> x count   pad colours, each 0-7 (data = the triples, arg = first, len = count)
 *   0B 00 / 0B 01 <channel 1-16>                        surface mode off / on (arg = 0 or the channel) */
#define CTL_SURF_MAX 10
/* SCENES (v0.30, for the ORC's scenes):
 *   0D <n 0-31>   queue pattern n for the next bar, exactly like PATTERN up / down and the pattern picker (in song mode
 *                 n is a song SLOT and the song jumps there at the bar); while stopped it switches at once, like the
 *                 buttons. The Fire answers 0D <ok 0/1> <current pattern> <queued: pattern / slot, 127 = none> <song 0/1>.
 *   0E 00 / 01    bar messages off / on (off at power-on; nothing is sent until the host asks)
 *   0F (Fire -> host) <pattern 0-31> <song slot, 127 = no song> <bar 0-127>: a bar starts (step 0, 16, 32 ...)
 *                 while playing, internal or external clock; sent after that step's notes, pattern = after any switch. */
#define CTL_NO_SLOT 127
enum { CQ_REFUSE = 0, CQ_QUEUE = 1, CQ_NOW = 2 };
/* What 0D <n> does. song = song mode is on (n = a slot, song_len of them), npat = the pattern count, running = playing
 * (internal or external clock), cur = the current pattern. CQ_QUEUE sets *queued (-1 = queue cleared: n is the pattern
 * already playing, like the picker); CQ_NOW = switch at once (stopped); CQ_REFUSE = n out of range, nothing changes. */
int ctl_queue_pat(int n, int song, int song_len, int npat, int running, int cur, int *queued);
#define CTL_BAR_LEN 10
/* The 0F message for step g (page = steps per bar): CTL_BAR_LEN bytes into out when g starts a bar, else 0. */
int ctl_bar_msg(uint8_t *out, uint32_t g, int page, int pat, int slot);
/* A whole SysEx as collected (F0 ... F7, `len` bytes, buffer of `cap`): 1 + the payload when it is a complete ORC message
 * that fits the buffer, 0 otherwise (a message that filled the buffer was cut short and is dropped, never half-used). */
int ctl_sysex_payload(const uint8_t *sx, unsigned len, unsigned cap, const uint8_t **p, unsigned *n);

/* ---- the light-frame timer (microseconds, unsigned: right across the 71-minute wrap and after a long freeze) ---- */
#define CTL_FRAME_US 33000u
int ctl_frame_due(uint32_t now_us, uint32_t last_us);

/* ---- the Pi's VU show ---- */
#define CTL_HOST_QUIET_MS 3000u
enum { HT_NONE = 0, HT_FALLBACK = 1, HT_OFF = 2 };
/* The host has stopped sending levels: when the show was picked as PI VU, fall back to another show and wait for it;
 * a host-started show (not the setting) ends. */
int ctl_host_timeout(int lt_on, int lt_host, int show_is_host, uint32_t now_ms, uint32_t host_ms);
/* A levels frame arrived: should the PI VU bars take the pads back (the show IS PI VU, lights are on, currently a fallback)? */
int ctl_host_reengage(int lt_on, int lt_host, int show_is_host);
/* Host "lights off" (02 00): does it end the running show? (host-driven bars, or any show the host started) */
int ctl_host_stop_ends(int lt_on, int lt_host, int by_host);


/* ---- settings changed while stopped (tempo, swing, ...): saved this long after the LAST such change (and at STOP) ---- */
#define CTL_LAZY_SAVE_MS 20000u
uint32_t ctl_lazy_arm(uint32_t now_ms);                     /* the new due time (never 0) */
int ctl_lazy_due(uint32_t lazy_due, uint32_t now_ms);       /* 1 once it is time (0 = not armed) */

/* ---- row buttons: tap = mute, hold + knob = that track's settings (2026-10-19) ----
 * Several rows can be down at once (the owner: "possibly can't mute more than one track at a time"): the old code kept ONE
 * held row, so pressing a second row while the first was down overwrote it and the first row's tap was lost.
 * `held` = the row the knobs belong to (-1 none), `turned` = a knob moved while it was held, `consumed` = a bit per row
 * whose tap was already counted because another row was pressed on top of it (its release then does nothing). */
int ctl_row_press(int *held, int *turned, uint8_t *consumed, int row);     /* the row whose mute to toggle NOW, or -1 */
int ctl_row_release(int *held, int *turned, uint8_t *consumed, int row);   /* 1 = a tap: toggle this row's mute */

/* ---- step pads: tap a lit step = off (2026-11-11, the owner: "i still can't turn more than one step off at a time") ----
 * A lit step turns off when its pad is RELEASED without a knob having been turned, but main.c remembers only ONE held pad:
 * pressing a second pad while the first was still down overwrote it, so the first pad's release did nothing and only the
 * last-pressed step turned off. Same shape as the row buttons above: when another pad is pressed on top of an untouched
 * lit one, that one's tap counts NOW (it turns off), then the new pad is the held one. */
int ctl_step_displace(int held_pad, int was_lit, int turned);               /* 1 = the held pad's tap counts now: turn its step off */

/* ---- per-track volume (v0.23, the STEP faders; the owner: "the fader corresponds to the volume of the track") ----
 * A track's level 0-127 scales the velocity of every note it plays. It is stored in one saved byte per track where 0 means
 * "full" (127), so an older save (all zeros) plays exactly as before; 1 .. 127 = level 0 .. 126. */
int ctl_trk_level(uint8_t stored);              /* saved byte -> level 0-127 */
uint8_t ctl_trk_store(int level);               /* level (clamped to 0-127) -> saved byte; 127 -> 0 so "full" stays all zeros */
int ctl_trk_vel(int vel, int level);            /* vel * level / 127, rounded, never below 1; level 127 leaves vel untouched */
int ctl_trk_pad_level(int col);                 /* a tap on column 0-15 of a row -> level 0-127 (the same mapping as the PERFORM faders) */

/* ---- fader pad lights (2026-10-19): the bar is bright up to a TIP pad, the tip pad is dim, everything above it is dark ----
 * The tip is the exact inverse of a tap (ctl_trk_pad_level), so tapping a pad puts the dim pad on THAT pad. At level 0 the
 * first pad is the dim one (an empty fader is still visible). Returns 2 = bright, 1 = dim, 0 = dark. */
int ctl_fader_tip(int level);                   /* 0-127 -> the tip pad 0-15 */
int ctl_fader_pad(int col, int level);          /* what pad `col` of a row shows for that level */

/* ---- faders follow the host (v0.31c): a Control Change FROM the USB host sets every fader on that channel + CC ----
 * status = the CC's status byte (0xB0-0xBF; anything else changes nothing), num / value = its data bytes.
 * ch[] holds channels 1-16 (as G.fad_ch), cc[] CC numbers, val[] levels; n = how many faders exist (<= 32).
 * A value above 127 is clamped to 127 (a CC number above 127 matches nothing). Returns a mask of the faders whose
 * level CHANGED (bit f = fader f); a fader already at that level is not in it. Sends nothing, saves nothing. */
uint32_t ctl_fader_follow(uint8_t *val, const uint8_t *ch, const uint8_t *cc, int n, int status, int num, int value);

/* ---- fader SETS (v0.31d): the same fader code runs the saved FADERS and the host's DAW page ----
 * A set = n rows of levels, VIEW rows on screen from `top`. The saved FADERS set has one extra row after its last fader
 * (the "+ add" row) while it has fewer than `cap`; the DAW page has none. */
int ctl_fset_max_top(int n, int add_row, int cap, int view);     /* the largest top (0 when everything fits) */
/* Scroll by d rows (SELECT turn / SHIFT + PATTERN): *top moves and is clamped to 0..max_top. Returns -1 when d < 0 and the
 * top could not move at all (already at the first row), +1 when d > 0 and it could not move (already at the last), else 0.
 * (The DAW page turns -1 / +1 into 1E: the previous / next bank; FADERS ignores it.) */
int ctl_fset_scroll(int *top, int d, int max_top);
/* v0.31e: where the DAW page's view goes after it asked for another bank (edge from ctl_fset_scroll): past the bottom
 * (+1) -> the TOP of the new bank, past the top (-1) -> its BOTTOM, so scrolling flows on from track 8 into track 9 (and
 * back). v0.31d left the view where it was: after a bank right the rows on screen were the new bank's LAST strips (often
 * empty), the new tracks sat above, and scrolling up to them asked for the previous bank (the owner, 2026-11-06). */
int ctl_daw_bank_top(int edge, int max_top);
/* A new level for row f (tap / knob): clamped to 0-127, stored; returns 1 when it CHANGED (then the caller sends it). */
int ctl_fset_set(uint8_t *val, int f, int v);

/* ---- the DAW fader page (v0.31d): a second fader set the HOST defines, RAM only (never saved) ----
 * For a Pi bridge to a DAW (REAPER as a Mackie Control surface): up to 8 tracks + the master, each a colour, a name and a
 * level. The host owns the rows: the Fire only moves levels (and says so with 1D) and asks for another bank (1E). */
#define CTL_DAW_ROWS 9
#define CTL_DAW_NAME 8
typedef struct {
    uint8_t n;                                  /* rows the host defined, 0 = no page */
    uint8_t val[CTL_DAW_ROWS];                  /* levels 0-127: what the pads + screen show (= fine >> 7) */
    uint16_t fine[CTL_DAW_ROWS];                /* v0.31f: the full level 0-16383 (Mackie faders are 14-bit) */
    uint8_t rgb[CTL_DAW_ROWS][3];               /* pad colour, LED brightness 0-7 per channel (ctl_daw_rgb) */
    char name[CTL_DAW_ROWS][CTL_DAW_NAME + 1];  /* printable ASCII, NUL-terminated, "" = none */
} ctl_daw_t;
/* 1A: n rows (0-9, larger is cut to 9); every row from n on is cleared (level 0, dark, no name). */
void ctl_daw_count(ctl_daw_t *d, int n);
/* 1B: row (0-8, else nothing) in full from data = R G B levelHi levelLo name... (v0.31f: a 14-bit level, high 7 bits
 * first), len = how many of those bytes (< 5 = nothing).
 * The name is cut to 8 characters; anything outside ASCII 0x20-0x7E becomes '?'. */
void ctl_daw_row(ctl_daw_t *d, int row, const uint8_t *data, unsigned len);
/* 1C (v0.31f: <row> <hi> <lo>) / a tap / a knob: a 14-bit level (clamped 0-16383) for a row; val = fine >> 7. Returns 1
 * when it changed. Never sends anything itself. */
int ctl_daw_level(ctl_daw_t *d, int row, int fine);
/* v0.31f a pad tap's column level 0-127 -> the 14-bit level (0 -> 0, 127 -> 16383): taps jump exactly as before. */
int ctl_daw_tap_fine(int level);
/* v0.31f knob acceleration for the DAW page (full resolution): ms since this knob's last detent -> the 14-bit step per
 * detent. A slow, single click = 1 (the finest REAPER takes); quicker turns take bigger steps, so a full sweep is quick. */
int ctl_daw_knob_step(uint32_t dt_ms);
/* The host's colour (0-127 per channel, e.g. REAPER's track colour) -> the pads' 0-7: scaled so the brightest channel is
 * 7 (every row as bright as a FADERS colour, the hue kept); black (no colour set) -> white 6,6,6 (FADERS' white). */
void ctl_daw_rgb(int r, int g, int b, uint8_t out[3]);
/* Fire -> host: 1D <row> <hi> <lo> (v0.31f: 14-bit; the user moved a row: tap / knob; never for a level the host set),
 * 10 bytes; 1E <dir> <count> (v0.31f: the page never scrolls itself; SELECT asks the host to move: 00 = up / back,
 * 01 = down / on, count = detents 1-127, e.g. 4 for SHIFT + PATTERN), 9 bytes. */
#define CTL_DAW_LEVEL_LEN 10
#define CTL_DAW_BANK_LEN 9
int ctl_daw_level_msg(uint8_t *out, int row, int fine);
int ctl_daw_bank_msg(uint8_t *out, int dir, int count);

/* ---- tap tempo (v0.24, the SELECT knob's push) ----
 * the owner: "tap tempo is hard to get right". The rules that make it feel right:
 *   - the tempo is the AVERAGE over the whole window of the last CTL_TAP_MAX taps ((last - first) / intervals), not the last
 *     two taps: a human tap is off by 10-30 ms, and averaging n intervals divides that error by n;
 *   - two taps already give a tempo (the 2nd tap sets it), and every further tap refines it;
 *   - a pause longer than CTL_TAP_GAP_MS starts over (the first tap of a new count never changes the tempo);
 *   - a tap whose interval is more than 30 % off the running average is a tempo change or a missed / extra tap: the window
 *     restarts from that tap, so it never averages two different tempos;
 *   - after such a restart the OLD tempo stays until a 3rd tap confirms the new one (two agreeing intervals): one late tap
 *     must not flash a wrong tempo (a tap 200 ms late followed by an on-time one reads as 200 BPM for one interval). A new
 *     count after a PAUSE has no old tempo to protect, so its 2nd tap sets the tempo as before;
 *   - two taps closer than CTL_TAP_MIN_MS are one tap with a bouncing contact: the second is ignored. */
#define CTL_TAP_MAX 8
#define CTL_TAP_GAP_MS 2000u
#define CTL_TAP_MIN_MS 150u
typedef struct { uint32_t t[CTL_TAP_MAX]; int n; int probation; } ctl_tap_t;   /* probation: restarted by a sudden change, not yet trusted */
/* A tap at now_ms. Returns the new tempo (40-300 BPM), or 0 when this tap sets none (the first of a count, a bounce, a restart). */
int ctl_tap(ctl_tap_t *s, uint32_t now_ms);

/* ---- the ROLLER / LOOPER (v0.29, like the BeatStep Pro's) ----
 * While a roller pad is held, a track plays a short loop that starts at the step that was about to sound when it began
 * (the anchor), instead of its real position. The sequencer's own step counter keeps running underneath, so on release
 * the track simply plays the real step again = exactly where it would have been. Codes 0-4 loop 16 / 8 / 4 / 2 / 1 steps;
 * 5-7 repeat the anchor step 2 / 3 / 4 times per step (1/32, 1/48 = sixteenth triplets, 1/64). */
#define CTL_ROLL_CODES 8
extern const char *const CTL_ROLL_NAME[CTL_ROLL_CODES];     /* "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/48", "1/64" */
uint32_t ctl_roll_g(uint32_t g, uint32_t anchor, int code);  /* the step to play instead of g (code < 0 = no roll: g) */
int ctl_roll_hits(int code);                    /* hits per step forced by the roll, 0 = the step's own ratchet */
/* Which code a track rolls with, from the held roller pads: `n` pads, pad i = {code, track (-1 = all tracks), press order}.
   The newest pad naming this track wins, else the newest all-tracks pad, else -1 (no roll). */
typedef struct { int8_t code, track; uint32_t seq; } ctl_roll_pad_t;
int ctl_roll_code(const ctl_roll_pad_t *pads, int n, int track);

#endif
