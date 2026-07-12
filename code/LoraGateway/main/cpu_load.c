#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "cpu_load.h"

static volatile uint32_t s_idle_cnt[2];
static uint32_t s_calib[2] = { 1, 1 };

static bool idle_hook_c0(void) { s_idle_cnt[0]++; return true; }
static bool idle_hook_c1(void) { s_idle_cnt[1]++; return true; }

void cpu_load_init(void)
{
    esp_register_freertos_idle_hook_for_cpu(idle_hook_c0, 0);
    esp_register_freertos_idle_hook_for_cpu(idle_hook_c1, 1);

    /* Hieu chuan: dem idle trong 1 giay khi he thong gan nhu ranh. */
    s_idle_cnt[0] = 0;
    s_idle_cnt[1] = 0;
    vTaskDelay(pdMS_TO_TICKS(1000));
    s_calib[0] = s_idle_cnt[0] ? s_idle_cnt[0] : 1;
    s_calib[1] = s_idle_cnt[1] ? s_idle_cnt[1] : 1;

    s_idle_cnt[0] = 0;
    s_idle_cnt[1] = 0;
}

void cpu_load_sample(uint8_t *c0_cpu, uint8_t *c1_cpu)
{
    uint32_t n0 = s_idle_cnt[0];
    uint32_t n1 = s_idle_cnt[1];
    s_idle_cnt[0] = 0;
    s_idle_cnt[1] = 0;

    int l0 = 100 - (int)((uint64_t)n0 * 100ULL / s_calib[0]);
    int l1 = 100 - (int)((uint64_t)n1 * 100ULL / s_calib[1]);
    if (l0 < 0) {
        l0 = 0;
    } else if (l0 > 100) {
        l0 = 100;
    }
    if (l1 < 0) {
        l1 = 0;
    } else if (l1 > 100) {
        l1 = 100;
    }

    if (c0_cpu) *c0_cpu = (uint8_t)l0;
    if (c1_cpu) *c1_cpu = (uint8_t)l1;
}
