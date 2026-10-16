/* "velprobe": reproduce Akai's pad-velocity MEASUREMENT chain, as read from the stock image
 * (see ../VELOCITY_NOTES.md, "Akai's velocity code").  Nothing here is a finished feature: it exists
 * to answer one question, "do we already see Akai's signal, just on a x16 scale?".
 *
 * What it copies from the stock firmware:
 *   - TIM5 CC1 at 8 kHz triggers ADC1: a 16-conversion scan alternating PA0/PA1, 15-cycle sampling,
 *     into a circular DMA buffer; the DMA transfer-complete interrupt fires once per burst.
 *   - Per burst: the 8 PA1 samples are summed into the slot of the CURRENT selector code, then the
 *     selector (PC0-PC2, push-pull, very high speed) moves to the next code of the Gray sequence
 *     0,1,3,2,6,7,5,4.  Slots are indexed by CODE (as the stock driver does), not by time step.
 *   - Once per 8 bursts (one "frame", ~1 ms): slot value = sum * 65535 / (8 * 4095)  (= the average x16,
 *     the stock driver's "mode 1"), pushed through a 2-frame history and the 8-tap Q15 smoothing FIR
 *     {-672,-1208,4310,13910,13910,4310,-1208,-672}  -> 8 filtered values y[0..7] (16-bit scale).
 *   - Stock thresholds on that scale: a peak counts once y[1] > 280; velocity = curve[(clamp(peak,280,800)-280)
 *     *127/520 (min 1) - 1] using the 127-entry curve from Akai's settings sector (copied below).
 *
 * What it does with it: whenever a pad is pressed (chain bit rises, bytes 0-7), it watches ~30 ms of
 * frames and then sends  F0 7D 4F 52 43 1C <pad bit> <rest 4 nibbles> <peak 4> <peak1 4> <vel 2> <nframes 2> F7
 *   rest  = mean of all y[] over the 4 frames before the press;
 *   peak  = max of any y[j] in the window; peak1 = max of y[1] only (the stock gate);
 *   vel   = what Akai's mapping gives for the gated peak (a frame's y[j] counts only if that frame's y[1] > 280;
 *           no gated peak -> 0 -> the floor, 32).  Not reproduced: the stock "repeat hit within 400 ms reuses the
 *           last velocity" shortcut and its fixed-velocity setting, so every press here is measured fresh.
 * Also F0 7D 4F 52 43 1D <8 x 4-nibble slot averages> F7 about 5x/s while idle (the resting level per code).
 *
 * Commands: F0 7D 1A <on> F7 start/stop measuring;  F0 7D 16 <id> <level> F7 drive a stock output
 * (0 PA2, 1 PA7 [LED drivers, low = on], 2 PC4, 3 PC5);  F0 7D 13 <58 nibbles> F7 chain outputs (LEDs);
 * F0 7D 01 <text> F7 OLED label;  F0 47 7F 43 72 00 00 F7 reboot to update mode.
 * Safety nets as the other probes: watchdog, watchdog-reset -> DFU, faults -> DFU.
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

/* ---------- the button / LED chain (same as probe) ---------- */

static uint8_t chain_out[CHAIN], chain_in[CHAIN], chain_prev[CHAIN];

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
    GPIOB->BSRR = 1u << 12;                         /* PB12 latch output, high (the stock idle) */
    GPIOB->MODER = (GPIOB->MODER & ~(3u << 24)) | (1u << 24);
    SPI2->CR1 = 0;
    SPI2->CR1 = SPI_CR1_MSTR | (2u << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;   /* /8 = 5.25 MHz, as stock */
    SPI2->CR1 |= SPI_CR1_SPE;
}

static inline void latch(int level) { GPIOB->BSRR = level ? (1u << 12) : (1u << 28); }
static void short_wait(void) { for (volatile int i = 0; i < 40; i++) {} }   /* ~2 us */

static void chain_scan(void)
{
    latch(0); short_wait(); latch(1); short_wait();
    while (SPI2->SR & SPI_SR_RXNE) (void)SPI2->DR;
    for (int i = 0; i < CHAIN; i++) {
        while (!(SPI2->SR & SPI_SR_TXE)) {}
        *(volatile uint8_t *)&SPI2->DR = chain_out[i];
        while (!(SPI2->SR & SPI_SR_RXNE)) {}
        chain_in[i] = *(volatile uint8_t *)&SPI2->DR;
    }
    while (SPI2->SR & SPI_SR_BSY) {}
}

/* ---------- Akai's ADC driver, reproduced ---------- */

