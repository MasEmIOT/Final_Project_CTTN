#include <string.h>
#include "ds3231.h"
#include "board_pins.h"

#define DS_TIMEOUT_MS   100
#define REG_TIME        0x00   /* 0x00..0x06: s, min, hour, dow, date, month, year */
#define REG_TEMP        0x11   /* 0x11 MSB, 0x12 LSB */

static uint8_t bcd2dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static uint8_t dec2bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

/* ---- Chuyen doi lich <-> Unix epoch (UTC), khong phu thuoc mui gio he thong ----
 * Thuat toan days_from_civil (Howard Hinnant), chuan & gon cho MCU. */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)(yoe) + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yy + (*m <= 2);
}

esp_err_t ds3231_init(i2c_master_bus_handle_t bus, ds3231_t *out)
{
    memset(out, 0, sizeof(*out));
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &out->dev);
    if (err != ESP_OK) return err;

    /* Doc thu 1 byte de xac nhan chip phan hoi */
    uint8_t reg = REG_TIME, tmp = 0;
    err = i2c_master_transmit_receive(out->dev, &reg, 1, &tmp, 1, DS_TIMEOUT_MS);
    out->present = (err == ESP_OK);
    return err;
}

esp_err_t ds3231_get_epoch(ds3231_t *s, uint32_t *epoch)
{
    if (!s->present) return ESP_ERR_INVALID_STATE;
    uint8_t reg = REG_TIME, r[7] = { 0 };
    esp_err_t err = i2c_master_transmit_receive(s->dev, &reg, 1, r, sizeof(r), DS_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    unsigned sec   = bcd2dec(r[0] & 0x7F);
    unsigned mint  = bcd2dec(r[1] & 0x7F);
    unsigned hour  = bcd2dec(r[2] & 0x3F);       /* gia dinh che do 24h */
    unsigned date  = bcd2dec(r[4] & 0x3F);
    unsigned month = bcd2dec(r[5] & 0x1F);
    unsigned year  = 2000 + bcd2dec(r[6]);

    if (month < 1 || month > 12 || date < 1 || date > 31) return ESP_ERR_INVALID_RESPONSE;

    int64_t days = days_from_civil((int)year, month, date);
    int64_t t = days * 86400 + (int64_t)hour * 3600 + (int64_t)mint * 60 + sec;
    if (t < 0) return ESP_ERR_INVALID_RESPONSE;
    *epoch = (uint32_t)t;
    return ESP_OK;
}

esp_err_t ds3231_set_epoch(ds3231_t *s, uint32_t epoch)
{
    if (!s->present) return ESP_ERR_INVALID_STATE;
    int64_t days = epoch / 86400;
    int64_t rem  = epoch % 86400;
    unsigned hour = (unsigned)(rem / 3600);
    unsigned mint = (unsigned)((rem % 3600) / 60);
    unsigned sec  = (unsigned)(rem % 60);
    int y; unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    if (y < 2000 || y > 2099) return ESP_ERR_INVALID_ARG;

    /* day-of-week (1..7); khong bat buoc chinh xac nhung tinh cho gon */
    unsigned dow = (unsigned)(((days % 7) + 4) % 7) + 1;

    uint8_t buf[8];
    buf[0] = REG_TIME;
    buf[1] = dec2bcd(sec);
    buf[2] = dec2bcd(mint);
    buf[3] = dec2bcd(hour);          /* bit6=0 -> che do 24h */
    buf[4] = dec2bcd(dow);
    buf[5] = dec2bcd(d);
    buf[6] = dec2bcd(m);
    buf[7] = dec2bcd((uint8_t)(y - 2000));
    return i2c_master_transmit(s->dev, buf, sizeof(buf), DS_TIMEOUT_MS);
}

esp_err_t ds3231_read_temp(ds3231_t *s, float *temp_c)
{
    if (!s->present) return ESP_ERR_INVALID_STATE;
    uint8_t reg = REG_TEMP, r[2] = { 0 };
    esp_err_t err = i2c_master_transmit_receive(s->dev, &reg, 1, r, sizeof(r), DS_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    int16_t raw = (int16_t)((r[0] << 8) | r[1]);
    *temp_c = (float)(raw >> 6) * 0.25f;   /* 10-bit, 0.25 C/LSB */
    return ESP_OK;
}
