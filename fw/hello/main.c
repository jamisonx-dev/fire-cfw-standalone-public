/* "hello": the first custom Fire firmware that speaks USB MIDI.
 *
 * - Shows up as "ORC FW hello" (1209:0001), one MIDI port.
 * - Announces itself on connect: F0 7D 4F 52 43 01 F7 (7D = non-commercial ID, "ORC", v1).
 * - Echoes every MIDI packet it receives back to the host.
 * - SysEx F0 47 7F 43 72 00 00 F7 (same as the stock Fire) = reboot into ST ROM DFU.
 *
 * Safety nets so a bug can never strand the Fire remotely:
 * - Independent watchdog (~2 s) starts first thing; the main loop feeds it.
 * - If the last reset came from the watchdog, go straight to DFU instead of running.
 * - Every fault handler goes to DFU.
 */
#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx.h"
#include "tusb.h"

static volatile uint32_t ms_ticks;

void SysTick_Handler(void) { ms_ticks++; }
uint32_t tusb_time_millis_api(void) { return ms_ticks; }
void OTG_FS_IRQHandler(void) { tusb_int_handler(0, true); }

static void __attribute__((noreturn)) reboot_to_dfu(void)
{
    __disable_irq();
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;          /* backup registers writable */
    RTC->BKP0R = 1u;                /* Akai bootloader: bit0 -> jump to ST ROM DFU */
    NVIC_SystemReset();
}

/* Any crash -> DFU. */
void HardFault_Handler(void)  { reboot_to_dfu(); }
void MemManage_Handler(void)  { reboot_to_dfu(); }
void BusFault_Handler(void)   { reboot_to_dfu(); }
void UsageFault_Handler(void) { reboot_to_dfu(); }

static void watchdog_start(void)
{
    IWDG->KR = 0x5555;              /* unlock PR/RLR */
    IWDG->PR = 4;                   /* LSI ~32 kHz / 64 = 500 Hz */
    IWDG->RLR = 1000;               /* ~2 s */
    IWDG->KR = 0xCCCC;              /* start */
    IWDG->KR = 0xAAAA;
}

static inline void watchdog_feed(void) { IWDG->KR = 0xAAAA; }

/* 8 MHz crystal -> PLL -> 84 MHz core, 48 MHz USB (same as the stock firmware). */
static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}        /* watchdog covers a dead crystal */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;   /* scale 2, fine for 84 MHz */
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    RCC->PLLCFGR = 0x07415408u;     /* M=8 N=336 P=4 Q=7, source HSE: the stock value */
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
    /* PA11 = D-, PA12 = D+, alternate function 10 (OTG_FS), very high speed */
    GPIOA->MODER   = (GPIOA->MODER & ~(0xFu << 22)) | (0xAu << 22);
    GPIOA->OSPEEDR |= 0xFu << 22;
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~(0xFFu << 12)) | (0xAAu << 12);
    RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
    (void)RCC->AHB2ENR;
}

/* Watches the incoming byte stream for the stock "reboot to DFU" SysEx. */
static const uint8_t DFU_SYSEX[] = {0xF0, 0x47, 0x7F, 0x43, 0x72, 0x00, 0x00, 0xF7};
static uint8_t dfu_match;
static bool dfu_requested;

static void scan_byte(uint8_t b)
{
    if (b == DFU_SYSEX[dfu_match]) {
        if (++dfu_match == sizeof DFU_SYSEX) { dfu_requested = true; dfu_match = 0; }
    } else {
        dfu_match = (b == DFU_SYSEX[0]) ? 1 : 0;
    }
}

/* Number of MIDI bytes carried by a USB-MIDI packet, by Code Index Number. */
static uint8_t cin_len(uint8_t cin)
{
    static const uint8_t len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    return len[cin & 0xF];
}

static bool announced;

static void midi_task(void)
{
    if (!tud_midi_mounted()) { announced = false; return; }
    if (!announced) {
        static const uint8_t hello[] = {0xF0, 0x7D, 'O', 'R', 'C', 0x01, 0xF7};
        tud_midi_stream_write(0, hello, sizeof hello);
        announced = true;
    }
    uint8_t pkt[4];
    while (tud_midi_packet_read(pkt)) {
        uint8_t n = cin_len(pkt[0]);
        for (uint8_t i = 0; i < n; i++) scan_byte(pkt[1 + i]);
        tud_midi_packet_write(pkt);          /* echo */
    }
}

int main(void)
{
    SCB->VTOR = 0x08008000u;        /* our vector table (Akai's bootloader owns 0x08000000) */

    if (RCC->CSR & RCC_CSR_IWDGRSTF) {      /* last run froze: don't run again, go to DFU */
        RCC->CSR |= RCC_CSR_RMVF;
        reboot_to_dfu();
    }
    RCC->CSR |= RCC_CSR_RMVF;
    watchdog_start();

    clock_init();
    SysTick_Config(84000u);         /* 1 ms */
    usb_pins_init();
    NVIC_SetPriority(OTG_FS_IRQn, 2);

    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    tusb_init(0, &dev_init);

    uint32_t dfu_at = 0;
    for (;;) {
        watchdog_feed();
        tud_task();
        midi_task();
        if (dfu_requested && !dfu_at) dfu_at = ms_ticks + 50;   /* let the echo go out first */
        if (dfu_at && (int32_t)(ms_ticks - dfu_at) >= 0) reboot_to_dfu();
    }
}
