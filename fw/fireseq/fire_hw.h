/* Fire hardware layer: the SPI2 chain (buttons, pads, LEDs) refreshed from a 2 kHz timer
 * interrupt, LED brightness by fast blinking, debounced input events, the SELECT encoder,
 * and the 4 endless knobs. */
#ifndef FIRE_HW_H
#define FIRE_HW_H
#include <stdint.h>
#include <stdbool.h>
#include "fire_map.h"
#include "remote.h"
#include "ledpat.h"

#define LED_LEVELS 8                /* led_set: brightness 0 (off) .. 7 (full) */

/* v0.33: led_level[] holds FINE levels, 0 .. LED_FRAMES (21, ledpat.h): in how many of every 21 chain passes the LED is
   lit. v0.34: every pass has TWO slots, a long one (led_level, about 450 us) and a SHORT one (led_fine, the ~50 us the
   long frame takes to shift in), each with its own level. One step of led_fine is worth about a tenth of a step of
   led_level, so brightness has about 200 steps instead of 22 (hostm.c hm_duty splits a brightness into the two).
   led_set takes the old 0-7 and lights BOTH slots at 3 x that: exactly the on-time it always had (level / 7). */
extern volatile uint8_t led_level[LED_COUNT];
extern volatile uint8_t led_fine[LED_COUNT];
extern volatile uint8_t led_dual;                   /* v0.34b: set while led_fine differs from led_level (ORC mode): TIM4 then runs the short slot too */
extern volatile uint8_t t4_gap_us, t4_len_us;       /* measured every pass: the short slot's length, the whole interrupt's (us, 255 = more) */
extern volatile uint8_t led_pad_lock;               /* pad LEDs are indices 0..191 (led_pad); the rest are buttons */
#define LED_PAD_END 192
_Static_assert(LED_COUNT == 232, "TIM4 builds a frame 8 LEDs per chain byte: LED_COUNT must be 8 x 29");

void fire_hw_init(void);            /* chain, timer interrupt, encoder pins, ADC for the knobs */

/* Input events: (index | 0x80 if pressed). Returns -1 when empty. Each of these also carries the host's REMOTE
   input (remote.h, v0.31r): real events first, then injected ones; held = really held OR held by the host; the
   takes add the host's detents. Main loop only (rin is not interrupt-safe and needs no lock there). */
extern rin_t rin;
int fire_event(void);
bool fire_held(int idx);

int fire_encoder_take(void);        /* SELECT clicks since last call (+ = clockwise) */

void fire_knobs_poll(void);         /* call ~every 5 ms from the main loop */
int fire_knob_take(int k);          /* detents turned since last call for knob 0-3 (+ = clockwise) */
int fire_knob_pending(int k);       /* the same count WITHOUT taking it (is a turn waiting?) */

static inline void led_set(int idx, int level)
{
    if (idx >= 0 && idx < LED_COUNT && !(led_pad_lock && idx < LED_PAD_END)) led_level[idx] = led_fine[idx] = ledpat_fine(level);
}
void leds_clear(void);

/* v0.29d diagnostic: TIM4 checks its own stack while it runs (fire_hw.c). On a slip it calls this (main.c: records
   crash cause 8 and restarts). kind: 1 = the interrupted code's frame is not where TIM4's stack starts, 2 / 3 / 4 = the
   stack pointer moved (after the LEDs / after the SPI chain / after the buttons), 5 = the saved return address changed. */
/* v0.33b the supply (fire_hw.c fire_knobs_poll): raw readings of the 1.21 V reference, VDD = 1.21 x 4095 / raw. */
typedef struct { uint16_t last, hi, lo, pad; uint32_t n; } vdd_stat_t;   /* hi = the LOWEST supply seen, lo = the highest */
extern volatile vdd_stat_t vdd_stat;
void fire_vdd_reset(void);

void tim4_slip(uint32_t kind, uint32_t sp_in, uint32_t sp0, uint32_t sp_now) __attribute__((noreturn));
extern volatile uint8_t tim4_bench;     /* bench builds only (CRASH_TEST 7E 06 / 07 / 08): fake a slip of kind 3 / 5, or (1) the 2026-11-11 entry misread */

/* v0.31g: TIM4's return-slot check no longer resets the Fire on a mismatch that memory does not have (crash.h
   crash_t4_verdict). Every mismatch is NOTED here (.noinit: survives a reset; magic-guarded; host 06 v3 reads it,
   06 01 clears it). kind = T4V_*, ms = uptime, seen / want = the check's reads, exc = the EXC_RETURN TIM4 was given. */
#define T4_NOTE_MAGIC 0x7E4A0B1Eu
typedef struct { uint32_t magic, count, ms, kind, seen, want, exc; } t4_note_t;
extern t4_note_t t4_note;

#endif
