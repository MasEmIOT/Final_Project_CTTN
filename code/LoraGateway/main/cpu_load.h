/*
 * cpu_load.h - Uoc luong % tai CPU tung core (c0, c1) cho profiling FreeRTOS.
 *
 * Cach lam: dem so lan idle hook chay tren moi core. Luc khoi dong, do so dem
 * idle trong 1 giay khi he thong ranh -> lay lam moc 100% ranh. Sau do moi 1 giay
 * lay ti le idle so voi moc de suy ra % tai. Day la uoc luong nhe, khong can
 * bat run-time stats nang.
 */
#pragma once

#include <stdint.h>

/* Dang ky idle hook + hieu chuan moc (chiem ~1 giay, goi som trong app_main). */
void cpu_load_init(void);

/* Lay % tai 2 core ke tu lan goi truoc. NEN goi deu moi 1 giay (cung cua so do). */
void cpu_load_sample(uint8_t *c0_cpu, uint8_t *c1_cpu);
