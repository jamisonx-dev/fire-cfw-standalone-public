/* Boomerang: the first custom Fire firmware.
 *
 * Runs from 0x08008000 (Akai's bootloader stays at 0x08000000), waits ~3 s,
 * then asks the bootloader for ST's ROM DFU and resets. If the Fire shows up
 * in DFU mode a few seconds after booting this, our code ran.
 *
 * No USB, no pins touched. Bare registers, no libraries.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define RCC_APB1ENR  REG(0x40023840)
#define PWR_CR       REG(0x40007000)
#define RTC_BKP0R    REG(0x40002850)   /* bootloader's flag: bit0 = go to ROM DFU */
#define RTC_BKP1R    REG(0x40002854)   /* ours: "boomerang ran" marker */
#define SCB_AIRCR    REG(0xE000ED0C)

extern uint32_t _estack;
void reset_handler(void);
static void hang(void) { for (;;) {} }

__attribute__((section(".vectors"), used))
const void *vectors[16] = {
    &_estack, reset_handler, hang, hang, hang, hang, hang, 0, 0, 0, 0, hang, hang, 0, hang, hang,
};

/* The bootloader only starts us if this sits at 0x0801FFF0. */
__attribute__((section(".token"), used))
const char token[16] = "EndOfFlashToken";

static void reboot_to_dfu(void)
{
    RCC_APB1ENR |= 1u << 28;          /* PWR clock on */
    (void)RCC_APB1ENR;
    PWR_CR |= 1u << 8;                /* DBP: backup registers writable */
    RTC_BKP1R = 0xB00E0001u;
    RTC_BKP0R = 1u;                   /* bootloader -> jump to 0x1FFF0000 */
    __asm volatile ("dsb");
    SCB_AIRCR = 0x05FA0004u;          /* system reset */
    __asm volatile ("dsb");
    for (;;) {}
}

void reset_handler(void)
{
    /* After the bootloader's reset we run on the 16 MHz internal clock.
       ~4 cycles per loop -> about 3 seconds. */
    for (volatile uint32_t i = 0; i < 12000000u; i++) {}
    reboot_to_dfu();
}
