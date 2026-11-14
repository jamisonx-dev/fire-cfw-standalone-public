/* DAW mode's protocol translator (v0.36): Mackie Control (MCU) and HUI, the two languages a DAW speaks to a control
 * surface. Pure C, no hardware: daw_test.c runs it on the Pi. Byte tables: the DAW-mode research notes (kept outside this repo)
 * sections 1 (MCU), 2 (HUI) and 5.6 (one firmware, both).
 *
 * IN:  daw_byte() takes the DAW port's bytes one at a time (USB cable 1) and turns them into events (a fader level, a
 *      button light, text, a timecode digit, a V-pot ring, a HUI ping). It answers the host by itself where the
 *      protocol needs an answer (MCU handshake + version, HUI ping), through daw_out().
 * OUT: daw_button / daw_fader / daw_vpot / daw_jog / daw_bank / daw_channel write complete messages through daw_out().
 *
 * The caller provides   void daw_out(const uint8_t *m, int n);   = send ONE complete MIDI message (or SysEx) on the DAW
 * port. A HUI button / touch is two CC messages that go out back to back in the same call (zone select, then port):
 * nothing else can get between them because the firmware's main loop is the only sender.
 *
 * Which protocol: DAW_AUTO (the default) stays silent and LOCKS to whichever the host speaks first:
 *   MCU: an MCU SysEx (F0 00 00 66 14 .. or Logic Control's 10), a pitch bend (a fader), a note other than 90 00 00,
 *        a CC 30-37 (V-pot ring) or 40-4B (time display);
 *   HUI: a HUI SysEx (F0 00 00 66 05 ..), a CC 0C / 2C (a light), 00-07 / 20-27 (a fader) or 10-17 (a ring), or a
 *        SECOND 90 00 00 in a row (a HUI ping; one alone is ambiguous: MCU's "REC 1 off" is the same 3 bytes).
 *   v0.36b (the owner's REAPER test, 2026-11-13): v0.36a locked only on SysEx and the first ping, so a DAW that was already
 *   running (it only sends changes) never woke a fresh Auto up. The message that locks it is decoded too.
 * In Auto a SysEx of the OTHER protocol re-locks (a different DAW on the same port). Until locked nothing is decoded and
 * nothing is sent. DAW_MCU / DAW_HUI are locked from the start. daw_init() unlocks.
 * Any F0 00 00 66 message it does not understand (reset, all-LEDs-off, backlight, meter mode ...) is ignored.
 *
 * RAM: the whole state is one daw_state_t (sizeof checked by daw_test, target under 200 B); daw_out costs nothing. */
#ifndef DAW_H
#define DAW_H
#include <stdint.h>

enum { DAW_AUTO = 0, DAW_MCU = 1, DAW_HUI = 2 };

#define DAW_STRIPS   9              /* 8 tracks + the master (MCU only: HUI has no master fader) */
#define DAW_TOUCH_MS 500u           /* a fader with no new level for this long is let go (touch off) */
/* The text buffer, shared by both protocols (only one is ever locked):
 *   MCU: the 2 x 56 LCD, offsets 0-55 top line, 56-111 bottom (8 strips x 7 characters per line)
 *   HUI: the 2 x 40 main display at 0-79 (row r, column c = r * 40 + c), then the nine 4-character scribble strips at
 *        DAW_HUI_STRIP + ch * 4 (ch 0-7 = the tracks, 8 = the select / assign display) */
#define DAW_MCU_LCD   112
#define DAW_HUI_MAIN  80
#define DAW_HUI_STRIP 80
#define DAW_TEXT      116
/* Timecode digits, digit[0] = the RIGHTMOST (both protocols count from the right). ASCII, bit 7 = its decimal point.
 * MCU: 0-9 the 10 timecode digits, 10-11 the 2-digit assign display. HUI: 0-7 (frames, seconds, minutes, hours). */
#define DAW_DIGITS 12

/* Buttons, one id for both protocols. 0-55 are per strip (id = group + strip 0-7); for 0-39 the id IS the MCU note. */
enum {
    DB_REC = 0, DB_SOLO = 8, DB_MUTE = 16, DB_SELECT = 24, DB_VSEL = 32,     /* MCU notes 0-39 */
    DB_AUTO = 40, DB_INSERT = 48,                                             /* HUI only */
    DB_BANK_L = 56, DB_BANK_R, DB_CH_L, DB_CH_R,
    DB_REW, DB_FF, DB_STOP, DB_PLAY, DB_RECORD,
    DB_UP, DB_DOWN, DB_LEFT, DB_RIGHT, DB_ZOOM, DB_SCRUB, DB_LOOP,
    DB_SHIFT, DB_OPTION, DB_CTRL, DB_ALT, DB_SAVE, DB_UNDO,
    DB_AREAD, DB_AWRITE, DB_ATRIM, DB_ATOUCH, DB_ALATCH,
    DB_CLICK, DB_FLIP, DB_NAMEVAL, DB_SMPTEBEATS, DB_MARKER,
    DB_F1, DB_F8 = DB_F1 + 7,
    DB_LED_SMPTE, DB_LED_BEATS, DB_LED_RUDE,                                  /* lights only, no button */
    DB_A_TRACK, DB_A_SEND, DB_A_PAN, DB_A_PLUGIN, DB_A_EQ,
    DB_COUNT
};
#define DB_GLOBAL DB_BANK_L         /* first id in the table of non-strip buttons */
#define DAW_LEDB ((DB_COUNT + 7) / 8)
/* Each non-strip button's code in each protocol: MCU note, HUI zone << 3 | port; 0xFF = not in that protocol. */
extern const uint8_t DAW_MCU_NOTE[DB_COUNT - DB_GLOBAL], DAW_HUI_ZP[DB_COUNT - DB_GLOBAL];
/* HUI strip zones (0-7): the port of each per-strip group, in id order REC SOLO MUTE SELECT VSEL AUTO INSERT */
extern const uint8_t DAW_HUI_STRIP_PORT[7];

