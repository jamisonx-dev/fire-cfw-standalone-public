#include <math.h>
#include <string.h>
#include "stm32f4xx.h"
#include "fire_hw.h"
#include "crash.h"

#define CHAIN 29

volatile uint8_t led_level[LED_COUNT];              /* the long slot: its level, 0-21 (fire_hw.h) */
volatile uint8_t led_fine[LED_COUNT];               /* v0.34 the short slot's level, 0-21 */
volatile uint8_t t4_gap_us, t4_len_us;
volatile uint8_t led_dual;                          /* v0.34b: 1 = TIM4 runs both slots (ORC mode); 0 = the long slot only */
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

rin_t rin;                                          /* v0.31r the host's remote input (remote.h); main loop only */

int fire_event(void)
{
    if (evq_r == evq_w) return rin_event(&rin);     /* real events first, then the host's */
    int e = evq[evq_r];
    evq_r = (uint8_t)((evq_r + 1) & 63);
    return e;
}

bool fire_held(int idx) { return (in_stable[idx >> 3] >> (idx & 7) & 1) || rin_held(&rin, idx); }

int fire_encoder_take(void)
{
    __disable_irq();
    int v = enc_acc; enc_acc = 0;
    __enable_irq();
    return v + rin_turn_take(&rin, RIN_SELECT);
}

void leds_clear(void) { memset((void *)led_level, 0, sizeof led_level); memset((void *)led_fine, 0, sizeof led_fine); }

/* ---------- the chain: 2 kHz timer interrupt (TIM4) ---------- */

static uint8_t frame;               /* 0..LED_FRAMES-1: the blink frame being shown */
static uint32_t led_lit[LED_FRAMES]; /* v0.34d (ledpat.h ledpat_lit): frame -> which levels are lit in it. In RAM, filled
                                       once at init; TIM4 reads ONE word of it a run (flash reads are the crash suspect) */
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
static inline void canary_watch_arm(const uint8_t *a, const uint8_t *b, const uint8_t *c, unsigned n)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_MON_EN_Msk)) return;
    uintptr_t top = (uintptr_t)(a + n) > (uintptr_t)(b + n) ? (uintptr_t)(a + n) : (uintptr_t)(b + n);
    if ((uintptr_t)(c + n) > top) top = (uintptr_t)(c + n);   /* v0.34: three arrays; the guard is above the highest */
    const uint32_t *w = (const uint32_t *)((top + 3u) & ~3u), g = __stack_chk_guard;
    for (int i = 0; i < 3; i++)
        if (w[i] == g) { DWT->COMP0 = (uint32_t)&w[i]; DWT->MASK0 = 2; DWT->FUNCTION0 = 6; return; }   /* 6 = data write */
}
static inline void canary_watch_disarm(void) { DWT->FUNCTION0 = 0; }

/* 2026-10-26 diagnostic (v0.29d). The v0.29c crashes (and very likely the 2026-10-13 one) were TIM4 returning through
   a bad saved return address: when it popped its registers, the interrupted code's exception frame sat 8 bytes away
   from where TIM4's own pushes said it should be. So TIM4 now checks its stack while it runs: a naked entry hands the
   body the stack pointer the hardware left (= where the interrupted code's frame must start) and the EXC_RETURN in
   LR; the body checks that frame, then that its own SP never moves, then that its saved return is unchanged. Any
   failure -> tim4_slip (main.c) records cause 8 BEFORE the bad return, with a stack snapshot. Kept in globals, not
   locals, so a moved stack can't move the yardstick. */
volatile uint8_t tim4_bench;
static volatile uint32_t t4_in, t4_exc, t4_sp0;
static inline uint32_t sp_read(void) { uint32_t s; __asm volatile("mov %0, sp" : "=r"(s)); return s; }
#define SLIP_CHECK(k) do { uint32_t s_ = sp_read(); if (s_ != t4_sp0) tim4_slip(k, t4_in, t4_sp0, s_); } while (0)

void t4_recheck(uint32_t seen, uint32_t want, uint32_t exc, uint32_t body_sp);
void t4_entry_note(uint32_t sp_in, uint32_t exc);

void __attribute__((naked)) TIM4_IRQHandler(void)
{
    __asm volatile("mov r0, sp\n\tmov r1, lr\n\tb tim4_body\n\t");   /* SP and LR untouched: the body returns for us */
}

