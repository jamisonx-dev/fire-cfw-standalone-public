/* LED brightness: which blink frames an LED is lit in (v0.33).
 * The chain interrupt shows one on/off frame every 500 us. Until v0.33 a cycle was 7 frames and an LED of level L
 * (0-7) was lit in the first L of them: 8 brightness steps. Now a cycle is LED_FRAMES (21) frames and an LED holds a
 * FINE level 0-21, lit in that many frames SPREAD EVENLY over the cycle: 22 steps, and the interrupt's timing, its
 * chain transfer and its button scan are exactly what they were (the 2026-10-13 64-step driver changed all three and
 * broke SHIFT + pad; this changes none). The old levels are fine level 3 x L: the same on-time (L / 7), and because the
 * frames are spread, the same 7-frame rhythm as before or faster. Only the new in-between levels blink slower, the
 * slowest (fine 1) at 2000 / 21 = 95 Hz.
 * Pure C: ledpat_test.c checks it on the Pi. */
#ifndef LEDPAT_H
#define LEDPAT_H
#include <stdint.h>

#define LED_FRAMES 21               /* blink frames per cycle = the finest brightness level (full on) */
#define LED_PER_LEVEL 3             /* fine levels per old level: 7 x 3 = 21 */
#define LED_PAT_N 32                /* table size: a level is masked to 0-31, 22-31 = full on (never out of bounds) */

/* Bit f set = lit in frame f. level frames out of `frames`, evenly spread (Bresenham). */
static inline uint32_t ledpat_bits(int level, int frames)
{
    if (level < 0) level = 0;
    if (level > frames) level = frames;
    uint32_t m = 0;
    for (int f = 0; f < frames; f++)
        if ((f + 1) * level / frames > f * level / frames) m |= 1u << f;
    return m;
}

/* v0.34d: the same table turned on its side, for the interrupt. Bit L set = an LED of level L (0-31, 22-31 = full on)
   is lit in frame f. One word per frame: the interrupt loads the word for its frame ONCE, and each LED is then a shift
   and a mask instead of a table read (measured on the chip, v0.34c: the two frame builds were about 80 of the
   interrupt's 217 us in ORC mode). */
static inline uint32_t ledpat_lit(int f, int frames)
{
    uint32_t m = 0;
    for (int L = 0; L < LED_PAT_N; L++) m |= (ledpat_bits(L, frames) >> f & 1u) << L;
    return m;
}

/* One chain byte: the levels of 8 LEDs -> their 8 lit bits (bit k = LED k) for the frame whose word is `lit`.
   Written out, no loop and no branch, and always inline: the interrupt builds a byte while another is on the wire. */
#define LEDPAT_BIT_(k) ((lit >> (lv[k] & (LED_PAT_N - 1)) & 1u) << (k))
static inline __attribute__((always_inline)) uint8_t ledpat_byte(const volatile uint8_t *lv, uint32_t lit)
{
    return (uint8_t)(LEDPAT_BIT_(0) | LEDPAT_BIT_(1) | LEDPAT_BIT_(2) | LEDPAT_BIT_(3)
                   | LEDPAT_BIT_(4) | LEDPAT_BIT_(5) | LEDPAT_BIT_(6) | LEDPAT_BIT_(7));
}

/* An old 0-7 level as a fine level (out of range = clamped). */
static inline uint8_t ledpat_fine(int level)
{
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    return (uint8_t)(level * LED_PER_LEVEL);
}

#endif
