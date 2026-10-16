/* Akai Fire control and LED map, measured on the hardware 2026-10-06 (see FIRE_HARDWARE_MAP.md).
 * Inputs and LEDs are indices into the 29-byte SPI2 chain: index = byte * 8 + bit. */
#ifndef FIRE_MAP_H
#define FIRE_MAP_H

/* ---- inputs (set bit = pressed) ---- */
enum {
    IN_ALT = 64, IN_PATSONG = 65, IN_PLAY = 66, IN_STOP = 67, IN_REC = 68,
    IN_ROW2 = 72, IN_ROW3 = 73, IN_ROW4 = 74,
    IN_STEP = 75, IN_NOTE = 76, IN_DRUM = 77, IN_PERFORM = 78, IN_SHIFT = 79,
    IN_GRID_R = 80, IN_GRID_L = 81, IN_SELECT = 82, IN_BROWSER = 83,
    IN_PAT_UP = 84, IN_PAT_DN = 85, IN_KNOBMODE = 86, IN_ROW1 = 87,
};
#define IN_COUNT 88                 /* bytes 0-10 */

/* Pad at row r (0 = top), column c (0-15). */
static inline int in_pad(int r, int c) { int n = r * 16 + c; return (7 - n / 8) * 8 + n % 8; }
/* Inverse: input index -> pad number r*16+c, or -1 if not a pad. */
static inline int pad_of_input(int idx) { if (idx >= 64) return -1; return (7 - idx / 8) * 8 + idx % 8; }

static const unsigned char IN_ROWBTN[4] = {IN_ROW1, IN_ROW2, IN_ROW3, IN_ROW4};

/* ---- LEDs (chain output bits; active low on the wire, handled by the driver) ---- */
/* Pad colour channel j: 0 = red, 1 = green, 2 = blue. */
static inline int led_pad(int r, int c, int j) { int L = 3 * c + j; return ((3 - r) * 6 + 5 - L / 8) * 8 + L % 8; }

enum {
    LED_STEP_Y = 213, LED_STEP_R = 214, LED_NOTE_Y = 215, LED_NOTE_R = 200,
    LED_DRUM_Y = 201, LED_DRUM_R = 202, LED_PERFORM_Y = 203, LED_PERFORM_R = 204,
    LED_SHIFT_Y = 205, LED_SHIFT_R = 206, LED_ALT_Y = 207,
    LED_PATSONG_Y = 192, LED_PATSONG_G = 193,
    LED_PLAY_Y = 194, LED_PLAY_G = 195, LED_STOP_Y = 196, LED_REC_Y = 197, LED_REC_G = 198,
    LED_GRID_R = 224, LED_GRID_L = 225, LED_BROWSER = 226, LED_PAT_UP = 227, LED_PAT_DN = 228,
    LED_CHANNEL = 229, LED_MIXER = 230, LED_USER1 = 231, LED_USER2 = 216,
};
#define LED_COUNT 232

/* Row buttons: the button's own LED (green) and the red / green side LEDs. */
static const unsigned char LED_ROW_G[4]      = {217, 220, 223, 210};
static const unsigned char LED_ROWSIDE_R[4]  = {218, 221, 208, 211};
static const unsigned char LED_ROWSIDE_G[4]  = {219, 222, 209, 212};

#endif
