#ifndef OLED_H
#define OLED_H
#include <stdbool.h>
void oled_init(void);    /* pins, SPI3, reset, SSD1306 init, clear, on (blocking, once) */
bool oled_flush(void);   /* start sending fb[] by DMA; false if the previous frame is still going */
bool oled_busy(void);    /* also finishes a completed transfer (call regularly) */
#endif
