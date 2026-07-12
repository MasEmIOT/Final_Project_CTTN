#include "fsm.h"
#include "packet.h"       /* FSM_SAFE.., ACTx_BIT */
#include "actuator.h"
#include "esp_timer.h"

/* ===== Nguong (co the tinh chinh theo Bang 3.6 trong bao cao) =====
 * Chuyen LEN muc nguy hiem hon o nguong CAO; ha XUONG o nguong THAP hon
 * (hysteresis) de tranh role dong/cat lien tuc quanh nguong. */
#define T_EMERG      35.0f
#define T_WARN       32.0f
#define T_CLEAR      30.0f      /* ha ve SAFE khi < nguong nay */

#define NH3_EMERG    25.0f
#define NH3_WARN     20.0f
#define NH3_CLEAR    18.0f

#define CO2_EMERG    5000.0f    /* ppm */
#define CO2_WARN     3000.0f
#define CO2_CLEAR    2500.0f

#define THI_EMERG    78.0f
#define THI_WARN     72.0f
#define THI_CLEAR    70.0f

static float compute_thi(float t, float h)
{
    /* Temperature-Humidity Index (giong bao cao, lst:fsm) */
    return 0.8f * t + (h / 100.0f) * (t - 14.4f) + 46.4f;
}

void fsm_update(const fsm_input_t *in, uint8_t cur, bool apply_act, fsm_output_t *out)
{
    int64_t t0 = esp_timer_get_time();

    float t   = in->have_th  ? in->temp_c  : 0.0f;
    float h   = in->have_th  ? in->hum_pct : 0.0f;
    float nh3 = in->have_nh3 ? (float)in->nh3_ppm : 0.0f;
    float co2 = in->have_co2 ? (float)in->co2_ppm : 0.0f;
    float thi = in->have_th ? compute_thi(t, h) : 0.0f;

    uint8_t state;
    uint8_t mask;

    if ((in->have_th  && t   > T_EMERG)   ||
        (in->have_nh3 && nh3 > NH3_EMERG) ||
        (in->have_co2 && co2 > CO2_EMERG) ||
        (in->have_th  && thi > THI_EMERG)) {
        state = FSM_EMERGENCY;
        mask  = ACT1_BIT | ACT2_BIT | ACT3_BIT;   /* bat toan bo: quat + phun suong + du phong */
    } else if ((in->have_th  && t   > T_WARN)   ||
               (in->have_nh3 && nh3 > NH3_WARN)  ||
               (in->have_co2 && co2 > CO2_WARN)  ||
               (in->have_th  && thi > THI_WARN)) {
        state = FSM_WARN;
        mask  = ACT1_BIT;                          /* canh bao: chi quat muc thap */
    } else {
        /* Vung "giua": chi ha ve SAFE khi da xuong DUOI nguong clear (hysteresis);
         * neu chua thi GIU trang thai hien tai de chong chattering. */
        bool below_clear = (!in->have_th  || (t < T_CLEAR && thi < THI_CLEAR)) &&
                           (!in->have_nh3 || nh3 < NH3_CLEAR) &&
                           (!in->have_co2 || co2 < CO2_CLEAR);
        if (cur != FSM_SAFE && !below_clear) {
            state = cur;                            /* giu nguyen */
            mask  = (cur == FSM_EMERGENCY) ? (ACT1_BIT | ACT2_BIT | ACT3_BIT) : ACT1_BIT;
        } else {
            state = FSM_SAFE;
            mask  = 0;                              /* tat het */
        }
    }

    if (apply_act && !actuator_is_manual()) {
        actuator_set_mask(mask);
    }

    out->state    = state;
    out->act_mask = actuator_is_manual() ? actuator_get_mask() : mask;
    out->thi_x10  = (uint16_t)(thi * 10.0f + 0.5f);
    int64_t dt = esp_timer_get_time() - t0;
    out->decide_us = (uint16_t)(dt > 65000 ? 65000 : dt);
}
