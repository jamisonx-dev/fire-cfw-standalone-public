/* SSD1306 over SPI3 on the Fire (pins and init sequence taken from the stock firmware).
 *   PC10 SCK, PC12 MOSI (AF6), PC11 D/C (low = command), PC13 CS (active low), PD2 RESET (active low).
 *   PA2 / PA7 / PB12 are driven high before the reset pulse, as the stock firmware does. */
#include "stm32f4xx.h"
#include "draw.h"
#include "oled_hw.h"

extern volatile uint32_t ms_ticks;

static void delay_ms(uint32_t ms)
{
    uint32_t t0 = ms_ticks;
    while (ms_ticks - t0 < ms) { IWDG->KR = 0xAAAA; }
}

static void pin_out(GPIO_TypeDef *g, int pin, int level)
{
    g->BSRR = level ? (1u << pin) : (1u << (pin + 16));
    g->MODER = (g->MODER & ~(3u << (2 * pin))) | (1u << (2 * pin));
    g->OSPEEDR |= 3u << (2 * pin);
}

static void pin_af(GPIO_TypeDef *g, int pin, int af)
{
    g->MODER = (g->MODER & ~(3u << (2 * pin))) | (2u << (2 * pin));
    g->OSPEEDR |= 3u << (2 * pin);
    volatile uint32_t *afr = &g->AFR[pin >> 3];
    *afr = (*afr & ~(0xFu << (4 * (pin & 7)))) | ((uint32_t)af << (4 * (pin & 7)));
}

#define DC(l)  (GPIOC->BSRR = (l) ? (1u << 11) : (1u << 27))
#define CS(l)  (GPIOC->BSRR = (l) ? (1u << 13) : (1u << 29))

static void spi_send(const uint8_t *p, unsigned n, int data)
{
    DC(data);
    CS(0);
    while (n--) {
        while (!(SPI3->SR & SPI_SR_TXE)) {}
        *(volatile uint8_t *)&SPI3->DR = *p++;
    }
    while (!(SPI3->SR & SPI_SR_TXE)) {}
    while (SPI3->SR & SPI_SR_BSY) {}
    CS(1);
}

static const uint8_t INIT[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x20, 0x00, 0x22, 0x00, 0x07,
};

void oled_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
    (void)RCC->APB1ENR;

    pin_out(GPIOA, 7, 1);
    pin_out(GPIOB, 12, 1);
    pin_out(GPIOD, 2, 0);          /* hold the panel in reset */
    pin_out(GPIOC, 11, 0);
    pin_out(GPIOC, 13, 1);
    pin_out(GPIOA, 2, 1);
    pin_af(GPIOC, 10, 6);
    pin_af(GPIOC, 12, 6);

    /* master, /16 (2.6 MHz, the stock speed), software NSS, mode 0 */
    SPI3->CR1 = SPI_CR1_MSTR | (3u << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI3->CR1 |= SPI_CR1_SPE;

    delay_ms(10);
    GPIOD->BSRR = 1u << (2 + 16);  /* reset pulse */
    delay_ms(10);
    GPIOD->BSRR = 1u << 2;
    delay_ms(10);

    spi_send(INIT, sizeof INIT, 0);
    draw_clear();
    oled_flush();
    static const uint8_t on = 0xAF;
    spi_send(&on, 1, 0);
}

void oled_flush(void)
{
    static const uint8_t home[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};  /* full window, start at 0,0 */
    spi_send(home, sizeof home, 0);
    spi_send(fb, sizeof fb, 1);
}
