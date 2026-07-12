#include <string.h>
#include "mhz19.h"
#include "board_pins.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "mhz19";

#define MHZ_BAUD        9600
#define MHZ_TIMEOUT_MS  200
#define MHZ_BUF         64

/* Checksum MH-Z19: 0xFF - (sum byte 1..7) + 1 */
static uint8_t mhz_checksum(const uint8_t *p)
{
    uint8_t s = 0;
    for (int i = 1; i < 8; i++) s += p[i];
    return (uint8_t)(0xFF - s + 1);
}

esp_err_t mhz19_init(mhz19_t *out)
{
    memset(out, 0, sizeof(*out));
    out->port = CO2_UART_PORT;

    uart_config_t cfg = {
        .baud_rate = MHZ_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(out->port, MHZ_BUF * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(out->port, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(out->port, PIN_CO2_TX, PIN_CO2_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;

    /* Thu doc 1 lan de xac nhan cam bien co mat */
    uint16_t co2 = 0;
    if (mhz19_read_co2(out, &co2) == ESP_OK) {
        out->present = true;
        ESP_LOGI(TAG, "MH-Z19 OK (CO2=%u ppm) UART%d RX=%d TX=%d",
                 co2, out->port, PIN_CO2_RX, PIN_CO2_TX);
    } else {
        ESP_LOGW(TAG, "MH-Z19 chua phan hoi (kiem tra day RX/TX, cho warm-up)");
    }
    return ESP_OK;   /* van cho phep chay (cam bien co the warm-up cham) */
}

esp_err_t mhz19_read_co2(mhz19_t *s, uint16_t *co2_ppm)
{
    static const uint8_t cmd[9] = { 0xFF, 0x01, 0x86, 0, 0, 0, 0, 0, 0x79 };
    uart_flush_input(s->port);
    uart_write_bytes(s->port, (const char *)cmd, sizeof(cmd));

    uint8_t r[9] = { 0 };
    int n = uart_read_bytes(s->port, r, sizeof(r), pdMS_TO_TICKS(MHZ_TIMEOUT_MS));
    if (n != 9) return ESP_ERR_TIMEOUT;
    if (r[0] != 0xFF || r[1] != 0x86) return ESP_ERR_INVALID_RESPONSE;
    if (r[8] != mhz_checksum(r)) return ESP_ERR_INVALID_CRC;

    uint16_t co2 = (uint16_t)(r[2] << 8 | r[3]);
    if (co2_ppm) *co2_ppm = co2;
    return ESP_OK;
}

esp_err_t mhz19_set_abc(mhz19_t *s, bool enable)
{
    uint8_t cmd[9] = { 0xFF, 0x01, 0x79, enable ? 0xA0 : 0x00, 0, 0, 0, 0, 0 };
    cmd[8] = mhz_checksum(cmd);
    int w = uart_write_bytes(s->port, (const char *)cmd, sizeof(cmd));
    return (w == sizeof(cmd)) ? ESP_OK : ESP_FAIL;
}
