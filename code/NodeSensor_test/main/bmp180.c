#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bmp180.h"

static const char *TAG = "bmp180";

#define BMP_TIMEOUT_MS     100

#define REG_CHIP_ID        0xD0   /* = 0x55 */
#define REG_SOFT_RESET     0xE0
#define REG_CTRL_MEAS      0xF4
#define REG_OUT_MSB        0xF6   /* MSB, LSB, (XLSB) */
#define REG_CALIB_START    0xAA   /* 22 byte = 11 he so 16-bit */

#define CMD_READ_TEMP      0x2E
#define CMD_READ_PRESS     0x34   /* + (oss << 6) */

static esp_err_t rd(bmp180_t *b, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(b->dev, &reg, 1, buf, n, BMP_TIMEOUT_MS);
}

static esp_err_t wr(bmp180_t *b, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(b->dev, buf, 2, BMP_TIMEOUT_MS);
}

static esp_err_t read_u16(bmp180_t *b, uint8_t reg, uint16_t *out)
{
    uint8_t d[2] = { 0 };
    esp_err_t err = rd(b, reg, d, 2);
    if (err != ESP_OK) return err;
    *out = (uint16_t)((d[0] << 8) | d[1]);
    return ESP_OK;
}

esp_err_t bmp180_init(i2c_master_bus_handle_t bus, uint8_t addr, bmp180_t *out)
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
    if (out->chip_id != 0x55) {
        ESP_LOGE(TAG, "Chip ID 0x%02X (mong doi 0x55) - khong phai BMP180", out->chip_id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Doc 22 byte he so hieu chuan trong 1 BURST tu 0xAA (on dinh hon voi
       module clone so voi nhieu lan doc 2 byte). MSB truoc. */
    uint8_t c[22] = { 0 };
    err = rd(out, REG_CALIB_START, c, sizeof(c));
    if (err != ESP_OK) return err;

    out->AC1 = (int16_t) ((c[0]  << 8) | c[1]);
    out->AC2 = (int16_t) ((c[2]  << 8) | c[3]);
    out->AC3 = (int16_t) ((c[4]  << 8) | c[5]);
    out->AC4 = (uint16_t)((c[6]  << 8) | c[7]);
    out->AC5 = (uint16_t)((c[8]  << 8) | c[9]);
    out->AC6 = (uint16_t)((c[10] << 8) | c[11]);
    out->B1  = (int16_t) ((c[12] << 8) | c[13]);
    out->B2  = (int16_t) ((c[14] << 8) | c[15]);
    out->MB  = (int16_t) ((c[16] << 8) | c[17]);
    out->MC  = (int16_t) ((c[18] << 8) | c[19]);
    out->MD  = (int16_t) ((c[20] << 8) | c[21]);

    /* EEPROM hop le neu khong co he so nao = 0x0000 hoac 0xFFFF */
    if (out->AC1 == 0 || out->AC1 == -1 || out->AC4 == 0 || out->AC4 == 0xFFFF ||
        out->AC5 == 0 || out->AC6 == 0) {
        ESP_LOGE(TAG, "He so hieu chuan khong hop le (day I2C/nguon?)");
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "BMP180 calib: AC1=%d AC2=%d AC3=%d AC4=%u AC5=%u AC6=%u",
             out->AC1, out->AC2, out->AC3, out->AC4, out->AC5, out->AC6);
    ESP_LOGI(TAG, "BMP180 calib: B1=%d B2=%d MB=%d MC=%d MD=%d (OSS=%d)",
             out->B1, out->B2, out->MB, out->MC, out->MD, BMP180_OSS);
    return ESP_OK;
}

esp_err_t bmp180_read(bmp180_t *b, float *temp_c, float *press_hpa)
{
    esp_err_t err;
    const int oss = BMP180_OSS;

    /* ---- Uncompensated temperature (UT) ---- */
    err = wr(b, REG_CTRL_MEAS, CMD_READ_TEMP);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(5));                 /* >= 4.5ms */
    uint16_t ut16 = 0;
    err = read_u16(b, REG_OUT_MSB, &ut16);
    if (err != ESP_OK) return err;
    int32_t UT = (int32_t)ut16;

    /* ---- Uncompensated pressure (UP) ---- */
    err = wr(b, REG_CTRL_MEAS, (uint8_t)(CMD_READ_PRESS + (oss << 6)));
    if (err != ESP_OK) return err;
    /* thoi gian chuyen doi: 4.5/7.5/13.5/25.5 ms theo oss = 0/1/2/3 */
    static const uint8_t conv_ms[4] = { 5, 8, 14, 26 };
    vTaskDelay(pdMS_TO_TICKS(conv_ms[oss & 3]));
    uint8_t d[3] = { 0 };
    err = rd(b, REG_OUT_MSB, d, 3);
    if (err != ESP_OK) return err;
    int32_t UP = (((int32_t)d[0] << 16) | ((int32_t)d[1] << 8) | d[2]) >> (8 - oss);

    /* ---- Bu theo datasheet Bosch (so nguyen) ---- */
    int32_t X1 = ((UT - (int32_t)b->AC6) * (int32_t)b->AC5) >> 15;
    int32_t X2 = ((int32_t)b->MC << 11) / (X1 + (int32_t)b->MD);
    int32_t B5 = X1 + X2;
    int32_t T  = (B5 + 8) >> 4;                   /* 0.1 do C */
    *temp_c = (float)T / 10.0f;

    int32_t B6 = B5 - 4000;
    X1 = ((int32_t)b->B2 * ((B6 * B6) >> 12)) >> 11;
    X2 = ((int32_t)b->AC2 * B6) >> 11;
    int32_t X3 = X1 + X2;
    int32_t B3 = ((((int32_t)b->AC1 * 4 + X3) << oss) + 2) >> 2;
    X1 = ((int32_t)b->AC3 * B6) >> 13;
    X2 = ((int32_t)b->B1 * ((B6 * B6) >> 12)) >> 16;
    X3 = ((X1 + X2) + 2) >> 2;
    uint32_t B4 = ((uint32_t)b->AC4 * (uint32_t)(X3 + 32768)) >> 15;
    uint32_t B7 = ((uint32_t)UP - B3) * (uint32_t)(50000 >> oss);
    int32_t p;
    if (B4 == 0) return ESP_ERR_INVALID_STATE;
    if (B7 < 0x80000000UL) {
        p = (int32_t)((B7 << 1) / B4);
    } else {
        p = (int32_t)((B7 / B4) << 1);
    }
    X1 = (p >> 8) * (p >> 8);
    X1 = (X1 * 3038) >> 16;
    X2 = (-7357 * p) >> 16;
    p  = p + ((X1 + X2 + 3791) >> 4);             /* ap suat Pa */

    *press_hpa = (float)p / 100.0f;
    return ESP_OK;
}
