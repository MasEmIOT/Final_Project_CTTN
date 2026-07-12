/*
 * actuator.h - Khoi chap hanh 3 kenh Act1..Act3 (relay/MOSFET).
 *   Act1 = quat/thong gio, Act2 = phun suong (lam mat), Act3 = du phong.
 * FSM tren node dieu khien tu dong; App co the OVERRIDE thu cong qua downlink.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "packet.h"   /* ACT1_BIT.. */

/* Khoi tao 3 chan GPIO output, mac dinh TAT het. */
void actuator_init(void);

/* Dat trang thai theo bitmask (bit0=Act1 bit1=Act2 bit2=Act3, 1=ON). */
void actuator_set_mask(uint8_t mask);

/* Bat/tat 1 kenh (bit ACTx_BIT). */
void actuator_set(uint8_t act_bit, bool on);

/* Lay bitmask trang thai hien tai. */
uint8_t actuator_get_mask(void);

/* Che do override thu cong CO HAN: khi bat, FSM khong ghi de actuator nua;
 * sau MANUAL_TIMEOUT_S giay se TU DONG ve AUTO. Goi lai se gia han them 60s. */
void actuator_set_manual(bool manual);
bool actuator_is_manual(void);

/* So giay con lai cua che do manual (0 neu dang AUTO). */
uint16_t actuator_manual_left_s(void);

/* Goi moi chu ky: neu manual het han thi tu dong chuyen ve AUTO. */
void actuator_tick(void);
