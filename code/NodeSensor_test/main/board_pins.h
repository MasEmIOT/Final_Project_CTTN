/*
 * board_pins.h - Pin map theo schematic "Node Sensor" Rev 1.0 (ESP32-S3-WROOM-1)
 *
 * I2C bus 0 : net SDA  = GPIO8,  net SCL  = GPIO14  -> J14, J17 (BH1750), J12
 * I2C bus 1 : net I2C_SDA1 = GPIO6, net I2C_SCL1 = GPIO7 -> J15 (BMP180), J16, J13 (RTC)
 * (J19/SHT30 nam tren 1 trong 2 bus - code tu dong probe ca 2 bus nen khong can quan tam)
 *
 * LoRa SX1278 (Ra-02): SCK=GPIO10, MISO=GPIO11, MOSI=GPIO12, NSS=GPIO13,
 *                      RST=GPIO9, DIO0=GPIO15
 *   -> DIO0 (GPIO15) dung lam nguon WAKE khi co "su kien bat ngo" (RxDone tu LoRa)
 *      trong co che hybrid light-sleep.
 *
 * LED: B=GPIO39, G=GPIO40, R=GPIO41
 */
#pragma once

#include "driver/gpio.h"

/* ---------- I2C ---------- */
#define PIN_I2C0_SDA        GPIO_NUM_8
#define PIN_I2C0_SCL        GPIO_NUM_14
#define PIN_I2C1_SDA        GPIO_NUM_6
#define PIN_I2C1_SCL        GPIO_NUM_7
#define I2C_SPEED_HZ        100000

/* ---------- RTC DS3231 (I2C, dia chi 0x68) ----------
 * SDA/SCL dung chung bus I2C0 (GPIO8/GPIO14). SQW/INT -> GPIO1 lam nguon wake. */
#define PIN_RTC_SQW         GPIO_NUM_1
#define DS3231_I2C_ADDR     0x68

/* ---------- MQ135 (khi NH3/CO2/khoi) - ngo ra analog A0 ----------
 * IO16 = ADC2_CH5 tren ESP32-S3. Node khong dung WiFi luc thuong nen ADC2 OK;
 * khi vao OTA (bat WiFi) da tam dung doc cam bien nen khong xung dot. */
#define PIN_MQ135_ADC       GPIO_NUM_16

/* ---------- Cam bien CO2 UART (mac dinh MH-Z19) ----------
 * Sensor TX -> ESP RX (IO5); Sensor RX -> ESP TX (IO4). 9600 8N1. */
#define PIN_CO2_RX          GPIO_NUM_5    /* ESP nhan tu sensor TX */
#define PIN_CO2_TX          GPIO_NUM_4    /* ESP gui toi sensor RX */
#define CO2_UART_PORT       1             /* UART1 */

/* ---------- Khoi chap hanh (Actuator) - relay/MOSFET ----------
 * Act1=quat/thong gio, Act2=phun suong, Act3=du phong (den suoi...). */
#define PIN_ACT1            GPIO_NUM_38
#define PIN_ACT2            GPIO_NUM_42
#define PIN_ACT3            GPIO_NUM_45
/* 0 = relay kich muc THAP (active-LOW, loai co opto pho bien - BAT khi chan=0).
 * 1 = relay/MOSFET kich muc CAO (active-HIGH). Doi lai neu relay cua ban nguoc. */
#define ACT_ON_LEVEL        0

/* ---------- LoRa SX1278 ---------- */
#define PIN_LORA_SCK        GPIO_NUM_10
#define PIN_LORA_MISO       GPIO_NUM_11
#define PIN_LORA_MOSI       GPIO_NUM_12
#define PIN_LORA_NSS        GPIO_NUM_13
#define PIN_LORA_RST        GPIO_NUM_9
#define PIN_LORA_DIO0       GPIO_NUM_15   /* dung lam nguon wake (GPIO wakeup khi light-sleep) */

/* Chan danh thuc khi co su kien bat ngo trong luc light-sleep.
 * Mac dinh = DIO0 cua LoRa (RxDone keo len) -> goi LoRa den se danh thuc node.
 * Co the doi sang chan nut nhan/PIR neu muon. */
#define PIN_WAKE_EVENT      PIN_LORA_DIO0
#define WAKE_EVENT_LEVEL    1             /* muc tich cuc cua su kien wake (1 = canh len) */

/* Chan KICH HOAT TEST WATCHDOG: chap chan nay xuong GND (muc 0) la node se
 * co tinh "treo" (vong lap vo han, khong reset WDT) -> Task WDT se reset chip.
 * Mac dinh GPIO0 (nut BOOT tren nhieu board). Doi neu chan nay ban. */
#define PIN_WDT_TEST        GPIO_NUM_0

/* Sync word rieng cho mang nay - PHAI GIONG NHAU o Node va Gateway */
#define LORA_SYNC_WORD      0x12
#define LORA_BW_HZ          125000
#define LORA_CR_DENOM       5             /* coding rate 4/5 */

/* ---------- LED bao trang thai ---------- */
#define PIN_LED_B           GPIO_NUM_39
#define PIN_LED_G           GPIO_NUM_40
#define PIN_LED_R           GPIO_NUM_41
#define LED_ON_LEVEL        1             /* doi thanh 0 neu LED noi kieu active-low */
