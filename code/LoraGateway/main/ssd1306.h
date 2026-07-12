/*
 * ssd1306.h - Driver OLED SSD1306 128x64 I2C (framebuffer + font 5x7).
 * Tao bus I2C rieng tren PIN_OLED_SDA/SCL. Dung de hien thi trang thai gateway:
 * so node dang ket noi, thong so co ban, FSM...
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define OLED_W  128
#define OLED_H  64

esp_err_t ssd1306_init(void);
bool      ssd1306_present(void);

void ssd1306_clear(void);
/* Ve chuoi tai (col pixel 0..127, page 0..7). Chu cao 8px, rong 6px/ky tu. */
void ssd1306_text(int col, int page, const char *s);
/* Ke duong ngang tai page (dung lam gach phan cach). */
void ssd1306_hline(int page);
/* Day framebuffer ra man hinh. */
void ssd1306_flush(void);
