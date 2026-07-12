/*
 * ds3231.h - RTC DS3231 (I2C, dia chi 0x68) tren bus I2C0 dung chung.
 *
 * Cung cap nhan thoi gian THUC (Unix epoch) cho:
 *   - Chong tan cong phat lai (anti-replay): moi goi mang timestamp tu RTC.
 *   - Luu tru offline (store-and-forward): moi ban ghi co thoi diem goc chinh xac.
 * RTC giu gio qua mat dien (co pin CR2032) -> khong phu thuoc SNTP/gateway.
 * Chan SQW/INT (GPIO1) co the dung lam nguon danh thuc (alarm) khi ngu.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
    bool present;
} ds3231_t;

/* Them DS3231 vao bus. Tra ve ESP_OK neu tim thay chip. */
esp_err_t ds3231_init(i2c_master_bus_handle_t bus, ds3231_t *out);

/* Doc thoi gian hien tai -> Unix epoch (UTC). 0 neu loi. */
esp_err_t ds3231_get_epoch(ds3231_t *s, uint32_t *epoch);

/* Ghi thoi gian tu Unix epoch (UTC) vao RTC (dung de dong bo tu gateway). */
esp_err_t ds3231_set_epoch(ds3231_t *s, uint32_t epoch);

/* Doc nhiet do chip RTC (do C, phan giai 0.25) - tien loi de kiem tra RTC song. */
esp_err_t ds3231_read_temp(ds3231_t *s, float *temp_c);