/* Events from daw_byte (at most one per byte). */
enum {
    DE_NONE = 0,
    DE_LEVEL,       /* idx = strip 0-8, val = 0-16383 (the host moved a fader) */
    DE_LED,         /* idx = button id, val = 0 off / 1 blink / 2 on (daw_led reads it back) */
    DE_TEXT,        /* idx = first changed offset in daw_text(), val = one past the last */
    DE_DIGIT,       /* idx = first changed digit, val = how many */
    DE_RING,        /* idx = V-pot 0-7, val = the ring byte (bit 6 centre LED, bits 5-4 mode, bits 3-0 position 0-11) */
    DE_PING,        /* HUI ping answered (90 00 7F already sent): the host is alive */
    DE_HELLO,       /* MCU handshake / version request answered: the host is alive */
    DE_LOCK,        /* Auto just locked to a protocol (daw_proto() says which) */
};
typedef struct { uint8_t type, idx; uint16_t val; } daw_ev_t;

typedef struct {
    char text[DAW_TEXT];
    uint8_t digit[DAW_DIGITS];
    uint8_t led_on[DAW_LEDB], led_blink[DAW_LEDB];
    uint8_t ring[8];
    uint16_t touch_ms[DAW_STRIPS];  /* low 16 bits of the ms clock at each strip's last level */
    uint16_t touched;               /* bit per strip: touch on has been sent */
    uint8_t mode, proto, dev;       /* DAW_*, the locked protocol (DAW_AUTO = not yet), the MCU device id heard (14 / 10) */
    uint8_t status, d1, nd;         /* channel-message parser (running status kept) */
    uint8_t sx, sx_pos, cmd, tpos;  /* SysEx parser: 0 none / 1 reading / 2 skipping, bytes since F0, command, text offset */
    uint8_t tlo, thi;               /* the text range this SysEx changed */
    uint8_t zone;                   /* HUI: the zone the host last selected for a light (0xFF = none) */
    uint8_t hi_strip, hi_val;       /* HUI: the fader's top 7 bits wait here for its bottom 7 */
    uint8_t pings;                  /* Auto, not locked yet: 90 00 00 seen in a row (a HUI ping, or MCU's "REC 1 off") */
} daw_state_t;
extern daw_state_t daw_st;

void daw_out(const uint8_t *m, int n);     /* PROVIDED BY THE CALLER (main.c / daw_test.c) */

void daw_init(int mode);            /* DAW_AUTO / DAW_MCU / DAW_HUI: forget everything, blank text + lights, unlock */
int daw_proto(void);                /* DAW_MCU / DAW_HUI once locked, else DAW_AUTO */
int daw_byte(uint8_t b, daw_ev_t *ev);  /* one byte from the DAW port; returns 1 and fills *ev when something changed */
int daw_led(int id);                /* 0 off / 1 blink / 2 on */
const char *daw_text(void);         /* DAW_TEXT characters, printable ASCII (no NUL at the end) */
uint8_t daw_ring(int pot);

/* Out. Each returns 1 if it sent something, 0 if this protocol has no such control (or nothing is locked yet). */
int daw_announce(void);             /* MCU: the surface introduces itself (01 connection query); HUI / unlocked: nothing */
int daw_button(int id, int down);
int daw_fader(int strip, int fine, uint32_t now_ms);    /* touch on (first move), then the level; daw_tick lets go */
void daw_tick(uint32_t now_ms);     /* call often: touch off for every strip idle DAW_TOUCH_MS */
void daw_release_all(void);         /* touch off for every touched strip (leaving DAW mode, banking) */
int daw_vpot(int pot, int detents); /* +clockwise / -counter; clamped to 63 per message */
int daw_jog(int detents);
int daw_bank(int dir);              /* -1 / +1: 8 tracks (lets every fader go first) */
int daw_channel(int dir);           /* -1 / +1: one track (lets every fader go first) */

/* The fixed bytes the surface sends about itself (MCU). */
#define DAW_SERIAL    "FLAME01"     /* 7 ASCII, in 01 and 03 */
#define DAW_VERSION   "V1.00"       /* 5 ASCII, the 14 reply */

#endif
