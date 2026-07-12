/*
 * sx127x.h - Driver toi gian cho SX1276/77/78 (Ai-Thinker Ra-02...) o che do LoRa.
 * Dung SPI master + polling thanh ghi IRQ (khong can DIO0).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#define SX127X_MAX_PAYLOAD  128   /* du cho goi ma hoa (envelope ~73 byte) */

typedef struct {
    spi_host_device_t spi_host;     /* vd: SPI2_HOST */
    gpio_num_t pin_sck;
    gpio_num_t pin_miso;
    gpio_num_t pin_mosi;
    gpio_num_t pin_nss;
    gpio_num_t pin_rst;
    uint32_t   freq_hz;             /* vd: 433000000 */
    uint8_t    sf;                  /* spreading factor 7..12 */
    uint32_t   bw_hz;               /* 125000 / 250000 / 500000 */
    uint8_t    cr_denom;            /* coding rate 4/x, x = 5..8 */
    int8_t     tx_power_dbm;        /* 2..20 (PA_BOOST) */
    uint8_t    sync_word;           /* 0x12 = mang rieng */
    uint16_t   preamble_len;        /* thuong la 8 */
} sx127x_config_t;

/* Khoi tao SPI + reset chip + cau hinh LoRa. */
esp_err_t sx127x_init(const sx127x_config_t *cfg);

/* Gui 1 goi (blocking den khi TxDone hoac het timeout_ms). */
esp_err_t sx127x_send(const uint8_t *data, uint8_t len, uint32_t timeout_ms);

/* Chuyen sang che do thu lien tuc (RX continuous). */
esp_err_t sx127x_start_rx(void);

/*
 * Kiem tra va lay goi tin dang cho (non-blocking):
 *  - ESP_OK              : co goi hop le, *out_len byte da copy vao buf
 *  - ESP_ERR_NOT_FOUND   : chua co goi nao
 *  - ESP_ERR_INVALID_CRC : co goi nhung sai CRC (da bi bo)
 *  - ESP_ERR_INVALID_SIZE: goi dai hon max_len (da bi bo)
 */
esp_err_t sx127x_receive(uint8_t *buf, uint8_t max_len, uint8_t *out_len,
                         int16_t *out_rssi_dbm, float *out_snr_db);

/* Dua chip ve che do sleep (tiet kiem dien). */
esp_err_t sx127x_sleep(void);