/* Akai's settings-sector curve (127 entries, read from the stock dump at 0x08004015). */
static const uint8_t CURVE[127] = {
    32, 33, 34, 35, 36, 36, 37, 38, 39, 39, 40, 41, 42, 42, 43, 44, 45, 45, 46, 47, 48, 48, 49, 50, 51, 51, 52, 53,
    54, 54, 55, 56, 56, 57, 58, 59, 59, 60, 61, 62, 62, 63, 64, 65, 65, 66, 67, 68, 68, 69, 70, 71, 71, 72, 73, 74,
    74, 75, 76, 77, 77, 78, 79, 80, 80, 81, 82, 82, 83, 84, 85, 85, 86, 87, 88, 88, 89, 90, 91, 91, 92, 93, 94, 94,
    95, 96, 97, 97, 98, 99, 100, 100, 101, 102, 103, 103, 104, 105, 105, 106, 107, 108, 108, 109, 110, 111, 111,
    112, 113, 114, 114, 115, 116, 117, 117, 118, 119, 120, 120, 121, 122, 123, 123, 124, 125, 126, 127
};
static const uint8_t GRAY[8] = {0, 1, 3, 2, 6, 7, 5, 4};          /* stock table at 0x08016A1C */
static const int32_t FIR[8] = {-672, -1208, 4310, 13910, 13910, 4310, -1208, -672};   /* stock 0x08015950 */

#define BURST 16
static uint16_t vbuf[BURST];                     /* DMA target, circular: one burst */
static volatile uint32_t slot_sum[8];            /* PA1 sum of the 8 samples of the burst taken at code c */
static volatile uint32_t slot_pa0[8];            /* PA0 sum, kept for the idle report */
static volatile uint32_t frames;                 /* ++ every time the sequence wraps to step 0 */
static volatile uint8_t step;                    /* position in the Gray sequence */
static bool meas_on;

void DMA2_Stream0_IRQHandler(void)
{
    if (DMA2->LISR & DMA_LISR_TCIF0) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0;
        uint8_t code = GRAY[step];
        uint32_t a = 0, b = 0;
        for (int j = 0; j < BURST; j += 2) { a += vbuf[j]; b += vbuf[j + 1]; }
        slot_pa0[code] = a;
        slot_sum[code] = b;
        step = (uint8_t)((step + 1) & 7);
        GPIOC->BSRR = (7u << 16) | GRAY[step];    /* next burst's selector code */
        if (step == 0) frames++;
    }
}

static void meas_start(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN | RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;
    GPIOA->MODER |= 0xFu;                            /* PA0, PA1 analog, no pull */
    GPIOA->PUPDR &= ~0xFu;
    for (int pin = 0; pin <= 2; pin++) {             /* PC0-PC2: push-pull output, no pull, very high speed, low */
        GPIOC->MODER   = (GPIOC->MODER   & ~(3u << (2 * pin))) | (1u << (2 * pin));
        GPIOC->OTYPER &= ~(1u << pin);
        GPIOC->PUPDR  &= ~(3u << (2 * pin));
        GPIOC->OSPEEDR |= 3u << (2 * pin);
    }
    GPIOC->BSRR = 7u << 16;
    step = 0; frames = 0;
    ADC->CCR = 1u << ADC_CCR_ADCPRE_Pos;             /* 84 / 4 = 21 MHz */
    ADC1->CR2 = 0;
    DMA2_Stream0->CR = 0;
    while (DMA2_Stream0->CR & DMA_SxCR_EN) {}
    DMA2->LIFCR = 0x3Du;
    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)vbuf;
    DMA2_Stream0->NDTR = BURST;
    DMA2_Stream0->CR = DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 | DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_TCIE;
    DMA2_Stream0->CR |= DMA_SxCR_EN;
    ADC1->CR1 = ADC_CR1_SCAN;
    ADC1->SQR1 = 0x00F08020u;                        /* 16 conversions: ch0, ch1, ch0, ch1, ... */
    ADC1->SQR2 = 0x02008020u;
    ADC1->SQR3 = 0x02008020u;
    ADC1->SMPR2 = 0x9u;                              /* ch0, ch1: 15 cycles */
    ADC1->CR2 = 0x1A000301u;                         /* ADON | DMA | DDS | rising edge of TIM5_CC1 (stock value) */
    TIM5->CR1 = 0;
    TIM5->PSC = 0;
    TIM5->ARR = 10500;
    TIM5->CCR1 = 0x1EC3;
    TIM5->CCMR1 = 0x68;
    TIM5->CCER = 0x3;
    TIM5->EGR = 1;
    TIM5->SR = 0;
    NVIC_SetPriority(DMA2_Stream0_IRQn, 1);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    TIM5->CR1 = 1;
    meas_on = true;
}

static void meas_stop(void)
{
    TIM5->CR1 = 0;
    NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;
    DMA2_Stream0->CR = 0;
    meas_on = false;
}

