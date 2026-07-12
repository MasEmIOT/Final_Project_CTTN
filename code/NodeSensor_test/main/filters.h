/*
 * filters.h - Bo loc du lieu cam bien (chay tren NODE - edge processing).
 * Ho tro nhieu thuat toan, chon qua menuconfig:
 *   NONE    : khong loc (passthrough)
 *   MA      : trung binh truot (moving average)
 *   EMA     : trung binh mu (exponential moving average)
 *   MEDIAN  : trung vi cua so (chong nhieu xung/outlier)
 *   KALMAN  : Kalman 1D (loc nhieu Gauss, muot & bam theo)
 *
 * Moi tin hieu (nhiet do, do am, ap suat, anh sang) dung 1 filter_t rieng.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FILTER_NONE = 0,
    FILTER_MA,
    FILTER_EMA,
    FILTER_MEDIAN,
    FILTER_KALMAN,
} filter_type_t;

#define FILTER_WIN  8      /* cua so cho MA / MEDIAN */

typedef struct {
    filter_type_t type;
    /* MA / MEDIAN */
    float    win[FILTER_WIN];
    uint8_t  count;
    uint8_t  idx;
    /* EMA */
    float    ema;
    bool     ema_init;
    /* Kalman 1D */
    float    q;        /* process noise */
    float    r;        /* measurement noise */
    float    x;        /* uoc luong trang thai */
    float    p;        /* uoc luong sai so */
    bool     k_init;
} filter_t;

/* Khoi tao filter voi 1 loai thuat toan. */
void filter_init(filter_t *f, filter_type_t type);

/* Dua 1 mau do vao, tra ve gia tri DA LOC. */
float filter_apply(filter_t *f, float meas);

/* Ten thuat toan (de bao cao len tool). */
const char *filter_name(filter_type_t type);
