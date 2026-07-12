#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sht3x.h"

#define SHT3X_TIMEOUT_MS  100

/* CRC8 cua Sensirion: poly 0x31, init 0xFF */
static uint8_t sht_crc8(const uint8_t *d, int n)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t sht3x_init(i2c_master_bus_handle_t bus, uint8_t addr, sht3x_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &out->dev);
}

esp_err_t sht3x_read(sht3x_t *s, float *temp_c, float *hum_pct)
{
    /* 0x2400: single shot, high repeatability, khong clock-stretching */
    const uint8_t cmd[2] = { 0x24, 0x00 };
    esp_err_t err = i2c_master_transmit(s->dev, cmd, sizeof(cmd), SHT3X_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(20));   /* thoi gian do toi da 15.5ms */

    uint8_t raw[6] = { 0 };
    err = i2c_master_receive(s->dev, raw, sizeof(raw), SHT3X_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    if (sht_crc8(&raw[0], 2) != raw[2] || sht_crc8(&raw[3], 2) != raw[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t t_raw = (uint16_t)((raw[0] << 8) | raw[1]);
    uint16_t h_raw = (uint16_t)((raw[3] << 8) | raw[4]);
    *temp_c  = -45.0f + 175.0f * (float)t_raw / 65535.0f;
    *hum_pct = 100.0f * (float)h_raw / 65535.0f;
    return ESP_OK;
}
