#ifndef OLED_HW_H
#define OLED_HW_H
void oled_init(void);   /* pins, SPI3, reset pulse, SSD1306 init, clear, display on */
void oled_flush(void);  /* push fb[] (about 3 ms at 2.6 MHz) */
#endif
