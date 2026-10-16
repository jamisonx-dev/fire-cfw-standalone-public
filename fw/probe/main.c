/* "probe": map the Fire's button/LED shift-register chain.
 *
 * Clocks SPI2 (PB13 SCK, PB14 MISO, PB15 MOSI, PB12 latch) every 5 ms: 29 bytes out, 29 in.
 * Whenever the 29 incoming bytes change (and every 2 s anyway) it sends
 *     F0 7D 10 <mode> <58 nibbles: byte0 hi, byte0 lo, ...> F7
 * and shows them on the OLED, with the last bit that changed.
 *
 * Also: F0 7D 15 <16 x 12-bit analog, 3 nibbles each> F7 20x/s (PA0/PA1 at selector PC0-2 = 0..7),
 *       F0 7D 14 <A,B,C,D input pins, 4 nibbles each, driven pins masked> F7 on change.
 *
 * Commands (SysEx):
 *   F0 7D 11 <mode> F7          latch style: 0 = pulse PB12 low->high, then shift (default)
 *                                            1 = pulse PB12 high->low, then shift
 *                                            2 = PB12 low while shifting, high after
 *                                            3 = PB12 untouched (stays high)
 *   F0 7D 12 <cpha|cpol<<1> F7  SPI clock phase/polarity (stock = 0)
 *   F0 7D 13 <58 nibbles> F7    set the 29 bytes shifted OUT (LEDs; default all 0)
 *   F0 7D 16 <id> <level> F7    drive a stock output pin: id 0 = PA2, 1 = PA7, 2 = PC4, 3 = PC5
 *   F0 7D 01 <text> F7          label on the OLED's bottom line
 *   F0 7D 1A <on> F7            velocity mode: PA1 at ~42.7 kHz; 8 ms after a pad press sends F0 7D 19 <pad bit>
 *                               <baseline> <raw peak> <smoothed peak> <steepest rise> <64-point profile> F7 (3 nibbles each)
 *   F0 7D 17 <k hi> <k lo> F7   LED walk from output bit k: lights one LED, the next press names it,
 *                               replies F0 7D 18 <k hi> <k lo> <input hi> <input lo> F7 (input 0x3FFF = SELECT = none)
 *   F0 47 7F 43 72 00 00 F7     reboot to ST ROM DFU (as stock)
 * Safety nets as hello/oled: watchdog, watchdog-reset -> DFU, faults -> DFU.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "tusb.h"
#include "draw.h"
#include "oled_hw.h"

#define CHAIN 29

volatile uint32_t ms_ticks;
void SysTick_Handler(void) { ms_ticks++; }
uint32_t tusb_time_millis_api(void) { return ms_ticks; }
void OTG_FS_IRQHandler(void) { tusb_int_handler(0, true); }

static void __attribute__((noreturn)) reboot_to_dfu(void)
{
    __disable_irq();
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
    RTC->BKP0R = 1u;
    NVIC_SystemReset();
}
void HardFault_Handler(void)  { reboot_to_dfu(); }
void MemManage_Handler(void)  { reboot_to_dfu(); }
void BusFault_Handler(void)   { reboot_to_dfu(); }
void UsageFault_Handler(void) { reboot_to_dfu(); }

static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->PLLCFGR = 0x07415408u;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    SystemCoreClock = 84000000u;
}

static void usb_pins_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER   = (GPIOA->MODER & ~(0xFu << 22)) | (0xAu << 22);
    GPIOA->OSPEEDR |= 0xFu << 22;
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~(0xFFu << 12)) | (0xAAu << 12);
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
}

/* ---------- the chain ---------- */

static uint8_t chain_out[CHAIN], chain_in[CHAIN], chain_prev[CHAIN];
static uint8_t latch_mode, spi_mode;
static int last_byte = -1, last_bit = -1, last_level = -1;
static char label[22] = "press one button";

static bool dirty;   /* defined (with its initial value) further down */

/* LED walk: one output bit lit at a time; the next press names the control that lit. */
#define OUT_BITS (CHAIN * 8)
#define SELECT_PUSH (10 * 8 + 2)
static bool walk_on, walk_wait_release;
static int walk_k;

