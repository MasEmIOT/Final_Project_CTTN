#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sx127x.h"

static const char *TAG = "sx127x";

/* ---------------- Thanh ghi SX127x (LoRa mode) ---------------- */
#define REG_FIFO                0x00
#define REG_OP_MODE             0x01
#define REG_FRF_MSB             0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB             0x08
#define REG_PA_CONFIG           0x09
#define REG_OCP                 0x0B
#define REG_LNA                 0x0C
#define REG_FIFO_ADDR_PTR       0x0D
#define REG_FIFO_TX_BASE        0x0E
#define REG_FIFO_RX_BASE        0x0F
#define REG_FIFO_RX_CURRENT     0x10
#define REG_IRQ_FLAGS           0x12
#define REG_RX_NB_BYTES         0x13
#define REG_PKT_SNR_VALUE       0x19
#define REG_PKT_RSSI_VALUE      0x1A
#define REG_MODEM_CONFIG1       0x1D
#define REG_MODEM_CONFIG2       0x1E
#define REG_PREAMBLE_MSB        0x20
#define REG_PREAMBLE_LSB        0x21
#define REG_PAYLOAD_LENGTH      0x22
#define REG_MODEM_CONFIG3       0x26
#define REG_SYNC_WORD           0x39
#define REG_DIO_MAPPING1        0x40
#define REG_VERSION             0x42
#define REG_PA_DAC              0x4D

/* RegOpMode */
#define OPMODE_LONG_RANGE       0x80
#define OPMODE_LOW_FREQ         0x08
#define MODE_SLEEP              0x00
#define MODE_STDBY              0x01
#define MODE_TX                 0x03
#define MODE_RX_CONT            0x05

/* RegIrqFlags */
#define IRQ_RX_DONE             0x40
#define IRQ_PAYLOAD_CRC_ERROR   0x20
#define IRQ_TX_DONE             0x08

#define SX_VERSION_EXPECTED     0x12    /* SX1276/77/78 */

static spi_device_handle_t s_spi;
static sx127x_config_t     s_cfg;
static uint8_t             s_opmode_base;   /* LONG_RANGE | (LOW_FREQ neu < 779MHz) */

/* ---------------- SPI helpers ---------------- */

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 16,
        .tx_data = { (uint8_t)(reg | 0x80), val, 0, 0 },
    };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 16,
        .tx_data = { (uint8_t)(reg & 0x7F), 0, 0, 0 },
    };
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    if (err == ESP_OK) {
        *val = t.rx_data[1];
    }
    return err;
}