void __attribute__((used, noinline)) tim4_body(uint32_t sp_in, uint32_t exc)
{
    uint8_t out[CHAIN], out2[CHAIN], in[CHAIN];
    t4_exc = exc; t4_sp0 = sp_read();
    if (tim4_bench == 1) { tim4_bench = 0; sp_in = 0xFFFFFFFFu; }   /* bench: the 2026-11-11 misread (r0 = r12) */
    bool entry_ok = crash_t4_entry_ok(sp_in, t4_sp0);   /* v0.33b (crash.h): is sp_in an address the frame can be at? */
    if (entry_ok) {
        t4_in = sp_in;
        const volatile uint32_t *f = (const volatile uint32_t *)sp_in;
        if (!(f[7] & (1u << 24)) || f[6] < 0x08008000u || f[6] >= 0x08080000u) tim4_slip(1, sp_in, t4_sp0, t4_sp0);
    } else {
        t4_in = t4_sp0;                             /* (a real slip later this pass must not record through sp_in) */
        t4_entry_note(sp_in, exc);
    }
    canary_watch_arm(out, out2, in, CHAIN);
    TIM4->SR = 0;

    /* v0.34b: measured on the chip, v0.34 spent 208 us of every 500 here with both slots always on. Now (1) the short
       slot runs only while led_dual says a second frame exists (ORC mode); with one frame the pass is what it was in
       v0.33; (2) a frame is built a chain byte at a time, without branches (LED_COUNT = 8 x CHAIN exactly). */
    /* v0.34d: measured on v0.34c, ORC mode spent 217 us of every 500 here, about 80 of them building the two frames
       one LED at a time. Now (3) the frame's word is loaded once and a chain byte is 8 shifts (ledpat.h ledpat_byte:
       the same bits as before, ledpat_test proves it for every level and frame); (4) the short frame is built further
       down, a byte at a time WHILE the long frame's bytes are on the wire, in time the interrupt spent waiting. */
    uint8_t dual = led_dual;
    uint32_t lit = led_lit[frame < LED_FRAMES ? frame : 0];
    for (int b = 0; b < CHAIN; b++) out[b] = (uint8_t)~ledpat_byte(&led_level[b * 8], lit);   /* the long slot; active low */
    frame = (uint8_t)((frame + 1) % LED_FRAMES);
    SLIP_CHECK(2);

    /* v0.34: two slots per pass, and the pass is still exactly one TIM4 period (500 us) with ONE button scan.
         latch 1   the chain shows what the last pass left in it = the SHORT-slot frame; the buttons are loaded (the scan)
         shift     the long-slot frame goes in, the buttons come out          <- this takes ~50 us = the short slot
         latch 2   the chain shows the long-slot frame, until the next pass's latch 1 (~450 us)
         shift     the short-slot frame goes in and waits there (what comes out is thrown away)
       Everything is blocking, as before: no DMA, no second timer (the 2026-10-13 driver had both and uneven scans). */
    uint32_t c0 = TIM4->CNT;
    latch(0);
    for (volatile int d = 0; d < 20; d++) {}
    latch(1);
    for (volatile int d = 0; d < 20; d++) {}
    while (SPI2->SR & SPI_SR_RXNE) (void)SPI2->DR;
    for (int i = 0; i < CHAIN; i++) {
        while (!(SPI2->SR & SPI_SR_TXE)) {}
        *(volatile uint8_t *)&SPI2->DR = out[i];
        if (dual) out2[i] = (uint8_t)~ledpat_byte(&led_fine[i * 8], lit);   /* v0.34d the short slot, while byte i shifts (1.5 us) */
        while (!(SPI2->SR & SPI_SR_RXNE)) {}
        in[i] = *(volatile uint8_t *)&SPI2->DR;
    }
    while (SPI2->SR & SPI_SR_BSY) {}
    if (dual) {                                     /* (one frame only: it stays in the chain and the next latch shows it, as in v0.33) */
        latch(0);
        for (volatile int d = 0; d < 20; d++) {}
        latch(1);
        { uint32_t g = TIM4->CNT - c0; t4_gap_us = (uint8_t)(g > 255 ? 255 : g); }
        for (volatile int d = 0; d < 20; d++) {}
        /* v0.34e: this shift's timing sets no brightness and what comes back is thrown away, so the bytes go out back
           to back: the next one is handed over as soon as the transmit buffer is free, without waiting for (or reading)
           the byte that came back. Same chain clock, same bits; only the pause between bytes is gone (measured on
           v0.34d: 29 bytes took about 62 us, 44 of them on the wire). The receiver flags an overrun, cleared below. */
        for (int i = 0; i < CHAIN; i++) {
            while (!(SPI2->SR & SPI_SR_TXE)) {}
            *(volatile uint8_t *)&SPI2->DR = out2[i];
        }
        while (!(SPI2->SR & SPI_SR_TXE)) {}         /* the last byte has left the buffer for the wire */
        while (SPI2->SR & SPI_SR_BSY) {}
        (void)*(volatile uint8_t *)&SPI2->DR; (void)SPI2->SR;   /* drop what came back: DR then SR clears the overrun flag */
    }
    if (tim4_bench == 3) { tim4_bench = 0; __asm volatile("sub sp, sp, #8" ::: "memory"); }   /* bench: a fake slip */
    SLIP_CHECK(3);

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
    { uint32_t l = TIM4->CNT; t4_len_us = (uint8_t)(l > 255 ? 255 : l); }   /* TIM4 counts us from the tick that raised this */
    canary_watch_disarm();
    SLIP_CHECK(4);
    if (tim4_bench == 5) { tim4_bench = 0; *(volatile uint32_t *)(t4_in - 4) = 0x2Eu; }   /* bench: a fake overwrite */
    if (entry_ok) {                                 /* (the return slot is found from the entry address) */
        uint32_t seen = *(const volatile uint32_t *)(t4_in - 4), want = t4_exc;      /* the pushed LR vs the yardstick */
        if (seen != want) t4_recheck(seen, want, exc, sp_read());
    }
}