static void walk_show(void)
{
    memset(chain_out, 0xFF, sizeof chain_out);       /* active low: all off */
    if (walk_on) chain_out[walk_k >> 3] &= (uint8_t)~(1u << (walk_k & 7));
    char *l = label;
    memcpy(l, "LED ", 4); l += 4;
    if (walk_k >= 100) *l++ = (char)('0' + walk_k / 100);
    if (walk_k >= 10) *l++ = (char)('0' + walk_k / 10 % 10);
    *l++ = (char)('0' + walk_k % 10);
    memcpy(l, ": press it", 11);
    if (!walk_on) memcpy(label, "walk done", 10);
    dirty = true;
}

static bool inputs_released(void)
{
    for (int i = 0; i <= 10; i++) {
        uint8_t v = chain_in[i];
        if (i == 8) v &= 0x1F;                      /* 8.5-8.7 always read 1 */
        if (v) return false;
    }
    return true;
}

static void spi2_setup(void)
{
    SPI2->CR1 = 0;
    SPI2->CR1 = SPI_CR1_MSTR | (2u << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI | (spi_mode & 3);  /* /8 = 5.25 MHz, as stock */
    SPI2->CR1 |= SPI_CR1_SPE;
}

static void chain_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    (void)RCC->APB1ENR;
    for (int pin = 13; pin <= 15; pin++) {
        GPIOB->MODER = (GPIOB->MODER & ~(3u << (2 * pin))) | (2u << (2 * pin));
        GPIOB->OSPEEDR |= 3u << (2 * pin);
        GPIOB->AFR[1] = (GPIOB->AFR[1] & ~(0xFu << (4 * (pin - 8)))) | (5u << (4 * (pin - 8)));
    }
    GPIOB->BSRR = 1u << 12;                         /* PB12 output, high (the stock idle) */
    GPIOB->MODER = (GPIOB->MODER & ~(3u << 24)) | (1u << 24);
    spi2_setup();
}

static inline void latch(int level) { GPIOB->BSRR = level ? (1u << 12) : (1u << 28); }
static void short_wait(void) { for (volatile int i = 0; i < 40; i++) {} }   /* ~2 us */

static void chain_scan(void)
{
    if (latch_mode == 0) { latch(0); short_wait(); latch(1); short_wait(); }
    if (latch_mode == 1) { latch(1); short_wait(); latch(0); short_wait(); }
    if (latch_mode == 2) { latch(0); short_wait(); }
    while (SPI2->SR & SPI_SR_RXNE) (void)SPI2->DR;  /* drop stale */
    for (int i = 0; i < CHAIN; i++) {
        while (!(SPI2->SR & SPI_SR_TXE)) {}
        *(volatile uint8_t *)&SPI2->DR = chain_out[i];
        while (!(SPI2->SR & SPI_SR_RXNE)) {}
        chain_in[i] = *(volatile uint8_t *)&SPI2->DR;
    }
    while (SPI2->SR & SPI_SR_BSY) {}
    if (latch_mode == 2) { short_wait(); latch(1); }
}

/* ---------- analog (PA0/PA1 via the PC0-PC2 selector) + raw input pins ---------- */

static uint16_t analog[16];        /* [pos*2 + ch] */
static uint16_t pins_now[4], pins_prev[4];
/* pins we drive or that belong to USB/SPI/OLED: never reported */
static const uint16_t PIN_MASK[4] = {
    (uint16_t)~((1u << 2) | (1u << 7) | (1u << 11) | (1u << 12) | (1u << 0) | (1u << 1)),   /* A */
    (uint16_t)~(0xF000u),                                                                     /* B */
    (uint16_t)~((1u << 0) | (1u << 1) | (1u << 2) | (0xFu << 10)),                            /* C */
    (uint16_t)~(1u << 2),                                                                     /* D */
};

static void analog_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    (void)RCC->APB2ENR;
    GPIOA->MODER |= 0xFu;                           /* PA0, PA1 analog */
    for (int pin = 0; pin <= 2; pin++)              /* PC0-PC2 outputs, low (as stock) */
        GPIOC->MODER = (GPIOC->MODER & ~(3u << (2 * pin))) | (1u << (2 * pin));
    GPIOC->BSRR = 7u << 16;
    ADC->CCR = 1u << ADC_CCR_ADCPRE_Pos;            /* 84 MHz / 4 = 21 MHz */
    ADC1->SMPR2 = (4u << 0) | (4u << 3);            /* ch0/ch1: 84 cycles */
    ADC1->CR2 = ADC_CR2_ADON;
}