static esp_err_t fifo_write(const uint8_t *data, uint8_t len)
{
    uint8_t buf[SX127X_MAX_PAYLOAD + 1];
    buf[0] = REG_FIFO | 0x80;
    memcpy(&buf[1], data, len);
    spi_transaction_t t = {
        .length = (size_t)(len + 1) * 8,
        .tx_buffer = buf,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t fifo_read(uint8_t *data, uint8_t len)
{
    uint8_t tx[SX127X_MAX_PAYLOAD + 1] = { REG_FIFO & 0x7F };
    uint8_t rx[SX127X_MAX_PAYLOAD + 1] = { 0 };
    spi_transaction_t t = {
        .length = (size_t)(len + 1) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    if (err == ESP_OK) {
        memcpy(data, &rx[1], len);
    }
    return err;
}

static esp_err_t set_mode(uint8_t mode)
{
    return reg_write(REG_OP_MODE, s_opmode_base | mode);
}

/* ---------------- API ---------------- */

esp_err_t sx127x_init(const sx127x_config_t *cfg)
{
    s_cfg = *cfg;
    s_opmode_base = OPMODE_LONG_RANGE | ((cfg->freq_hz < 779000000U) ? OPMODE_LOW_FREQ : 0);

    /* Chan reset */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->pin_rst,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    /* SPI bus + device */
    spi_bus_config_t bus = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = cfg->pin_miso,
        .sclk_io_num = cfg->pin_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SX127X_MAX_PAYLOAD + 8,
    };
    esp_err_t err = spi_bus_initialize(cfg->spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* INVALID_STATE = bus da init */
        return err;
    }

    if (s_spi == NULL) {   /* tranh add trung device khi init lai */
        spi_device_interface_config_t dev = {
            .mode = 0,
            .clock_speed_hz = 8 * 1000 * 1000,
            .spics_io_num = cfg->pin_nss,
            .queue_size = 2,
        };
        err = spi_bus_add_device(cfg->spi_host, &dev, &s_spi);
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Reset chip: keo RST xuong >=100us roi tha len, doi >=5ms */
    gpio_set_level(cfg->pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(cfg->pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Kiem tra version */
    uint8_t ver = 0;
    for (int i = 0; i < 5; i++) {
        if (reg_read(REG_VERSION, &ver) == ESP_OK && ver == SX_VERSION_EXPECTED) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (ver != SX_VERSION_EXPECTED) {
        ESP_LOGE(TAG, "Khong tim thay SX127x (RegVersion=0x%02X, mong doi 0x12). Kiem tra day SPI!", ver);
        return ESP_ERR_NOT_FOUND;
    }

    /* Vao LoRa mode (LongRangeMode chi doi duoc khi dang SLEEP) */
    ESP_ERROR_CHECK(reg_write(REG_OP_MODE, MODE_SLEEP));
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_ERROR_CHECK(reg_write(REG_OP_MODE, s_opmode_base | MODE_SLEEP));
    vTaskDelay(pdMS_TO_TICKS(2));

    /* Tan so: Frf = freq * 2^19 / 32MHz */
    uint64_t frf = ((uint64_t)cfg->freq_hz << 19) / 32000000ULL;
    ESP_ERROR_CHECK(reg_write(REG_FRF_MSB, (uint8_t)(frf >> 16)));
    ESP_ERROR_CHECK(reg_write(REG_FRF_MID, (uint8_t)(frf >> 8)));
    ESP_ERROR_CHECK(reg_write(REG_FRF_LSB, (uint8_t)(frf >> 0)));

    /* FIFO base */
    ESP_ERROR_CHECK(reg_write(REG_FIFO_TX_BASE, 0x00));
    ESP_ERROR_CHECK(reg_write(REG_FIFO_RX_BASE, 0x00));

    /* LNA boost */
    uint8_t lna = 0;
    ESP_ERROR_CHECK(reg_read(REG_LNA, &lna));
    ESP_ERROR_CHECK(reg_write(REG_LNA, lna | 0x03));

    /* Cong suat phat qua PA_BOOST (Ra-02 chi noi anten vao PA_BOOST) */
    int8_t p = cfg->tx_power_dbm;
    if (p > 20) p = 20;
    if (p < 2)  p = 2;
    if (p > 17) {
        /* che do +20dBm */
        ESP_ERROR_CHECK(reg_write(REG_PA_DAC, 0x87));
        ESP_ERROR_CHECK(reg_write(REG_OCP, 0x20 | 0x11));            /* ~140mA */
        ESP_ERROR_CHECK(reg_write(REG_PA_CONFIG, 0x80 | (uint8_t)(p - 5)));
    } else {
        ESP_ERROR_CHECK(reg_write(REG_PA_DAC, 0x84));
        ESP_ERROR_CHECK(reg_write(REG_OCP, 0x20 | 0x0F));            /* 120mA */
        ESP_ERROR_CHECK(reg_write(REG_PA_CONFIG, 0x80 | (uint8_t)(p - 2)));
    }

    /* ModemConfig1: BW | CR | explicit header */
    uint8_t bw_bits;
    switch (cfg->bw_hz) {
        case 250000: bw_bits = 8; break;
        case 500000: bw_bits = 9; break;
        case 125000:
        default:     bw_bits = 7; break;
    }
    uint8_t cr_bits = (uint8_t)(cfg->cr_denom - 4);   /* 4/5 -> 1 ... 4/8 -> 4 */
    if (cr_bits < 1) cr_bits = 1;
    if (cr_bits > 4) cr_bits = 4;
    ESP_ERROR_CHECK(reg_write(REG_MODEM_CONFIG1, (uint8_t)((bw_bits << 4) | (cr_bits << 1))));

    /* ModemConfig2: SF | CRC on */
    uint8_t sf = cfg->sf;
    if (sf < 7)  sf = 7;
    if (sf > 12) sf = 12;
    ESP_ERROR_CHECK(reg_write(REG_MODEM_CONFIG2, (uint8_t)((sf << 4) | 0x04)));

    /* ModemConfig3: AGC on + LowDataRateOptimize neu thoi gian symbol > 16ms */
    bool ldo = ((uint64_t)(1u << sf) * 1000ULL) > ((uint64_t)16 * cfg->bw_hz);
    ESP_ERROR_CHECK(reg_write(REG_MODEM_CONFIG3, (uint8_t)(0x04 | (ldo ? 0x08 : 0x00))));

    /* Preamble + sync word */
    ESP_ERROR_CHECK(reg_write(REG_PREAMBLE_MSB, (uint8_t)(cfg->preamble_len >> 8)));
    ESP_ERROR_CHECK(reg_write(REG_PREAMBLE_LSB, (uint8_t)(cfg->preamble_len & 0xFF)));
    ESP_ERROR_CHECK(reg_write(REG_SYNC_WORD, cfg->sync_word));

    ESP_ERROR_CHECK(set_mode(MODE_STDBY));

    ESP_LOGI(TAG, "SX127x OK: %lu Hz, SF%u, BW %lu Hz, CR 4/%u, %d dBm, sync 0x%02X",
             (unsigned long)cfg->freq_hz, (unsigned)sf,
             (unsigned long)cfg->bw_hz, (unsigned)cfg->cr_denom,
             (int)p, (unsigned)cfg->sync_word);
    return ESP_OK;
}

esp_err_t sx127x_send(const uint8_t *data, uint8_t len, uint32_t timeout_ms)
{
    if (len == 0 || len > SX127X_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = set_mode(MODE_STDBY);
    if (err != ESP_OK) return err;

    ESP_ERROR_CHECK(reg_write(REG_IRQ_FLAGS, 0xFF));          /* xoa co IRQ */
    ESP_ERROR_CHECK(reg_write(REG_FIFO_ADDR_PTR, 0x00));
    err = fifo_write(data, len);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(reg_write(REG_PAYLOAD_LENGTH, len));

    ESP_ERROR_CHECK(set_mode(MODE_TX));

    /* Doi TxDone - dung dong ho thuc (esp_timer) de KHONG phu thuoc tick rate */
    int64_t t0 = esp_timer_get_time();
    int64_t limit_us = (int64_t)timeout_ms * 1000;
    while ((esp_timer_get_time() - t0) < limit_us) {
        uint8_t irq = 0;
        if (reg_read(REG_IRQ_FLAGS, &irq) == ESP_OK && (irq & IRQ_TX_DONE)) {
            reg_write(REG_IRQ_FLAGS, 0xFF);
            set_mode(MODE_STDBY);
            return ESP_OK;
        }
        vTaskDelay(1);                 /* nhuong CPU 1 tick, khong busy-spin */
    }
    set_mode(MODE_STDBY);
    return ESP_ERR_TIMEOUT;
}

esp_err_t sx127x_start_rx(void)
{
    esp_err_t err = reg_write(REG_IRQ_FLAGS, 0xFF);
    if (err != ESP_OK) return err;
    return set_mode(MODE_RX_CONT);
}

esp_err_t sx127x_receive(uint8_t *buf, uint8_t max_len, uint8_t *out_len,
                         int16_t *out_rssi_dbm, float *out_snr_db)
{
    uint8_t irq = 0;
    esp_err_t err = reg_read(REG_IRQ_FLAGS, &irq);
    if (err != ESP_OK) return err;

    if (!(irq & IRQ_RX_DONE)) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_ERROR_CHECK(reg_write(REG_IRQ_FLAGS, 0xFF));

    if (irq & IRQ_PAYLOAD_CRC_ERROR) {
        ESP_LOGW(TAG, "Goi tin loi CRC (PHY), bo qua");
        return ESP_ERR_INVALID_CRC;
    }

    uint8_t nb = 0, cur = 0;
    ESP_ERROR_CHECK(reg_read(REG_RX_NB_BYTES, &nb));
    ESP_ERROR_CHECK(reg_read(REG_FIFO_RX_CURRENT, &cur));
    ESP_ERROR_CHECK(reg_write(REG_FIFO_ADDR_PTR, cur));

    if (nb > max_len || nb > SX127X_MAX_PAYLOAD) {
        ESP_LOGW(TAG, "Goi tin dai %u byte > buffer, bo qua", (unsigned)nb);
        return ESP_ERR_INVALID_SIZE;
    }
    err = fifo_read(buf, nb);
    if (err != ESP_OK) return err;
    *out_len = nb;

    /* RSSI/SNR cua goi vua nhan */
    uint8_t raw_rssi = 0, raw_snr = 0;
    ESP_ERROR_CHECK(reg_read(REG_PKT_RSSI_VALUE, &raw_rssi));
    ESP_ERROR_CHECK(reg_read(REG_PKT_SNR_VALUE, &raw_snr));
    int rssi_offset = (s_cfg.freq_hz < 779000000U) ? -164 : -157;
    if (out_rssi_dbm) *out_rssi_dbm = (int16_t)(rssi_offset + raw_rssi);
    if (out_snr_db)   *out_snr_db = ((float)(int8_t)raw_snr) * 0.25f;

    return ESP_OK;
}

esp_err_t sx127x_sleep(void)
{
    return set_mode(MODE_SLEEP);
}
