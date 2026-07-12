#include <string.h>
#include "filters.h"

const char *filter_name(filter_type_t type)
{
    switch (type) {
        case FILTER_MA:     return "MovingAvg";
        case FILTER_EMA:    return "EMA";
        case FILTER_MEDIAN: return "Median";
        case FILTER_KALMAN: return "Kalman";
        default:            return "None";
    }
}

void filter_init(filter_t *f, filter_type_t type)
{
    memset(f, 0, sizeof(*f));
    f->type = type;
    /* Tham so Kalman mac dinh: q nho (tin model), r vua (nhieu do) */
    f->q = 0.01f;
    f->r = 4.0f;
}

static float apply_ma(filter_t *f, float meas)
{
    f->win[f->idx] = meas;
    f->idx = (uint8_t)((f->idx + 1) % FILTER_WIN);
    if (f->count < FILTER_WIN) f->count++;
    float sum = 0.0f;
    for (int i = 0; i < f->count; i++) sum += f->win[i];
    return sum / (float)f->count;
}

static float apply_ema(filter_t *f, float meas)
{
    const float alpha = 0.30f;       /* he so lam muot (0..1, nho = muot hon) */
    if (!f->ema_init) { f->ema = meas; f->ema_init = true; }
    else              { f->ema = alpha * meas + (1.0f - alpha) * f->ema; }
    return f->ema;
}

static float apply_median(filter_t *f, float meas)
{
    f->win[f->idx] = meas;
    f->idx = (uint8_t)((f->idx + 1) % FILTER_WIN);
    if (f->count < FILTER_WIN) f->count++;
    float tmp[FILTER_WIN];
    memcpy(tmp, f->win, sizeof(float) * f->count);
    /* sap xep noi bot (cua so nho) */
    for (int i = 0; i < f->count - 1; i++)
        for (int j = i + 1; j < f->count; j++)
            if (tmp[j] < tmp[i]) { float t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
    return tmp[f->count / 2];
}

static float apply_kalman(filter_t *f, float meas)
{
    if (!f->k_init) { f->x = meas; f->p = 1.0f; f->k_init = true; return f->x; }
    /* Predict: khong co mo hinh chuyen dong -> x giu nguyen, p tang theo q */
    f->p += f->q;
    /* Update */
    float k = f->p / (f->p + f->r);     /* Kalman gain */
    f->x += k * (meas - f->x);
    f->p = (1.0f - k) * f->p;
    return f->x;
}

float filter_apply(filter_t *f, float meas)
{
    switch (f->type) {
        case FILTER_MA:     return apply_ma(f, meas);
        case FILTER_EMA:    return apply_ema(f, meas);
        case FILTER_MEDIAN: return apply_median(f, meas);
        case FILTER_KALMAN: return apply_kalman(f, meas);
        default:            return meas;
    }
}
