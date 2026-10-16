#include <math.h>
#include <string.h>
#include "stm32f4xx.h"
#include "fire_hw.h"

#define CHAIN 29

volatile uint8_t led_level[LED_COUNT];
volatile uint8_t led_pad_lock;                      /* 1 = a light show owns the pad LEDs: led_set leaves them alone */

/* ---------- input events (filled by the chain interrupt, read by the main loop) ---------- */

static volatile uint8_t evq[64];
static volatile uint8_t evq_w, evq_r;
static uint8_t in_stable[CHAIN], in_last[CHAIN];
static volatile int enc_acc;

static void ev_push(uint8_t e)
{
    uint8_t n = (uint8_t)((evq_w + 1) & 63);
    if (n != evq_r) { evq[evq_w] = e; evq_w = n; }
}

int fire_event(void)
{
    if (evq_r == evq_w) return -1;
    int e = evq[evq_r];
    evq_r = (uint8_t)((evq_r + 1) & 63);
    return e;
}

bool fire_held(int idx) { return in_stable[idx >> 3] >> (idx & 7) & 1; }

int fire_encoder_take(void)
{
    __disable_irq();
    int v = enc_acc; enc_acc = 0;
    __enable_irq();
    return v;
}

void leds_clear(void) { memset((void *)led_level, 0, sizeof led_level); }

/* ---------- the chain: 2 kHz timer interrupt (TIM4) ---------- */

static uint8_t frame;               /* 0..LED_LEVELS-2: an LED of level L is on when L > frame */
static bool leds_enabled;

static inline void latch(int level) { GPIOB->BSRR = level ? (1u << 12) : (1u << 28); }

static void encoder_step(void)
{
    /* SELECT: PA8 / PA9 rest high; each click both dip low. Clockwise = PA8 falls first. */
    static uint8_t first;           /* 0 none, 1 = PA8 fell first, 2 = PA9 fell first */
    uint32_t idr = GPIOA->IDR;
    int a = idr >> 8 & 1, b = idr >> 9 & 1;
    if (!first) {
        if (!a && b) first = 1;
        else if (a && !b) first = 2;
    } else if (a && b) {
        enc_acc += (first == 1) ? 1 : -1;
        first = 0;
    }
}

/* 2026-10-14: TIM4's stack canary was found overwritten 7 times (v0.17c) and nothing in its code can do it. While it
   runs, a DWT write-watchpoint guards its canary word: ANY CPU write to it (any code, any context) raises the debug-
   monitor exception at once (priority 0, main.c), which records the writer's PC and context. If the canary still dies
   with no watchpoint record, the writer is not the CPU. The slot is found by value in the few words just ABOVE the
   frame's local arrays (where -fstack-protector-strong puts the canary): a first-match scan from the stack pointer
   found STALE guard copies left by other functions inside out[] and fired on TIM4's own memset (bench, 2026-10-14).
   Not found = not armed. A handful of loads per run. */
extern uint32_t __stack_chk_guard;
static inline void canary_watch_arm(const uint8_t *a, const uint8_t *b, unsigned n)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_MON_EN_Msk)) return;
    uintptr_t top = (uintptr_t)(a + n) > (uintptr_t)(b + n) ? (uintptr_t)(a + n) : (uintptr_t)(b + n);
    const uint32_t *w = (const uint32_t *)((top + 3u) & ~3u), g = __stack_chk_guard;
    for (int i = 0; i < 3; i++)
        if (w[i] == g) { DWT->COMP0 = (uint32_t)&w[i]; DWT->MASK0 = 2; DWT->FUNCTION0 = 6; return; }   /* 6 = data write */
}
static inline void canary_watch_disarm(void) { DWT->FUNCTION0 = 0; }

void TIM4_IRQHandler(void)
{
    uint8_t out[CHAIN], in[CHAIN];
    canary_watch_arm(out, in, CHAIN);
    TIM4->SR = 0;

    memset(out, 0xFF, sizeof out);  /* active low: all off */
    for (int i = 0; i < LED_COUNT; i++)
        if (led_level[i] > frame) out[i >> 3] &= (uint8_t)~(1u << (i & 7));
    frame = (uint8_t)((frame + 1) % (LED_LEVELS - 1));

    latch(0);
    for (volatile int d = 0; d < 20; d++) {}
    latch(1);
    for (volatile int d = 0; d < 20; d++) {}
    while (SPI2->SR & SPI_SR_RXNE) (void)SPI2->DR;
    for (int i = 0; i < CHAIN; i++) {
        while (!(SPI2->SR & SPI_SR_TXE)) {}
        *(volatile uint8_t *)&SPI2->DR = out[i];
        while (!(SPI2->SR & SPI_SR_RXNE)) {}
        in[i] = *(volatile uint8_t *)&SPI2->DR;
    }
    while (SPI2->SR & SPI_SR_BSY) {}

    if (!leds_enabled) { leds_enabled = true; GPIOA->BSRR = 1u << (7 + 16); }   /* PA7 low after the first real frame */

    /* debounce: a bit must read the same on two scans in a row (1 ms) */
    for (int i = 0; i <= 10; i++) {
        uint8_t v = in[i];
        if (i == 8) v &= 0x1F;      /* 8.5-8.7 always read 1 */
        uint8_t settled = (uint8_t)~(v ^ in_last[i]);
        uint8_t change = (uint8_t)((v ^ in_stable[i]) & settled);
        if (change) {
            for (int b = 0; b < 8; b++)
                if (change >> b & 1) ev_push((uint8_t)((i * 8 + b) | ((v >> b & 1) ? 0x80 : 0)));
            in_stable[i] ^= change;
        }
        in_last[i] = v;
    }
    encoder_step();
    canary_watch_disarm();
}