static uint16_t adc_read(int ch)
{
    ADC1->SQR3 = (uint32_t)ch;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC)) {}
    return (uint16_t)ADC1->DR;
}

static void analog_scan(void)
{
    for (int pos = 0; pos < 8; pos++) {
        GPIOC->BSRR = (7u << 16) | (uint32_t)pos;   /* selector = pos */
        for (volatile int i = 0; i < 200; i++) {}   /* ~10 us to settle */
        analog[pos * 2 + 0] = adc_read(0);
        analog[pos * 2 + 1] = adc_read(1);
    }
}

/* Velocity mode: PA1 (ADC1 ch1) free-running at ~42.7 kHz into a circular DMA buffer.
 * 8 ms after each pad press, report three strength measures over the last ~24 ms:
 *   raw peak above baseline, smoothed (8-sample average) peak above baseline, steepest rise. */
#define VBUF 4096            /* 256 bursts of 16 conversions = 32 ms */
#define NW (VBUF / 2)            /* PA1 samples per window */
static uint16_t vbuf[VBUF];
static bool vel_mode;
static int vel_pad = -1;
static uint32_t vel_at;
static volatile int sel_pos;                         /* selector position of the burst being taken */

/* Same ADC setup as the stock firmware (register values logged in the emulator):
 * TIM5 CC1 at 8 kHz triggers a 16-conversion scan alternating PA0/PA1, 15-cycle sampling, circular DMA. */
static void vel_start(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;
    ADC1->CR2 = 0;
    DMA2_Stream0->CR = 0;
    while (DMA2_Stream0->CR & DMA_SxCR_EN) {}
    DMA2->LIFCR = 0x3Du;
    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)vbuf;
    DMA2_Stream0->NDTR = VBUF;
    DMA2_Stream0->CR = DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC | DMA_SxCR_CIRC;
    DMA2_Stream0->CR |= DMA_SxCR_EN;
    ADC1->CR1 = ADC_CR1_SCAN;
    ADC1->SQR1 = 0x00F08020u;                       /* 16 conversions, ... ch0, ch1 */
    ADC1->SQR2 = 0x02008020u;
    ADC1->SQR3 = 0x02008020u;                       /* ch0, ch1, ch0, ch1, ... */
    ADC1->SMPR2 = 0x9u;                             /* ch0, ch1: 15 cycles */
    ADC1->CR2 = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS | (1u << 28) | (0xAu << 24);   /* rising edge of TIM5_CC1 */
    TIM5->CR1 = 0;
    TIM5->PSC = 0;
    TIM5->ARR = 10500;                              /* 84 MHz / 10501 = 8 kHz */
    TIM5->CCR1 = 0x1EC3;
    TIM5->CCMR1 = 0x68;                             /* PWM mode 1, preload */
    TIM5->CCER = 0x3;
    TIM5->EGR = 1;
    TIM5->SR = 0;
    /* combined experiment (2026-10-06): step the PC0-PC2 selector once per burst (TIM5 update comes
       ~25 % of a period before the next trigger), PC4/PC5 driven HIGH, PB9/PB10 pulled-up inputs */
    sel_pos = 0;
    GPIOC->BSRR = 7u << 16;
    for (int pin = 4; pin <= 5; pin++) {
        GPIOC->BSRR = 1u << pin;
        GPIOC->MODER = (GPIOC->MODER & ~(3u << (2 * pin))) | (1u << (2 * pin));
    }
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    for (int pin = 9; pin <= 10; pin++) {
        GPIOB->MODER &= ~(3u << (2 * pin));
        GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << (2 * pin))) | (1u << (2 * pin));
    }
    TIM5->DIER = TIM_DIER_UIE;
    NVIC_SetPriority(TIM5_IRQn, 1);
    NVIC_EnableIRQ(TIM5_IRQn);
    TIM5->CR1 = 1;
    vel_mode = true;
}

void TIM5_IRQHandler(void)
{
    if (TIM5->SR & TIM_SR_UIF) {
        TIM5->SR = ~TIM_SR_UIF;
        sel_pos = (sel_pos + 1) & 7;
        GPIOC->BSRR = (7u << 16) | (uint32_t)sel_pos;
    }
}

static void vel_stop(void)
{
    TIM5->CR1 = 0;
    TIM5->DIER = 0;
    NVIC_DisableIRQ(TIM5_IRQn);
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    DMA2_Stream0->CR = 0;
    ADC1->SMPR2 = (4u << 0) | (4u << 3);
    ADC1->CR2 = ADC_CR2_ADON;
    vel_mode = false;
}

