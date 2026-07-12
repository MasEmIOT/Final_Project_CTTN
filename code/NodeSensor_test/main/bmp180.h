/*
 * bmp180.h - Cam bien BMP180 (Bosch) qua I2C, doc nhiet do + ap suat.
 *
 * BMP180 dia chi I2C co dinh 0x77 (KHAC voi BMP280 co the 0x76/0x77).
 * Chip ID (thanh ghi 0xD0) = 0x55.
 * Co EEPROM hieu chuan 11 he so (AC1..AC6, B1, B2, MB, MC, MD) o 0xAA..0xBF.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* Oversampling setting: 0=ultra low power ... 3=ultra high res (lau hon) */
#ifndef BMP180_OSS
#define BMP180_OSS   3
#endif

typedef struct {
    i2c_master_dev_handle_t dev;
    /* He so hieu chuan doc tu EEPROM cua chip */
    int16_t  AC1, AC2, AC3;
    uint16_t AC4, AC5, AC6;
    int16_t  B1, B2;
    int16_t  MB, MC, MD;
    uint8_t  chip_id;        /* phai = 0x55 */
} bmp180_t;

/* addr nen la 0x77. Kiem tra chip id + doc EEPROM hieu chuan. */
esp_err_t bmp180_init(i2c_master_bus_handle_t bus, uint8_t addr, bmp180_t *out);

/* Doc nhiet do (do C) va ap suat (hPa) da bu theo datasheet Bosch. */
esp_err_t bmp180_read(bmp180_t *b, float *temp_c, float *press_hpa);
