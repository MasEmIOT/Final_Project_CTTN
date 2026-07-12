/* bmp280.h - BMP280/BME280 qua I2C (dia chi 0x76 hoac 0x77), doc nhiet do + ap suat */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
    /* he so hieu chinh doc tu chip */
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  chip_id;        /* 0x58 = BMP280, 0x60 = BME280 */
} bmp280_t;

esp_err_t bmp280_init(i2c_master_bus_handle_t bus, uint8_t addr, bmp280_t *out);
esp_err_t bmp280_read(bmp280_t *b, float *temp_c, float *press_hpa);
