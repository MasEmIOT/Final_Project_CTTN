#include <math.h>
#include <string.h>
#include "mq135.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "mq135";

/* --- Tham so mach --- */
#define MQ_VSUPPLY_MV     3300.0f   /* dien ap cap cho mach chia (module 3V3) */
#define MQ_RL_KOHM        10.0f     /* dien tro tai RL tren module (thuong 10k/20k) */
#define MQ_RO_CLEAN_AIR   3.6f      /* Rs/Ro trong khong khi sach (datasheet ~3.6) */
#define MQ_RO_DEFAULT     10.0f     /* Ro mac dinh (kOhm) truoc khi calibrate */
#define MQ_SAMPLES        16        /* so mau trung binh moi lan doc */

/* Duong cong log-log (a, b) - ppm = a*(Rs/Ro)^b. Gia tri gan dung tu datasheet. */
#define NH3_A   102.2f
#define NH3_B  (-2.473f)
#define CH4_A   26.3f
#define CH4_B  (-1.10f)

/* IO16 -> ADC2 channel 5 tren ESP32-S3 */
#define MQ_ADC_UNIT     ADC_UNIT_2
#define MQ_ADC_CHAN     ADC_CHANNEL_5
#define MQ_ADC_ATTEN    ADC_ATTEN_DB_12   /* toan dai ~0..3.1V */

esp_err_t mq135_init(mq135_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ro = MQ_RO_DEFAULT;
    out->channel = MQ_ADC_CHAN;

    adc_oneshot_unit_handle_t unit;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = MQ_ADC_UNIT };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &unit);
    if (err != ESP_OK) { ESP_LOGW(TAG, "adc unit init loi: %s", esp_err_to_name(err)); return err; }

    adc_oneshot_chan_cfg_t ccfg = { .atten = MQ_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    err = adc_oneshot_config_channel(unit, MQ_ADC_CHAN, &ccfg);
    if (err != ESP_OK) { adc_oneshot_del_unit(unit); return err; }
    out->unit = unit;

    /* Hieu chuan dien ap (curve fitting cua Espressif) - co the khong ho tro tren moi chip */
    adc_cali_handle_t cali = NULL;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = MQ_ADC_UNIT, .chan = MQ_ADC_CHAN,
        .atten = MQ_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &cali) == ESP_OK) {
        out->cali = cali; out->has_cali = true;
    }
#endif
    out->present = true;
    ESP_LOGI(TAG, "MQ135 tren ADC2_CH5 (IO16), cali=%d, Ro=%.1fk", out->has_cali, out->ro);
    return ESP_OK;
}

esp_err_t mq135_read_mv(mq135_t *s, int *mv)
{
    if (!s->present) return ESP_ERR_INVALID_STATE;
    long acc = 0;
    for (int i = 0; i < MQ_SAMPLES; i++) {
        int raw = 0, v = 0;
        if (adc_oneshot_read(s->unit, s->channel, &raw) != ESP_OK) return ESP_FAIL;
        if (s->has_cali && adc_cali_raw_to_voltage(s->cali, raw, &v) == ESP_OK) {
            acc += v;
        } else {
            acc += raw * 3100 / 4095;   /* xap xi khi khong co cali */
        }
    }
    *mv = (int)(acc / MQ_SAMPLES);
    return ESP_OK;
}

/* Tinh Rs (kOhm) tu dien ap ngo ra */
static float rs_from_mv(int mv)
{
    if (mv <= 0) return 1e6f;
    float v = (float)mv;
    return MQ_RL_KOHM * (MQ_VSUPPLY_MV - v) / v;
}

esp_err_t mq135_calibrate(mq135_t *s)
{
    int mv = 0;
    esp_err_t err = mq135_read_mv(s, &mv);
    if (err != ESP_OK) return err;
    float rs = rs_from_mv(mv);
    s->ro = rs / MQ_RO_CLEAN_AIR;
    if (s->ro < 0.1f) s->ro = 0.1f;
    ESP_LOGI(TAG, "Hieu chuan: mv=%d Rs=%.1fk -> Ro=%.1fk", mv, rs, s->ro);
    return ESP_OK;
}

esp_err_t mq135_read(mq135_t *s, uint16_t *nh3_ppm, uint16_t *ch4_ppm, int *raw_mv)
{
    int mv = 0;
    esp_err_t err = mq135_read_mv(s, &mv);
    if (err != ESP_OK) return err;
    if (raw_mv) *raw_mv = mv;

    float ratio = rs_from_mv(mv) / (s->ro > 0.01f ? s->ro : MQ_RO_DEFAULT);
    if (ratio < 0.01f) ratio = 0.01f;

    float nh3 = NH3_A * powf(ratio, NH3_B);
    float ch4 = CH4_A * powf(ratio, CH4_B);
    if (nh3 < 0) nh3 = 0;
    if (nh3 > 5000) nh3 = 5000;
    if (ch4 < 0) ch4 = 0;
    if (ch4 > 10000) ch4 = 10000;
    if (nh3_ppm) *nh3_ppm = (uint16_t)(nh3 + 0.5f);
    if (ch4_ppm) *ch4_ppm = (uint16_t)(ch4 + 0.5f);
    return ESP_OK;
}
