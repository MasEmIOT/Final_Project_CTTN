/*
 * board_pins.h - GATEWAY chay tren ESP32 DevKit (WROOM-32).
 *
 * Gan chan (do dong an tu dong xuat, thong bao cho nguoi dung):
 *   LoRa SX1278 (Ra-02) qua VSPI:
 *     SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23, NSS/CS=GPIO5, RST=GPIO27, DIO0=GPIO26
 *   OLED SSD1306 I2C:  SDA=GPIO21, SCL=GPIO22 (I2C mac dinh)
 *   LED trang thai:    GPIO2 (LED onboard) - dung chung R/G/B tro ve 1 chan
 *
 * Tranh GPIO34..39 (chi input, khong pull), GPIO6..11 (noi flash).
 */
#pragma once

#include "driver/gpio.h"

/* ---------- LoRa SX1278 (Ra-02) - VSPI ---------- */
#define PIN_LORA_SCK        GPIO_NUM_18
#define PIN_LORA_MISO       GPIO_NUM_19
#define PIN_LORA_MOSI       GPIO_NUM_23
#define PIN_LORA_NSS        GPIO_NUM_5
#define PIN_LORA_RST        GPIO_NUM_27
#define PIN_LORA_DIO0       GPIO_NUM_26   /* khong bat buoc trong code (polling) */

/* Sync word rieng cho mang nay - PHAI GIONG NHAU o Node va Gateway */
#define LORA_SYNC_WORD      0x12
#define LORA_BW_HZ          125000
#define LORA_CR_DENOM       5             /* coding rate 4/5 */

/* ---------- OLED SSD1306 (I2C) ---------- */
#define PIN_OLED_SDA        GPIO_NUM_21
#define PIN_OLED_SCL        GPIO_NUM_22
#define OLED_I2C_PORT       0
#define OLED_I2C_ADDR       0x3C
#define OLED_I2C_HZ         400000

/* ---------- LED bao trang thai (DevKit chi co 1 LED onboard GPIO2) ----------
 * Map ca 3 mau ve cung 1 chan de tai su dung code led_blink cua gateway. */
#define PIN_LED_B           GPIO_NUM_2
#define PIN_LED_G           GPIO_NUM_2
#define PIN_LED_R           GPIO_NUM_2
#define LED_ON_LEVEL        1
