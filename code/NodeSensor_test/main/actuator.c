#include "actuator.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static uint8_t s_mask = 0;
static bool    s_manual = false;
static int64_t s_manual_until_us = 0;   /* thoi diem manual het han */

static gpio_num_t pin_of(uint8_t bit)
{
    if (bit == ACT1_BIT) return PIN_ACT1;
    if (bit == ACT2_BIT) return PIN_ACT2;
    return PIN_ACT3;
}

/* LATCH muc ra qua che do ngu (gpio_hold) de relay KHONG bi nhay khi node
 * light-sleep. Phai hold_dis truoc khi doi muc, doi xong hold_en lai. */
static void apply_bit(uint8_t bit, bool on)
{
    gpio_num_t pin = pin_of(bit);
    gpio_hold_dis(pin);
    gpio_set_level(pin, on ? ACT_ON_LEVEL : !ACT_ON_LEVEL);
    gpio_hold_en(pin);
}

void actuator_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_ACT1) | (1ULL << PIN_ACT2) | (1ULL << PIN_ACT3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    s_mask = 0;
    s_manual = false;
    apply_bit(ACT1_BIT, false);
    apply_bit(ACT2_BIT, false);
    apply_bit(ACT3_BIT, false);
}

void actuator_set_mask(uint8_t mask)
{
    mask &= (ACT1_BIT | ACT2_BIT | ACT3_BIT);
    apply_bit(ACT1_BIT, mask & ACT1_BIT);
    apply_bit(ACT2_BIT, mask & ACT2_BIT);
    apply_bit(ACT3_BIT, mask & ACT3_BIT);
    s_mask = mask;
}

void actuator_set(uint8_t act_bit, bool on)
{
    if (on) s_mask |= act_bit; else s_mask &= ~act_bit;
    apply_bit(act_bit, on);
}

uint8_t actuator_get_mask(void) { return s_mask; }

void actuator_set_manual(bool manual)
{
    s_manual = manual;
    if (manual) {
        /* bat/gia han manual them MANUAL_TIMEOUT_S giay */
        s_manual_until_us = esp_timer_get_time() + (int64_t)MANUAL_TIMEOUT_S * 1000000;
    } else {
        s_manual_until_us = 0;
    }
}

bool actuator_is_manual(void) { return s_manual; }

uint16_t actuator_manual_left_s(void)
{
    if (!s_manual) return 0;
    int64_t left = s_manual_until_us - esp_timer_get_time();
    if (left <= 0) return 0;
    return (uint16_t)(left / 1000000);
}

void actuator_tick(void)
{
    /* Manual het han -> tu dong ve AUTO (FSM se dieu khien lai chu ky sau). */
    if (s_manual && esp_timer_get_time() >= s_manual_until_us) {
        s_manual = false;
        s_manual_until_us = 0;
    }
}
