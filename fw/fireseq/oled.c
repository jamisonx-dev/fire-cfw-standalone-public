/* SSD1306 on SPI3 (pins and init from the stock firmware), frames sent by DMA1 stream 7 so the
 * main loop never waits ~3 ms for a screen update.
 *   PC10 SCK, PC12 MOSI (AF6), PC11 D/C, PC13 CS, PD2 RESET, PA2 = brightness (low = bright, stock = high/dim). */
#include <string.h>
#include "stm32f4xx.h"
#include "draw.h"
#include "oled.h"

extern volatile uint32_t ms_ticks;

static uint8_t txbuf[FB_W * FB_H / 8];
static volatile bool busy;

static void delay_ms(uint32_t ms) { uint32_t t0 = ms_ticks; while (ms_ticks - t0 < ms) { IWDG->KR = 0xAAAA; } }

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

#define DC(l) (GPIOC->BSRR = (l) ? (1u << 11) : (1u << 27))
#define CS(l) (GPIOC->BSRR = (l) ? (1u << 13) : (1u << 29))

static void send_blocking(const uint8_t *p, unsigned n, int data)
{
    DC(data); CS(0);
    while (n--) { while (!(SPI3->SR & SPI_SR_TXE)) {} *(volatile uint8_t *)&SPI3->DR = *p++; }
    while (!(SPI3->SR & SPI_SR_TXE)) {}
    while (SPI3->SR & SPI_SR_BSY) {}
    CS(1);
}

static const uint8_t INIT[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0xFF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x20, 0x00, 0x22, 0x00, 0x07,
};

void oled_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_DMA1EN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI3EN;
    (void)RCC->APB1ENR;
    pin_out(GPIOD, 2, 0);
    pin_out(GPIOC, 11, 0);
    pin_out(GPIOC, 13, 1);
    pin_out(GPIOA, 2, 0);          /* PA2 low = brighter panel (measured; stock holds it high) */
    pin_af(GPIOC, 10, 6);
    pin_af(GPIOC, 12, 6);
    SPI3->CR1 = SPI_CR1_MSTR | (3u << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI3->CR1 |= SPI_CR1_SPE;
    delay_ms(10);
    GPIOD->BSRR = 1u << 18; delay_ms(10);
    GPIOD->BSRR = 1u << 2;  delay_ms(10);
    send_blocking(INIT, sizeof INIT, 0);
    memset(txbuf, 0, sizeof txbuf);
    send_blocking(txbuf, sizeof txbuf, 1);
    static const uint8_t on = 0xAF;
    send_blocking(&on, 1, 0);
}

bool oled_busy(void)
{
    if (!busy) return false;
    if (!(DMA1->HISR & DMA_HISR_TCIF7)) return true;
    while (!(SPI3->SR & SPI_SR_TXE)) {}
    while (SPI3->SR & SPI_SR_BSY) {}
    CS(1);
    SPI3->CR2 = 0;
    DMA1->HIFCR = 0x0F400000u;                      /* clear all stream 7 flags */
    busy = false;
    return false;
}

bool oled_flush(void)
{
    if (oled_busy()) return false;
    static const uint8_t home[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    send_blocking(home, sizeof home, 0);
    memcpy(txbuf, fb, sizeof txbuf);
    DMA1_Stream7->CR = 0;
    while (DMA1_Stream7->CR & DMA_SxCR_EN) {}
    DMA1->HIFCR = 0x0F400000u;
    DMA1_Stream7->PAR = (uint32_t)&SPI3->DR;
    DMA1_Stream7->M0AR = (uint32_t)txbuf;
    DMA1_Stream7->NDTR = sizeof txbuf;
    DMA1_Stream7->CR = (0u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_DIR_0;   /* channel 0 = SPI3_TX, mem -> periph */
    DC(1); CS(0);
    busy = true;
    DMA1_Stream7->CR |= DMA_SxCR_EN;
    SPI3->CR2 = SPI_CR2_TXDMAEN;
    return true;
}
