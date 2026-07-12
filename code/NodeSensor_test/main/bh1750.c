#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bh1750.h"

#define BH_TIMEOUT_MS       100
#define BH_CMD_POWER_ON     0x01
#define BH_CMD_CONT_HRES    0x10    /* do lien tuc, do phan giai 1 lx, chu ky ~120ms */

static esp_err_t cmd(bh1750_t *s, uint8_t c)
{
    return i2c_master_transmit(s->dev, &c, 1, BH_TIMEOUT_MS);
}

esp_err_t bh1750_init(i2c_master_bus_handle_t bus, uint8_t addr, bh1750_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &out->dev);
    if (err != ESP_OK) return err;

    err = cmd(out, BH_CMD_POWER_ON);
    if (err != ESP_OK) return err;
    err = cmd(out, BH_CMD_CONT_HRES);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(180));   /* doi phep do dau tien hoan tat */
    return ESP_OK;
}

esp_err_t bh1750_read(bh1750_t *s, float *lux)
{
    uint8_t raw[2] = { 0 };
    esp_err_t err = i2c_master_receive(s->dev, raw, sizeof(raw), BH_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    *lux = (float)((raw[0] << 8) | raw[1]) / 1.2f;
    return ESP_OK;
}
