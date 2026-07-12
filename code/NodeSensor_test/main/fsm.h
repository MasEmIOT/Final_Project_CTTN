/*
 * fsm.h - May trang thai dieu khien bien (edge control) 3 muc:
 *   SAFE - WARN - EMERGENCY, kem hysteresis chong chattering role.
 *
 * Vao: nhiet do, do am, NH3 (MQ135), CO2 (UART). Ra: trang thai + bitmask
 * actuator + chi so THI + do tre RA QUYET DINH (decide_us, do bang esp_timer).
 * Neu actuator dang o che do MANUAL (override tu App) thi FSM chi tinh trang
 * thai de bao cao, KHONG ghi de chan chap hanh.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float    temp_c;
    float    hum_pct;
    uint16_t nh3_ppm;
    uint16_t co2_ppm;
    bool     have_th;    /* co nhiet/am hop le */
    bool     have_nh3;
    bool     have_co2;
} fsm_input_t;

typedef struct {
    uint8_t  state;      /* FSM_SAFE / FSM_WARN / FSM_EMERGENCY */
    uint8_t  act_mask;   /* bitmask ACT1..ACT3 ma FSM mong muon */
    uint16_t thi_x10;    /* chi so nhiet am THI x10 */
    uint16_t decide_us;  /* thoi gian thuat toan ra quyet dinh (us) */
} fsm_output_t;

/* Cap nhat FSM tu trang thai hien tai `cur`. Neu apply_act=true va khong o
 * che do manual -> ghi bitmask ra actuator. Tra ve trang thai moi qua out->state. */
void fsm_update(const fsm_input_t *in, uint8_t cur, bool apply_act, fsm_output_t *out);
