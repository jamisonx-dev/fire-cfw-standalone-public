/* Fire hardware layer: the SPI2 chain (buttons, pads, LEDs) refreshed from a 2 kHz timer
 * interrupt, LED brightness by fast blinking, debounced input events, the SELECT encoder,
 * and the 4 endless knobs. */
#ifndef FIRE_HW_H
#define FIRE_HW_H
#include <stdint.h>
#include <stdbool.h>
#include "fire_map.h"

#define LED_LEVELS 8                /* brightness 0 (off) .. 7 (full) */

extern volatile uint8_t led_level[LED_COUNT];
extern volatile uint8_t led_pad_lock;               /* pad LEDs are indices 0..191 (led_pad); the rest are buttons */
#define LED_PAD_END 192

void fire_hw_init(void);            /* chain, timer interrupt, encoder pins, ADC for the knobs */

/* Input events: (index | 0x80 if pressed). Returns -1 when empty. */
int fire_event(void);
bool fire_held(int idx);

int fire_encoder_take(void);        /* SELECT clicks since last call (+ = clockwise) */

void fire_knobs_poll(void);         /* call ~every 5 ms from the main loop */
int fire_knob_take(int k);          /* detents turned since last call for knob 0-3 (+ = clockwise) */

static inline void led_set(int idx, int level)
{
    if (idx >= 0 && idx < LED_COUNT && !(led_pad_lock && idx < LED_PAD_END)) led_level[idx] = (uint8_t)level;
}
void leds_clear(void);

#endif
