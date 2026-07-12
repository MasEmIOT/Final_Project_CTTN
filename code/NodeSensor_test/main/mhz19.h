/*
 * mhz19.h - Cam bien CO2 giao tiep UART (MH-Z19 / MH-Z19B/C).
 *
 * Dau day: sensor TX -> ESP RX (IO5); sensor RX -> ESP TX (IO4). 9600 8N1.
 * Giao thuc khung 9 byte, lenh 0x86 doc nong do CO2.
 * Neu ban dung cam bien khac (SenseAir S8, Cubic CM1106...) hay bao de doi driver.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    int  port;
    bool present;
} mhz19_t;

/* Cai UART + kiem tra cam bien phan hoi. */
esp_err_t mhz19_init(mhz19_t *out);

/* Doc nong do CO2 (ppm). Tra ve ESP_OK + *co2_ppm neu khung + checksum hop le. */
esp_err_t mhz19_read_co2(mhz19_t *s, uint16_t *co2_ppm);

/* Bat/tat tu hieu chuan nen (ABC). Nen TAT khi dung trong chuong nuoi kin. */
esp_err_t mhz19_set_abc(mhz19_t *s, bool enable);