static void put12(uint8_t *r, int *n, int v)
{
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    r[(*n)++] = (uint8_t)(v >> 8 & 15); r[(*n)++] = (uint8_t)(v >> 4 & 15); r[(*n)++] = (uint8_t)(v & 15);
}

/* Per selector position: each burst (16 conversions) was taken at one position (burst slot b -> position
 * b % 8, since 256 slots is a multiple of 8). Average each burst's 8 PA1 samples, then per position report
 * the resting level (its oldest 4 bursts, ~4 ms before the press) and the peak over the window.
 *   F0 7D 1B <pad bit> <8 x baseline, 3 nibbles> <8 x peak, 3 nibbles> F7 */
static void vel_report(void)
{
    if (!tud_midi_mounted()) return;
    int head_burst = (VBUF - (int)DMA2_Stream0->NDTR) / 16;   /* oldest complete burst starts here */
    int base[8] = {0}, nb[8] = {0}, peak[8] = {0};
    for (int i = 0; i < VBUF / 16 - 1; i++) {      /* oldest -> newest, skipping the burst in progress */
        int b = (head_burst + 1 + i) % (VBUF / 16), p = b % 8, sum = 0;
        for (int j = 1; j < 16; j += 2) sum += vbuf[b * 16 + j];
        int v = sum / 8;
        if (nb[p] < 4) { base[p] += v; nb[p]++; }
        if (v > peak[p]) peak[p] = v;
    }
    uint8_t r[4 + 16 * 3 + 1];
    int n = 0;
    r[n++] = 0xF0; r[n++] = 0x7D; r[n++] = 0x1B; r[n++] = (uint8_t)vel_pad;
    for (int p = 0; p < 8; p++) put12(r, &n, nb[p] ? base[p] / nb[p] : 0);
    for (int p = 0; p < 8; p++) put12(r, &n, peak[p]);
    r[n++] = 0xF7;
    tud_midi_stream_write(0, r, (uint32_t)n);
}

static void pins_scan(void)
{
    pins_now[0] = (uint16_t)(GPIOA->IDR & PIN_MASK[0]);
    pins_now[1] = (uint16_t)(GPIOB->IDR & PIN_MASK[1]);
    pins_now[2] = (uint16_t)(GPIOC->IDR & PIN_MASK[2]);
    pins_now[3] = (uint16_t)(GPIOD->IDR & PIN_MASK[3]);
}

/* ---------- MIDI ---------- */

static uint8_t cin_len(uint8_t cin)
{
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    return len[cin & 0xF];
}

static uint8_t sx[80];
static unsigned sx_len;
static bool sx_on, dfu_requested, dirty = true, force_report = true;

static void sysex_done(void)
{
    static const uint8_t DFU[] = {0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7};
    if (sx_len == sizeof DFU && !memcmp(sx, DFU, sizeof DFU)) { dfu_requested = true; return; }
    if (sx_len < 4 || sx[1] != 0x7D) return;
    if (sx[2] == 0x11 && sx_len == 5) { latch_mode = sx[3] & 3; latch(1); force_report = dirty = true; }
    if (sx[2] == 0x12 && sx_len == 5) { spi_mode = sx[3] & 3; spi2_setup(); force_report = dirty = true; }
    if (sx[2] == 0x16 && sx_len == 6) {          /* F0 7D 16 <pin id> <level> F7: only pins stock drives as outputs */
        static GPIO_TypeDef *const port[] = {GPIOA, GPIOA, GPIOC, GPIOC};
        static const uint8_t pin[] = {2, 7, 4, 5};
        if (sx[3] < 4) port[sx[3]]->BSRR = sx[4] ? (1u << pin[sx[3]]) : (1u << (pin[sx[3]] + 16));
        if (sx[3] >= 2 && sx[3] < 4)              /* PC4/PC5: make them outputs as stock does */
            GPIOC->MODER = (GPIOC->MODER & ~(3u << (2 * pin[sx[3]]))) | (1u << (2 * pin[sx[3]]));
        force_report = dirty = true;
    }
    if (sx[2] == 0x01 && sx_len >= 4 && sx_len <= 4 + 21) {   /* F0 7D 01 <text> F7: OLED label */
        memcpy(label, &sx[3], sx_len - 4); label[sx_len - 4] = 0; dirty = true;
    }
    if (sx[2] == 0x17 && sx_len == 6) {          /* F0 7D 17 <k hi> <k lo> F7: start the LED walk at bit k */
        walk_k = (sx[3] << 7) | sx[4];
        walk_on = walk_k < OUT_BITS;
        walk_wait_release = true;
        GPIOA->BSRR = walk_on ? (1u << (7 + 16)) : (1u << 7);   /* PA7 low = LEDs enabled */
        walk_show();
    }
    if (sx[2] == 0x1A && sx_len == 5) {          /* F0 7D 1A <on> F7: velocity capture mode */
        if (sx[3]) vel_start(); else vel_stop();
        dirty = true;
    }
    if (sx[2] == 0x13 && sx_len == 4 + 2 * CHAIN) {
        for (int i = 0; i < CHAIN; i++) chain_out[i] = (uint8_t)((sx[3 + 2 * i] << 4) | (sx[4 + 2 * i] & 15));
        force_report = dirty = true;
    }
}