/* ---------- knobs: endless dual-wiper pots on PA0 via the PC0-PC2 selector ---------- */

static float knob_angle[4], knob_acc[4];
static bool knob_init;
static volatile int knob_out[4];

static uint16_t adc_read0(void)
{
    ADC1->SQR3 = 0;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC)) {}
    return (uint16_t)ADC1->DR;
}

void fire_knobs_poll(void)
{
    uint16_t v[8];
    for (int pos = 0; pos < 8; pos++) {
        GPIOC->BSRR = (7u << 16) | (uint32_t)pos;
        for (volatile int i = 0; i < 200; i++) {}
        v[pos] = adc_read0();
    }
    const float step = 6.2831853f / 64.0f;          /* 64 detents per turn */
    for (int k = 0; k < 4; k++) {
        float a = atan2f((float)v[2 * k] - 2048.0f, (float)v[2 * k + 1] - 2048.0f);
        if (!knob_init) { knob_angle[k] = a; continue; }
        float d = a - knob_angle[k];
        if (d > 3.14159265f) d -= 6.2831853f;
        if (d < -3.14159265f) d += 6.2831853f;
        if (fabsf(d) < 0.02f) continue;             /* noise */
        knob_angle[k] = a;
        knob_acc[k] += d;
        while (knob_acc[k] >= step)  { knob_acc[k] -= step; knob_out[k]++; }   /* measured: angle up = clockwise */
        while (knob_acc[k] <= -step) { knob_acc[k] += step; knob_out[k]--; }
    }
    knob_init = true;
}

int fire_knob_take(int k)
{
    int v = knob_out[k];
    knob_out[k] = 0;
    return v;
}

/* ---------- init ---------- */

void fire_hw_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN | RCC_APB1ENR_TIM4EN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    /* PA7 = LED enable (active low): keep LEDs off until the first frame is out */
    GPIOA->BSRR = 1u << 7;
    GPIOA->MODER = (GPIOA->MODER & ~(3u << 14)) | (1u << 14);

    /* SPI2 chain: PB13 SCK, PB14 MISO, PB15 MOSI (AF5), PB12 latch */
    for (int pin = 13; pin <= 15; pin++) {
        GPIOB->MODER = (GPIOB->MODER & ~(3u << (2 * pin))) | (2u << (2 * pin));
        GPIOB->OSPEEDR |= 3u << (2 * pin);
        GPIOB->AFR[1] = (GPIOB->AFR[1] & ~(0xFu << (4 * (pin - 8)))) | (5u << (4 * (pin - 8)));
    }
    GPIOB->BSRR = 1u << 12;
    GPIOB->MODER = (GPIOB->MODER & ~(3u << 24)) | (1u << 24);
    SPI2->CR1 = SPI_CR1_MSTR | (2u << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;   /* 5.25 MHz, mode 0 */
    SPI2->CR1 |= SPI_CR1_SPE;

    /* knobs: PA0 analog, PC0-PC2 selector outputs */
    GPIOA->MODER |= 3u;
    for (int pin = 0; pin <= 2; pin++)
        GPIOC->MODER = (GPIOC->MODER & ~(3u << (2 * pin))) | (1u << (2 * pin));
    ADC->CCR = 1u << ADC_CCR_ADCPRE_Pos;
    ADC1->SMPR2 = 4u;                               /* ch0: 84 cycles */
    ADC1->CR2 = ADC_CR2_ADON;

    /* SELECT encoder: PA8 / PA9 inputs (external pull-ups) - reset state is already input */

    /* TIM4: 1 MHz tick, update every 500 us */
    TIM4->PSC = 83;
    TIM4->ARR = 499;
    TIM4->DIER = TIM_DIER_UIE;
    TIM4->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM4_IRQn, 1);
    NVIC_EnableIRQ(TIM4_IRQn);
}