/* ---------- the stock main-loop measurement ---------- */

static uint32_t hist[16];                        /* 2-frame history exactly as the stock loop keeps it */
static uint16_t y[8];                            /* filtered values of the latest frame */
static uint16_t rest_ring[4];                    /* mean of y[] for the last 4 frames */
static int rest_i;

static int akai_velocity(unsigned peak)          /* stock 0x08011B0A, with the fixed-velocity override left out */
{
    unsigned x = peak < 0x118 ? 0x118 : (peak > 0x320 ? 0x320 : peak);
    unsigned idx = (x - 0x118) * 127u / 0x208u;
    if (idx == 0) idx = 1;
    return CURVE[idx - 1];
}

static void process_frame(void)
{
    uint16_t s[8];
    for (int i = 0; i < 8; i++) {
        uint32_t sum = slot_sum[i];
        s[i] = (uint16_t)((uint64_t)sum * 0xFFFFu / (8u * 4095u));       /* average x16 (stock "mode 1") */
    }
    for (int i = 0; i < 8; i++) {                 /* stock history shuffle: H[15-i] = H[7-i]; H[7-i] = s[i] */
        hist[15 - i] = hist[7 - i];
        hist[7 - i] = s[i];
    }
    for (int j = 0; j < 8; j++) {                 /* 8-tap FIR, Q15, +0x4000 rounding, 0 if not positive */
        int64_t acc = 0x4000;
        for (int k = 0; k < 8; k++) acc += (int64_t)FIR[k] * (int64_t)hist[7 + k - j];
        y[j] = acc > 0 ? (uint16_t)((acc >> 15) & 0xFFFF) : 0;
    }
    uint32_t m = 0;
    for (int j = 0; j < 8; j++) m += y[j];
    rest_ring[rest_i] = (uint16_t)(m / 8);
    rest_i = (rest_i + 1) & 3;
}

/* ---------- MIDI ---------- */

static uint8_t cin_len(uint8_t cin)
{
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    return len[cin & 0xF];
}

static uint8_t sx[80];
static unsigned sx_len;
static bool sx_on, dfu_requested, dirty = true;
static char label[22] = "velprobe: hit pads";