static void midi_byte(uint8_t b)
{
    if (b == 0xF0) { sx_on = true; sx_len = 0; }
    if (sx_on) {
        if (sx_len < sizeof sx) sx[sx_len++] = b;
        if (b == 0xF7) { sx_on = false; sysex_done(); }
    }
}

static void midi_task(void)
{
    if (!tud_midi_mounted()) return;
    uint8_t pkt[4];
    while (tud_midi_packet_read(pkt)) {
        uint8_t n = cin_len(pkt[0]);
        for (uint8_t i = 0; i < n; i++) midi_byte(pkt[1 + i]);
    }
}

static void report(void)
{
    if (!tud_midi_mounted()) return;
    uint8_t r[5 + 2 * CHAIN];
    r[0] = 0xF0; r[1] = 0x7D; r[2] = 0x10; r[3] = latch_mode | (uint8_t)(spi_mode << 2);
    for (int i = 0; i < CHAIN; i++) { r[4 + 2 * i] = chain_in[i] >> 4; r[5 + 2 * i] = chain_in[i] & 15; }
    r[4 + 2 * CHAIN] = 0xF7;
    tud_midi_stream_write(0, r, sizeof r);
}

static void report_analog(void)
{
    if (!tud_midi_mounted()) return;
    uint8_t r[4 + 16 * 3];
    r[0] = 0xF0; r[1] = 0x7D; r[2] = 0x15;
    for (int i = 0; i < 16; i++) {
        r[3 + 3 * i] = (uint8_t)(analog[i] >> 8 & 15);
        r[4 + 3 * i] = (uint8_t)(analog[i] >> 4 & 15);
        r[5 + 3 * i] = (uint8_t)(analog[i] & 15);
    }
    r[3 + 16 * 3] = 0xF7;
    tud_midi_stream_write(0, r, sizeof r);
}

static void report_pins(void)
{
    if (!tud_midi_mounted()) return;
    uint8_t r[4 + 16];
    r[0] = 0xF0; r[1] = 0x7D; r[2] = 0x14;
    for (int p = 0; p < 4; p++)
        for (int k = 0; k < 4; k++) r[3 + p * 4 + k] = (uint8_t)(pins_now[p] >> (12 - 4 * k) & 15);
    r[3 + 16] = 0xF7;
    tud_midi_stream_write(0, r, sizeof r);
}

/* ---------- screen ---------- */

static void hex2(char *o, uint8_t v) { static const char h[] = "0123456789ABCDEF"; o[0] = h[v >> 4]; o[1] = h[v & 15]; }

static void render(void)
{
    char line[24];
    draw_clear();
    draw_text(0, 0, "PROBE latch", 1);
    line[0] = (char)('0' + latch_mode); line[1] = ' '; line[2] = 's'; line[3] = 'p'; line[4] = 'i';
    line[5] = (char)('0' + spi_mode); line[6] = 0;
    draw_text(72, 0, line, 1);
    for (int row = 0; row < 3; row++) {             /* 29 bytes, 10 per row */
        int n = 0;
        for (int i = row * 10; i < row * 10 + 10 && i < CHAIN; i++) { hex2(line + n, chain_in[i]); n += 2; }
        line[n] = 0;
        draw_text(4, 12 + row * 9, line, 1);
    }
    if (last_byte >= 0) {
        memcpy(line, "byte    bit  ->", 16);
        line[5] = (char)('0' + last_byte / 10); line[6] = (char)('0' + last_byte % 10);
        line[12] = (char)('0' + last_bit);
        line[15] = (char)('0' + last_level); line[16] = 0;
        draw_text(4, 44, line, 1);
    }
    draw_text(4, 55, label, 1);
}

