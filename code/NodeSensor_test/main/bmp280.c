#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bmp280.h"

static const char *TAG = "bmp280";

#define BMP_TIMEOUT_MS    100

#define REG_CALIB_START   0x88
#define REG_CHIP_ID       0xD0
#define REG_RESET         0xE0
#define REG_CTRL_MEAS     0xF4
#define REG_CONFIG        0xF5
#define REG_DATA_START    0xF7   /* press msb,lsb,xlsb + temp msb,lsb,xlsb */

static esp_err_t rd(bmp280_t *b, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(b->dev, &reg, 1, buf, n, BMP_TIMEOUT_MS);
}

static esp_err_t wr(bmp280_t *b, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(b->dev, buf, 2, BMP_TIMEOUT_MS);
}

esp_err_t bmp280_init(i2c_master_bus_handle_t bus, uint8_t addr, bmp280_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &out->dev);
    if (err != ESP_OK) return err;

    err = rd(out, REG_CHIP_ID, &out->chip_id, 1);
    if (err != ESP_OK) return err;
    if (out->chip_id != 0x58 && out->chip_id != 0x60) {
        ESP_LOGE(TAG, "Chip ID la 0x%02X (mong doi 0x58/0x60)", out->chip_id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Doc 24 byte he so hieu chinh T/P (little-endian) */
    uint8_t c[24] = { 0 };
    err = rd(out, REG_CALIB_START, c, sizeof(c));
    if (err != ESP_OK) return err;
    out->dig_T1 = (uint16_t)(c[0]  | (c[1]  << 8));
    out->dig_T2 = (int16_t) (c[2]  | (c[3]  << 8));
    out->dig_T3 = (int16_t) (c[4]  | (c[5]  << 8));
    out->dig_P1 = (uint16_t)(c[6]  | (c[7]  << 8));
    out->dig_P2 = (int16_t) (c[8]  | (c[9]  << 8));
    out->dig_P3 = (int16_t) (c[10] | (c[11] << 8));
    out->dig_P4 = (int16_t) (c[12] | (c[13] << 8));
    out->dig_P5 = (int16_t) (c[14] | (c[15] << 8));
    out->dig_P6 = (int16_t) (c[16] | (c[17] << 8));
    out->dig_P7 = (int16_t) (c[18] | (c[19] << 8));
    out->dig_P8 = (int16_t) (c[20] | (c[21] << 8));
    out->dig_P9 = (int16_t) (c[22] | (c[23] << 8));

    /* config: standby 500ms, IIR filter x4 */
    err = wr(out, REG_CONFIG, (4 << 5) | (2 << 2));
    if (err != ESP_OK) return err;
    /* ctrl_meas: oversampling T x2, P x16, normal mode */
    err = wr(out, REG_CTRL_MEAS, (2 << 5) | (5 << 2) | 3);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(100));   /* doi phep do dau tien (osrs_p x16 ~ 50ms) */

    ESP_LOGI(TAG, "%s OK (che do normal)", out->chip_id == 0x60 ? "BME280" : "BMP280");
    return ESP_OK;
}

esp_err_t bmp280_read(bmp280_t *b, float *temp_c, float *press_hpa)
{
    uint8_t d[6] = { 0 };
    esp_err_t err = rd(b, REG_DATA_START, d, sizeof(d));
    if (err != ESP_OK) return err;

    int32_t adc_P = (int32_t)(((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | ((uint32_t)d[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | ((uint32_t)d[5] >> 4));

    /* Cong thuc bu tu datasheet Bosch (so nguyen) */
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)b->dig_T1 << 1))) * ((int32_t)b->dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)b->dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)b->dig_T1))) >> 12) *
                    ((int32_t)b->dig_T3)) >> 14;
    int32_t t_fine = var1 + var2;
    *temp_c = (float)((t_fine * 5 + 128) >> 8) / 100.0f;

    int64_t v1 = (int64_t)t_fine - 128000;
    int64_t v2 = v1 * v1 * (int64_t)b->dig_P6;
    v2 = v2 + ((v1 * (int64_t)b->dig_P5) << 17);
    v2 = v2 + (((int64_t)b->dig_P4) << 35);
    v1 = ((v1 * v1 * (int64_t)b->dig_P3) >> 8) + ((v1 * (int64_t)b->dig_P2) << 12);
    v1 = ((((int64_t)1) << 47) + v1) * ((int64_t)b->dig_P1) >> 33;
    if (v1 == 0) {
        return ESP_ERR_INVALID_STATE;   /* tranh chia 0 */
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - v2) * 3125) / v1;
    v1 = (((int64_t)b->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (((int64_t)b->dig_P8) * p) >> 19;
    p = ((p + v1 + v2) >> 8) + (((int64_t)b->dig_P7) << 4);

    *press_hpa = (float)p / 256.0f / 100.0f;   /* Pa (Q24.8) -> hPa */
    return ESP_OK;
}