static void sysex_done(void)
{
    static const uint8_t DFU[] = {0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7};
    if (sx_len == sizeof DFU && !memcmp(sx, DFU, sizeof DFU)) { dfu_requested = true; return; }
    if (sx_len < 4 || sx[1] != 0x7D) return;
    if (sx[2] == 0x16 && sx_len == 6) {          /* drive a pin the stock firmware drives */
        static GPIO_TypeDef *const port[] = {GPIOA, GPIOA, GPIOC, GPIOC};
        static const uint8_t pin[] = {2, 7, 4, 5};
        if (sx[3] < 4) {
            port[sx[3]]->BSRR = sx[4] ? (1u << pin[sx[3]]) : (1u << (pin[sx[3]] + 16));
            port[sx[3]]->MODER = (port[sx[3]]->MODER & ~(3u << (2 * pin[sx[3]]))) | (1u << (2 * pin[sx[3]]));
        }
    }
    if (sx[2] == 0x01 && sx_len >= 4 && sx_len <= 4 + 21) {
        memcpy(label, &sx[3], sx_len - 4); label[sx_len - 4] = 0; dirty = true;
    }
    if (sx[2] == 0x1A && sx_len == 5) { if (sx[3]) meas_start(); else meas_stop(); dirty = true; }
    if (sx[2] == 0x13 && sx_len == 4 + 2 * CHAIN)
        for (int i = 0; i < CHAIN; i++) chain_out[i] = (uint8_t)((sx[3 + 2 * i] << 4) | (sx[4 + 2 * i] & 15));
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

static void put16(uint8_t *r, int *n, unsigned v)
{
    r[(*n)++] = (uint8_t)(v >> 12 & 15); r[(*n)++] = (uint8_t)(v >> 8 & 15);
    r[(*n)++] = (uint8_t)(v >> 4 & 15);  r[(*n)++] = (uint8_t)(v & 15);
}

static char last1[22], last2[22];
static void hex4(char *o, unsigned v) { static const char h[] = "0123456789ABCDEF"; for (int i = 0; i < 4; i++) o[i] = h[v >> (12 - 4 * i) & 15]; }

/* ---------- screen ---------- */

static void render(void)
{
    draw_clear();
    draw_text(0, 0, "VELPROBE", 1);
    draw_text(60, 0, meas_on ? "on" : "off", 1);
    draw_text(4, 14, last1, 1);
    draw_text(4, 26, last2, 1);
    char line[24];
    memcpy(line, "now ", 4);
    hex4(line + 4, y[1]); line[8] = ' '; hex4(line + 9, y[0]); line[13] = 0;
    draw_text(4, 38, line, 1);
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
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;   /* C: so SysEx 0x16 works before measuring starts */

    uint32_t next_scan = 0, next_draw = 0, dfu_at = 0, next_idle = 0, seen = 0;
    int pad = -1;                                 /* pad bit being observed, -1 = none */
    uint32_t t_end = 0, nframes = 0;
    unsigned rest = 0, peak = 0, peak1 = 0, gpeak = 0;
    for (;;) {
        IWDG->KR = 0xAAAA;
        tud_task();
        midi_task();

        if (meas_on && frames != seen) {          /* a new frame of 8 bursts is complete */
            seen = frames;
            process_frame();
            if (pad >= 0) {
                nframes++;
                for (int j = 0; j < 8; j++) {
                    if (y[j] > peak) peak = y[j];
                    if (j == 1 && y[j] > peak1) peak1 = y[j];
                    if (y[j] > gpeak && y[1] > 0x118) gpeak = y[j];   /* stock rule: same frame's y[1] must pass 280 */
                }
            }
        }

        if ((int32_t)(ms_ticks - next_scan) >= 0) {
            next_scan = ms_ticks + 1;
            chain_scan();
            if (pad < 0 && meas_on)
                for (int i = 0; i <= 7; i++) {
                    uint8_t rise = chain_in[i] & (uint8_t)~chain_prev[i];
                    if (rise) {
                        pad = i * 8 + __builtin_ctz(rise);
                        t_end = ms_ticks + 30;
                        uint32_t m = 0;
                        for (int k = 0; k < 4; k++) m += rest_ring[k];
                        rest = m / 4; peak = 0; peak1 = 0; gpeak = 0; nframes = 0;
                        break;
                    }
                }
            memcpy(chain_prev, chain_in, CHAIN);
        }

        if (pad >= 0 && (int32_t)(ms_ticks - t_end) >= 0) {
            unsigned vel = akai_velocity(gpeak);          /* 0 if the gate never opened -> floor, like a stock soft tap */
            uint8_t r[3 + 4 + 4 * 3 + 4 + 1];
            int n = 0;
            r[n++] = 0xF0; r[n++] = 0x7D; r[n++] = 'O'; r[n++] = 'R'; r[n++] = 'C';   /* brain drops 7D 'ORC' (no DIN leak) */
            r[n++] = 0x1C; r[n++] = (uint8_t)pad;
            put16(r, &n, rest); put16(r, &n, peak); put16(r, &n, peak1);
            r[n++] = (uint8_t)(vel >> 4 & 15); r[n++] = (uint8_t)(vel & 15);
            r[n++] = (uint8_t)(nframes >> 4 & 15); r[n++] = (uint8_t)(nframes & 15);
            r[n++] = 0xF7;
            if (tud_midi_mounted()) tud_midi_stream_write(0, r, (uint32_t)n);
            memcpy(last2, last1, sizeof last2);
            /* "rNNNN pNNNN vNNN": hex rest and peak (16-bit scale), decimal velocity */
            last1[0] = 'r'; hex4(last1 + 1, rest); last1[5] = ' '; last1[6] = 'p'; hex4(last1 + 7, peak);
            last1[11] = ' '; last1[12] = 'v';
            last1[13] = (char)('0' + vel / 100 % 10); last1[14] = (char)('0' + vel / 10 % 10); last1[15] = (char)('0' + vel % 10);
            last1[16] = 0;
            pad = -1; dirty = true;
        }

        if (meas_on && pad < 0 && (int32_t)(ms_ticks - next_idle) >= 0) {   /* idle: resting level per code, ~5/s */
            next_idle = ms_ticks + 200;
            if (tud_midi_mounted()) {
                uint8_t r[3 + 3 + 8 * 4 + 1];
                int n = 0;
                r[n++] = 0xF0; r[n++] = 0x7D; r[n++] = 'O'; r[n++] = 'R'; r[n++] = 'C'; r[n++] = 0x1D;
                for (int i = 0; i < 8; i++) {
                    uint32_t sm = slot_sum[i];
                    put16(r, &n, (unsigned)((uint64_t)sm * 0xFFFFu / (8u * 4095u)));
                }
                r[n++] = 0xF7;
                tud_midi_stream_write(0, r, (uint32_t)n);
            }
            dirty = true;
        }

        if (dirty && (int32_t)(ms_ticks - next_draw) >= 0) {
            render();
            oled_flush();
            dirty = false;
            next_draw = ms_ticks + 100;
        }
        if (dfu_requested && !dfu_at) dfu_at = ms_ticks + 50;
        if (dfu_at && (int32_t)(ms_ticks - dfu_at) >= 0) reboot_to_dfu();
    }
}