/* v0.31g: a return-slot mismatch is looked at twice before it may reset the Fire (crash.h crash_t4_verdict). The
   2026-11-09 v0.31f record was a kind-5 "slip" whose snapshot showed the slot holding a correct EXC_RETURN: the
   check saw a mismatch memory did not have, and reset a healthy, playing Fire. Now: re-read after a DSB, compare with
   the EXC_RETURN the hardware handed TIM4 (exc, a register, never the RAM yardstick), note every mismatch in t4_note,
   and only a slot that really changed is cause 8 kind 5. */
t4_note_t t4_note __attribute__((section(".noinit")));
extern volatile uint32_t ms_ticks;
void __attribute__((noinline)) t4_recheck(uint32_t seen, uint32_t want, uint32_t exc, uint32_t body_sp)
{
    __DSB();
    uint32_t slot = *(const volatile uint32_t *)(t4_in - 4), now = t4_exc;
    int v = crash_t4_verdict(seen, want, slot, now, exc);
    if (t4_note.magic != T4_NOTE_MAGIC) { memset(&t4_note, 0, sizeof t4_note); t4_note.magic = T4_NOTE_MAGIC; }
    t4_note.count++; t4_note.ms = ms_ticks; t4_note.kind = (uint32_t)v;
    t4_note.seen = seen; t4_note.want = want; t4_note.exc = exc;
    if (v == T4V_CHANGED) tim4_slip(5, t4_in, t4_sp0, body_sp);
    t4_exc = exc;                                   /* STALE: put the right yardstick back (harmless otherwise) */
}

/* v0.33b: TIM4 was handed an entry address that cannot be one (crash.h crash_t4_entry_ok). Counted, never a reset. */
void __attribute__((noinline)) t4_entry_note(uint32_t sp_in, uint32_t exc)
{
    if (t4_note.magic != T4_NOTE_MAGIC) { memset(&t4_note, 0, sizeof t4_note); t4_note.magic = T4_NOTE_MAGIC; }
    t4_note.count++; t4_note.ms = ms_ticks; t4_note.kind = T4V_ENTRY;
    t4_note.seen = sp_in; t4_note.want = t4_sp0; t4_note.exc = exc;
}

/* ---------- knobs: endless dual-wiper pots on PA0 via the PC0-PC2 selector ---------- */

static float knob_angle[4], knob_acc[4];
static bool knob_init;
static volatile int knob_out[4];

/* v0.33b: the supply, measured. The chip's 1.21 V reference (ADC channel 17) read against VDDA: raw = 4095 x 1.21 / VDD,
   so a HIGHER raw = a LOWER supply. One reading at the end of every knob poll (200 a second, ~35 us each), at whatever
   moment of the LEDs' 2 kHz switching that falls on. hi = the lowest supply seen since the last reset (07 01). The
   flash is read with wait states that are only in spec above 2.7 V (raw 1835); nobody had measured this. */
volatile vdd_stat_t vdd_stat;
void fire_vdd_reset(void) { vdd_stat.hi = 0; vdd_stat.lo = 0xFFFF; vdd_stat.n = 0; }

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

    ADC1->SQR3 = 17;                                /* the internal reference; adc_read0 puts channel 0 back itself */
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC)) {}
    uint16_t r = (uint16_t)ADC1->DR;
    vdd_stat.last = r;
    if (vdd_stat.n >= 16) {                         /* (the reference needs a moment after it is switched on) */
        if (r > vdd_stat.hi) vdd_stat.hi = r;
        if (r < vdd_stat.lo) vdd_stat.lo = r;
    }
    vdd_stat.n++;
}

int fire_knob_pending(int k) { return knob_out[k] + rin_turn_pending(&rin, k); }

int fire_knob_take(int k)
{
    int v = knob_out[k];
    knob_out[k] = 0;
    return v + rin_turn_take(&rin, k);
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
    ADC->CCR = (1u << ADC_CCR_ADCPRE_Pos) | ADC_CCR_TSVREFE;   /* v0.33b: + the internal reference (channel 17) */
    ADC1->SMPR2 = 4u;                               /* ch0: 84 cycles */
    ADC1->SMPR1 = 7u << 21;                         /* ch17: 480 cycles (23 us at 21 MHz; it needs >= 10 us) */
    vdd_stat.lo = 0xFFFF;
    ADC1->CR2 = ADC_CR2_ADON;

    /* SELECT encoder: PA8 / PA9 inputs (external pull-ups) - reset state is already input */

    for (int f = 0; f < LED_FRAMES; f++) led_lit[f] = ledpat_lit(f, LED_FRAMES);   /* before the interrupt runs */

    /* TIM4: 1 MHz tick, update every 500 us */
    TIM4->PSC = 83;
    TIM4->ARR = 499;
    TIM4->DIER = TIM_DIER_UIE;
    TIM4->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM4_IRQn, 1);
    NVIC_EnableIRQ(TIM4_IRQn);
}
