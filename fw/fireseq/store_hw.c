/* Flash access for store.c on the Fire: sector 5 only (0x08020000-0x0803FFFF).
 * Akai's bootloader (sector 0), settings (sector 1) and our app (sectors 2-4) are never touched. */
#include "stm32f4xx.h"
#include "store.h"

#define AREA 0x08020000u
const uint8_t *store_area = (const uint8_t *)AREA;

static void unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu; }
}
static void lock(void) { FLASH->CR |= FLASH_CR_LOCK; }
static bool wait_ok(void)
{
    while (FLASH->SR & FLASH_SR_BSY) {}
    uint32_t err = FLASH->SR & (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR);
    FLASH->SR = err | FLASH_SR_EOP;
    return err == 0;
}

bool store_hw_erase(void)
{
    /* The chip stalls (~1-2 s) while erasing 128 KB. Give the watchdog ~8 s for it. A new reload
       value only takes effect once the watchdog has taken it in (RVU clears, a few 32 kHz ticks):
       reloading before that would still count from the old ~2 s. */
    IWDG->KR = 0x5555; IWDG->RLR = 4095;
    while (IWDG->SR & IWDG_SR_RVU) {}
    IWDG->KR = 0xAAAA;
    unlock();
    wait_ok();
    FLASH->CR = (2u << FLASH_CR_PSIZE_Pos) | FLASH_CR_SER | (5u << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    bool ok = wait_ok();
    FLASH->CR = 0;
    lock();
    IWDG->KR = 0x5555; IWDG->RLR = 1000;
    while (IWDG->SR & IWDG_SR_RVU) {}
    IWDG->KR = 0xAAAA;
    return ok;
}

bool store_hw_program(uint32_t offset, const uint32_t *words, uint32_t n)
{
    unlock();
    wait_ok();
    FLASH->CR = (2u << FLASH_CR_PSIZE_Pos) | FLASH_CR_PG;
    bool ok = true;
    volatile uint32_t *dst = (volatile uint32_t *)(AREA + offset);
    for (uint32_t i = 0; i < n && ok; i++) {
        dst[i] = words[i];
        ok = wait_ok();
    }
    FLASH->CR = 0;
    lock();
    return ok;
}
