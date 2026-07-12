/*
 * mq135.h - Cam bien khi MQ135 (NH3, CO2, khoi...) qua ADC.
 *
 * Ngo ra analog A0 noi IO16 (ADC2_CH5 tren ESP32-S3). Driver dung adc_oneshot.
 * MQ135 la cam bien MOX: can NUNG NONG (warm-up) truoc khi so lieu on dinh
 * (khuyen nghi >= 30s luc moi cap nguon; can nguon lien tuc de giu can bang nhiet).
 *
 * Quy doi ppm theo mo hinh Rs/Ro luy thua:  ppm = a * (Rs/Ro)^b
 *   Rs  = RL * (Vsupply - Vout) / Vout        (dien tro cam bien)
 *   Ro  = Rs do trong KHONG KHI SACH / RO_CLEAN_AIR_FACTOR
 * -> Goi mq135_calibrate() khi o khong khi sach de chot Ro cho chinh xac.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    void   *unit;        /* adc_oneshot_unit_handle_t */
    void   *cali;        /* adc_cali_handle_t (co the NULL neu khong hieu chuan) */
    int     channel;     /* ADC channel */
    bool    has_cali;
    float   ro;          /* dien tro tham chieu (kOhm) trong khong khi sach */
    bool    present;
} mq135_t;

/* Khoi tao ADC cho MQ135. Dat Ro mac dinh (se tinh chinh xac hon khi calibrate). */
esp_err_t mq135_init(mq135_t *out);

/* Doc dien ap ngo ra (mV, trung binh nhieu mau de giam nhieu). */
esp_err_t mq135_read_mv(mq135_t *s, int *mv);

/* Doc va quy doi: nh3_ppm, ch4_ppm (uoc luong), kem raw mV. */
esp_err_t mq135_read(mq135_t *s, uint16_t *nh3_ppm, uint16_t *ch4_ppm, int *raw_mv);

/* Hieu chuan Ro: goi khi cam bien o KHONG KHI SACH & da warm-up. */
esp_err_t mq135_calibrate(mq135_t *s);
