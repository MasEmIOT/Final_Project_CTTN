/* sht3x.h - SHT30/31/35 qua I2C (dia chi 0x44 hoac 0x45) */
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
} sht3x_t;

esp_err_t sht3x_init(i2c_master_bus_handle_t bus, uint8_t addr, sht3x_t *out);

/* Do don (single-shot, high repeatability). Mat ~20ms. */
esp_err_t sht3x_read(sht3x_t *s, float *temp_c, float *hum_pct);
