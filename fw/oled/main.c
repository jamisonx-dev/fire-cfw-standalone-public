/* "oled": hello + the Fire's OLED.
 *
 * USB MIDI device "ORC FW oled" (1209:0001):
 *   - announce on connect:  F0 7D 4F 52 43 02 F7   ("ORC", v2)
 *   - echoes every packet back (as hello)
 *   - F0 47 7F 43 72 00 00 F7 = reboot to ST ROM DFU (as stock)
 *   - F0 7D 01 <line 0-1> <ASCII...> F7 = set a text line on the screen
 *   - F0 7D 02 F7 = status reply: F0 7D 02 <frames pushed, 4x7 bits> F7
 * Screen: "ORC-1 custom firmware", uptime, last MIDI message, the two host text lines.
 * Safety nets as hello: watchdog (~2 s), watchdog-reset -> DFU, faults -> DFU.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32f4xx.h"
#include "tusb.h"
#include "draw.h"
#include "screen.h"
#include "oled_hw.h"

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
    RTC->BKP0R = 1u;                /* Akai bootloader: bit0 -> ST ROM DFU */
    NVIC_SystemReset();
}

void HardFault_Handler(void)  { reboot_to_dfu(); }
void MemManage_Handler(void)  { reboot_to_dfu(); }
void BusFault_Handler(void)   { reboot_to_dfu(); }
void UsageFault_Handler(void) { reboot_to_dfu(); }

static void watchdog_start(void)
{
    IWDG->KR = 0x5555; IWDG->PR = 4; IWDG->RLR = 1000;   /* ~2 s */
    IWDG->KR = 0xCCCC; IWDG->KR = 0xAAAA;
}

static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->PLLCFGR = 0x07415408u;     /* stock: 8 MHz HSE -> 84 MHz, 48 MHz USB */
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

/* ---------- incoming MIDI ---------- */

static uint8_t cin_len(uint8_t cin)
{
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    return len[cin & 0xF];
}

static uint8_t sx[64];              /* current SysEx, F0..F7 */
static unsigned sx_len;
static bool sx_on;
static bool dfu_requested, status_requested, dirty = true;
static char last_midi[16] = "-";
static uint32_t frames;

static void hex2(char *o, uint8_t v) { static const char h[] = "0123456789ABCDEF"; o[0] = h[v >> 4]; o[1] = h[v & 15]; }

static void sysex_done(void)
{
    static const uint8_t DFU[] = {0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7};
    if (sx_len == sizeof DFU && !memcmp(sx, DFU, sizeof DFU)) { dfu_requested = true; return; }
    if (sx_len >= 4 && sx[1] == 0x7D && sx[2] == 0x01 && sx[3] < SCREEN_TEXT_LINES) {
        unsigned n = sx_len - 5;    /* F0 7D 01 line ... F7 */
        if (n > SCREEN_TEXT_COLS) n = SCREEN_TEXT_COLS;
        memcpy(screen_lines[sx[3]], &sx[4], n);
        screen_lines[sx[3]][n] = 0;
        dirty = true;
    } else if (sx_len == 4 && sx[1] == 0x7D && sx[2] == 0x02) {
        status_requested = true;
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

static bool announced;

static void midi_task(void)
{
    if (!tud_midi_mounted()) { announced = false; return; }
    if (!announced) {
        static const uint8_t hello[] = {0xF0, 0x7D, 'O', 'R', 'C', 0x02, 0xF7};
        tud_midi_stream_write(0, hello, sizeof hello);
        announced = true;
    }
    uint8_t pkt[4];
    while (tud_midi_packet_read(pkt)) {
        uint8_t n = cin_len(pkt[0]);
        for (uint8_t i = 0; i < n; i++) midi_byte(pkt[1 + i]);
        uint8_t cin = pkt[0] & 0xF;
        if (cin >= 0x8 && cin <= 0xE) {          /* channel message: show it */
            char *o = last_midi;
            for (uint8_t i = 0; i < n; i++) { hex2(o, pkt[1 + i]); o[2] = ' '; o += 3; }
            o[-1] = 0;
            dirty = true;
        }
        tud_midi_packet_write(pkt);               /* echo */
    }
    if (status_requested) {
        status_requested = false;
        uint8_t r[] = {0xF0, 0x7D, 0x02, (uint8_t)(frames >> 21 & 0x7F), (uint8_t)(frames >> 14 & 0x7F),
                       (uint8_t)(frames >> 7 & 0x7F), (uint8_t)(frames & 0x7F), 0xF7};
        tud_midi_stream_write(0, r, sizeof r);
    }
}

int main(void)
{
    SCB->VTOR = 0x08008000u;
    if (RCC->CSR & RCC_CSR_IWDGRSTF) { RCC->CSR |= RCC_CSR_RMVF; reboot_to_dfu(); }
    RCC->CSR |= RCC_CSR_RMVF;
    watchdog_start();

    clock_init();
    SysTick_Config(84000u);
    usb_pins_init();
    NVIC_SetPriority(OTG_FS_IRQn, 2);
    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);

    oled_init();

    unsigned shown_s = ~0u;
    uint32_t dfu_at = 0;
    for (;;) {
        IWDG->KR = 0xAAAA;
        tud_task();
        midi_task();
        unsigned s = ms_ticks / 1000;
        if (dirty || s != shown_s) {
            screen_render(s, last_midi);
            oled_flush();
            frames++;
            shown_s = s;
            dirty = false;
        }
        if (dfu_requested && !dfu_at) dfu_at = ms_ticks + 50;
        if (dfu_at && (int32_t)(ms_ticks - dfu_at) >= 0) reboot_to_dfu();
    }
}
