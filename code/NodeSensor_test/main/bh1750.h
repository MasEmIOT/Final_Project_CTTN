/* bh1750.h - Cam bien anh sang BH1750 (dia chi 0x23 hoac 0x5C) */
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
} bh1750_t;

esp_err_t bh1750_init(i2c_master_bus_handle_t bus, uint8_t addr, bh1750_t *out);
esp_err_t bh1750_read(bh1750_t *s, float *lux);