int main(void)
{
    SCB->VTOR = 0x08008000u;
    if (RCC->CSR & RCC_CSR_IWDGRSTF) { RCC->CSR |= RCC_CSR_RMVF; reboot_to_dfu(); }
    RCC->CSR |= RCC_CSR_RMVF;
    IWDG->KR = 0x5555; IWDG->PR = 4; IWDG->RLR = 1000; IWDG->KR = 0xCCCC; IWDG->KR = 0xAAAA;

    clock_init();
    SysTick_Config(84000u);
    usb_pins_init();
    NVIC_SetPriority(OTG_FS_IRQn, 2);
    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);

    oled_init();
    chain_init();
    analog_init();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;

    uint32_t next_scan = 0, next_report = 0, next_draw = 0, dfu_at = 0, next_analog = 0;
    for (;;) {
        IWDG->KR = 0xAAAA;
        tud_task();
        midi_task();
        if ((int32_t)(ms_ticks - next_scan) >= 0) {
            next_scan = ms_ticks + (vel_mode ? 1 : 5);
            chain_scan();
            for (int i = 0; i < CHAIN; i++) {
                uint8_t x = chain_in[i] ^ chain_prev[i];
                if (x) {
                    for (int b = 7; b >= 0; b--)
                        if (x >> b & 1) { last_byte = i; last_bit = b; last_level = chain_in[i] >> b & 1; }
                    force_report = dirty = true;
                }
            }
            if (walk_on) {
                if (walk_wait_release) {
                    if (inputs_released()) walk_wait_release = false;
                } else {
                    for (int i = 0; i <= 10 && walk_on; i++) {
                        uint8_t rise = chain_in[i] & (uint8_t)~chain_prev[i];
                        for (int b = 0; b < 8; b++) {
                            if (!(rise >> b & 1)) continue;
                            int in = i * 8 + b;
                            if (in == SELECT_PUSH) in = 0x3FFF;          /* "nothing lit" */
                            uint8_t r[] = {0xF0, 0x7D, 0x18, (uint8_t)(walk_k >> 7), (uint8_t)(walk_k & 127),
                                           (uint8_t)(in >> 7 & 127), (uint8_t)(in & 127), 0xF7};
                            if (tud_midi_mounted()) tud_midi_stream_write(0, r, sizeof r);
                            walk_k++;
                            walk_wait_release = true;
                            if (walk_k >= OUT_BITS) { walk_on = false; GPIOA->BSRR = 1u << 7; }
                            walk_show();
                            break;
                        }
                        if (walk_wait_release) break;
                    }
                }
            }
            if (vel_mode && vel_pad < 0)
                for (int i = 0; i <= 7; i++) {
                    uint8_t rise = chain_in[i] & (uint8_t)~chain_prev[i];
                    if (rise) { vel_pad = i * 8 + __builtin_ctz(rise); vel_at = ms_ticks + 8; break; }
                }
            memcpy(chain_prev, chain_in, CHAIN);
            pins_scan();
            if (memcmp(pins_now, pins_prev, sizeof pins_now)) { report_pins(); memcpy(pins_prev, pins_now, sizeof pins_now); }
        }
        if (vel_pad >= 0 && (int32_t)(ms_ticks - vel_at) >= 0) { vel_report(); vel_pad = -1; }
        if (!vel_mode && (int32_t)(ms_ticks - next_analog) >= 0) {   /* analog: 20 per second */
            next_analog = ms_ticks + 50;
            analog_scan();
            report_analog();
        }
        if (force_report || (int32_t)(ms_ticks - next_report) >= 0) {
            report();
            force_report = false;
            next_report = ms_ticks + 2000;
        }
        if (dirty && (int32_t)(ms_ticks - next_draw) >= 0) {
            render();
            oled_flush();
            dirty = false;
            next_draw = ms_ticks + 40;              /* at most 25 redraws/s */
        }
        if (dfu_requested && !dfu_at) dfu_at = ms_ticks + 50;
        if (dfu_at && (int32_t)(ms_ticks - dfu_at) >= 0) reboot_to_dfu();
    }
}
