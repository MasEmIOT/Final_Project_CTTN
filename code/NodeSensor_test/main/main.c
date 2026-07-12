/*
 * Node Sensor - ESP32-S3 + SHT30 + BMP180 + BH1750 -> LoRa SX1278 (Ra-02)
 *
 * Tinh nang:
 *   - SHT30 + BMP180 + BH1750, tu dong probe 2 bus I2C, IN dia chi I2C ra log.
 *   - Chu ky nhanh 3-4 giay (CONFIG_NODE_SEND_INTERVAL_S).
 *   - MA HOA goi tin (AES-128-GCM hoac ASCON-128, chon trong crypto_cfg.h).
 *   - Co che XAC NHAN (ACK) 2 chieu: gui -> doi ACK -> moi ket thuc chu ky.
 *   - Tu do: thoi gian xu ly (proc_us), thoi gian ma hoa (enc_us),
 *     round-trip (rtt_ms), uoc luong KHOANG CACH toi gateway tu RSSI.
 *     Cac so lieu nay duoc gui kem trong goi sau de gateway/tool theo doi.
 *   - HYBRID light-sleep (timer HOAC GPIO/DIO0 wake) + Watchdog.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"

#include "esp_mac.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "board_pins.h"
#include "app_version.h"
#include "packet.h"
#include "crypto.h"
#include "crypto_cfg.h"
#include "sx127x.h"
#include "sht3x.h"
#include "bmp180.h"
#include "bh1750.h"
#include "ds3231.h"
#include "mq135.h"
#include "mhz19.h"
#include "actuator.h"
#include "fsm.h"
#include "ota_update.h"
#include "rtos_stats.h"
#include "filters.h"

/* ---- Gia tri mac dinh phong khi chua chay menuconfig lai ---- */
#ifndef CONFIG_NODE_ID
#define CONFIG_NODE_ID                1
#endif
#ifndef CONFIG_NODE_SEND_INTERVAL_S
#define CONFIG_NODE_SEND_INTERVAL_S   4
#endif
#ifndef CONFIG_LORA_FREQ_HZ
#define CONFIG_LORA_FREQ_HZ           433000000
#endif
#ifndef CONFIG_LORA_SF
#define CONFIG_LORA_SF                9
#endif
#ifndef CONFIG_LORA_TX_POWER_DBM
#define CONFIG_LORA_TX_POWER_DBM      17
#endif
#ifndef CONFIG_NODE_ACK_TIMEOUT_MS
#define CONFIG_NODE_ACK_TIMEOUT_MS    800
#endif
#ifndef CONFIG_NODE_ACK_RETRIES
#define CONFIG_NODE_ACK_RETRIES       2
#endif
#ifndef CONFIG_NODE_WDT_TIMEOUT_S
#define CONFIG_NODE_WDT_TIMEOUT_S     10
#endif
/* Che do ngu: 0=none, 1=light, 2=deep */
#if defined(CONFIG_NODE_SLEEP_DEEP)
#define NODE_SLEEP_MODE   2
#define NODE_SLEEP_NAME   "deep"
#elif defined(CONFIG_NODE_SLEEP_NONE)
#define NODE_SLEEP_MODE   0
#define NODE_SLEEP_NAME   "none"
#else
#define NODE_SLEEP_MODE   1
#define NODE_SLEEP_NAME   "light"
#endif

#define LORA_TX_TIMEOUT_MS            3000
#define MIN_BACKOFF_SLEEP_MS         300    /* luon ngu it nhat ngan nay (tranh CPU 100%) */
#define MAX_BACKOFF_SLEEP_MS         30000  /* offline: ngu lau dan, toi da 30s (tiet kiem dien) */

/* ===== Bo loc RIENG cho tung tin hieu (theo dac tinh vat ly chuong trai) =====
 *  - Nhiet do & Do am (SHT30) -> EMA: thay doi cham (quan tinh nhiet lon), EMA bam
 *    xu huong & lam muot tot, chi ton 1 bien RAM -> sieu nhe cho MCU.
 *  - Ap suat (BMP180) -> Moving Average (SMA) cua so nho: ap suat on dinh, chi can
 *    "ui phang" gon nhieu dien tu li ti ma khong lam he thong phan ung cham.
 *  - Anh sang (BH1750) -> Median: gat bo nhieu DOT BIEN (bong nguoi/con trung bay qua,
 *    luoi che dung dua...) de lay muc sang nen thuc te.
 * Co the doi tung dong duoi day. Cua so MA/Median = FILTER_WIN (mac dinh 8). */
#define NODE_FILTER_TEMP    FILTER_EMA
#define NODE_FILTER_HUM     FILTER_EMA
#define NODE_FILTER_PRESS   FILTER_MA
#define NODE_FILTER_LUX     FILTER_MEDIAN

/* Mo hinh suy hao duong truyen de uoc luong khoang cach tu RSSI:
 *   RSSI(d) = RSSI_REF_1M - 10*n*log10(d)
 *   => d = 10^((RSSI_REF_1M - RSSI)/(10*n))   (d tinh bang met)
 * RSSI_REF_1M: RSSI do duoc o 1m (nen HIEU CHUAN theo thuc te).
 * PATH_LOSS_N: he so suy hao moi truong (2.0 free space ... 3-4 trong nha). */
#define RSSI_REF_1M     (-43.0)
#define PATH_LOSS_N     (2.7)

static const char *TAG = "node";

static i2c_master_bus_handle_t s_bus[2];

static sht3x_t  s_sht;
static bmp180_t s_bmp;
static bh1750_t s_bh;
static bool s_has_sht, s_has_bmp, s_has_bh;
static int  s_sht_bus = -1, s_bmp_bus = -1, s_bh_bus = -1;
static uint8_t s_sht_addr, s_bmp_addr, s_bh_addr;

/* Cam bien / khoi moi (v4) */
static ds3231_t s_rtc;
static mq135_t  s_mq;
static mhz19_t  s_co2;
static bool s_has_rtc, s_has_mq, s_has_co2;

/* Dinh danh: Node ID + MAC (doc tu efuse) */
static uint8_t s_node_id = CONFIG_NODE_ID;
static uint8_t s_mac[6];

/* Gia tri cam bien khi + FSM chu ky nay */
static uint16_t s_nh3_ppm, s_co2_ppm, s_ch4_ppm;
static uint8_t  s_fsm_state, s_act_state;

/* Thong tin firmware / OTA (bao cao len App/Web) */
static uint32_t s_ota_epoch = 0;              /* thoi diem firmware nay bat dau chay (Unix) */
static bool     s_ota_record_pending = false; /* can ghi ota_epoch khi da co gio hop le */

/* Doc/ghi ota_epoch trong NVS (giu qua mat dien) */
static void ota_epoch_load(void)
{
    nvs_handle_t h;
    if (nvs_open("otainfo", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "epoch", &s_ota_epoch);
        nvs_close(h);
    }
}
static void ota_epoch_save(uint32_t ep)
{
    nvs_handle_t h;
    if (nvs_open("otainfo", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "epoch", ep);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- OTA: kich hoat qua lenh downlink LoRa ---- */
static SemaphoreHandle_t s_ota_sem;
static char s_ota_url[CMD_OTA_URL_MAX];
static TaskHandle_t s_sensor_task_handle;

/* ---- Chong replay LENH downlink: chi nhan cmd_seq TANG dan ---- */
RTC_DATA_ATTR static uint32_t s_last_cmd_seq = 0;

/* So lieu mang chu ky truoc (RTC -> giu qua deep sleep) */
RTC_DATA_ATTR static uint16_t s_last_rtt_ms = 0;
RTC_DATA_ATTR static uint16_t s_last_dist_dm = 0;
RTC_DATA_ATTR static int16_t  s_last_dl_rssi = 0;
RTC_DATA_ATTR static uint16_t s_last_enc_us = 0;
RTC_DATA_ATTR static uint16_t s_seq = 0;     /* so thu tu goi (giu qua deep sleep) */

/* Bo dem & nhip do NODE tu tinh (edge computing) - RTC de giu qua deep sleep */
RTC_DATA_ATTR static uint32_t s_tx_total = 0;
RTC_DATA_ATTR static uint32_t s_ack_total = 0;
RTC_DATA_ATTR static uint32_t s_offline_streak = 0;  /* so chu ky offline lien tiep (backoff) */
static uint32_t s_last_cycle_ms = 0;
static uint8_t  s_cpu_pct = 0;        /* % CPU = awake/(awake+sleep) */
static uint32_t s_last_awake_ms = 0;  /* thoi gian THUC trong chu ky */
static uint32_t s_last_sleep_ms = 0;  /* thoi gian NGU trong chu ky */
static const char *s_sleep_used = NODE_SLEEP_NAME;   /* che do ngu THUC SU dung chu ky nay */

/* Bo loc du lieu (edge processing). Trang thai luu RTC de giu qua deep sleep. */
RTC_DATA_ATTR static filter_t s_filt_t, s_filt_h, s_filt_press, s_filt_lux;
static float s_f_t, s_f_h, s_f_press, s_f_lux;   /* gia tri DA LOC chu ky nay */
static char  s_filt_desc[48] = "?";              /* mo ta bo loc tung tin hieu */

/* Watchdog / reset tracking. RTC memory giu lai qua WDT/SW reset, mat khi mat dien. */
RTC_DATA_ATTR static uint32_t s_boot_count = 0;
RTC_DATA_ATTR static uint32_t s_wdt_resets = 0;
static const char *s_reset_reason = "?";

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        default:                return "OTHER";
    }
}

/* ===== LUU TRU OFFLINE (ring buffer FIFO, day thi bo ban ghi cu nhat) ===== */
#define OFFLINE_CAP             256   /* so ban ghi toi da */
#define OFFLINE_FLUSH_PER_CYCLE 10    /* gui lai toi da bao nhieu ban ghi / chu ky */

typedef struct { uint32_t epoch; app_payload_t pl; } offrec_t;
static offrec_t  s_off[OFFLINE_CAP];
static int       s_off_head = 0, s_off_tail = 0, s_off_count = 0;
static uint16_t  s_off_stored = 0, s_off_flushed = 0, s_off_dropped = 0;

/* Dong bo gio (Unix epoch) lay tu gateway qua ACK (RTC -> giu qua deep sleep) */
RTC_DATA_ATTR static uint32_t s_epoch_base = 0;
RTC_DATA_ATTR static int64_t  s_epoch_base_us = 0;

static uint32_t node_now_epoch(void)
{
    /* Uu tien RTC DS3231 phan cung (giu gio qua mat dien, khong le thuoc gateway) */
    if (s_has_rtc) {
        uint32_t ep = 0;
        if (ds3231_get_epoch(&s_rtc, &ep) == ESP_OK && ep > 1600000000) {
            return ep;
        }
    }
    /* Du phong: epoch mem dong bo tu ACK cua gateway */
    if (!s_epoch_base) return 0;
    int64_t now = esp_timer_get_time();
    if (now < s_epoch_base_us) return s_epoch_base;   /* sau deep sleep, timer reset */
    return s_epoch_base + (uint32_t)((now - s_epoch_base_us) / 1000000);
}

/* Them 1 ban ghi vao buffer. Neu day -> bo ban ghi CU NHAT (giai phong). */
static void off_push(const app_payload_t *pl, uint32_t epoch)
{
    if (s_off_count == OFFLINE_CAP) {
        s_off_tail = (s_off_tail + 1) % OFFLINE_CAP;
        s_off_count--;
        s_off_dropped++;
    }
    s_off[s_off_head].epoch = epoch;
    s_off[s_off_head].pl = *pl;
    s_off_head = (s_off_head + 1) % OFFLINE_CAP;
    s_off_count++;
    s_off_stored++;
}

/* ---------------- LED ---------------- */

static void leds_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LED_R) | (1ULL << PIN_LED_G) | (1ULL << PIN_LED_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_LED_R, !LED_ON_LEVEL);
    gpio_set_level(PIN_LED_G, !LED_ON_LEVEL);
    gpio_set_level(PIN_LED_B, !LED_ON_LEVEL);
}

static void led_blink(gpio_num_t pin, uint32_t ms)
{
    gpio_set_level(pin, LED_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(pin, !LED_ON_LEVEL);
}

/* ---------------- I2C + sensor ---------------- */

static void i2c_init(void)
{
    i2c_master_bus_config_t bus0 = {
        .i2c_port = 0,
        .sda_io_num = PIN_I2C0_SDA,
        .scl_io_num = PIN_I2C0_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus0, &s_bus[0]));

    i2c_master_bus_config_t bus1 = {
        .i2c_port = 1,
        .sda_io_num = PIN_I2C1_SDA,
        .scl_io_num = PIN_I2C1_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus1, &s_bus[1]));
}

static int find_on_buses(const uint8_t *addrs, int n_addr, uint8_t *out_addr)
{
    for (int bus = 0; bus < 2; bus++) {
        for (int i = 0; i < n_addr; i++) {
            if (i2c_master_probe(s_bus[bus], addrs[i], 50) == ESP_OK) {
                *out_addr = addrs[i];
                return bus;
            }
        }
    }
    return -1;
}

/* Quet va in toan bo dia chi I2C tim thay tren 2 bus (de tien check phan cung) */
static void i2c_scan_dump(void)
{
    for (int bus = 0; bus < 2; bus++) {
        char line[128];
        int n = snprintf(line, sizeof(line), "I2C scan bus%d:", bus);
        bool any = false;
        for (uint8_t a = 0x08; a < 0x78; a++) {
            if (i2c_master_probe(s_bus[bus], a, 20) == ESP_OK) {
                n += snprintf(line + n, sizeof(line) - n, " 0x%02X", a);
                any = true;
            }
        }
        if (!any) n += snprintf(line + n, sizeof(line) - n, " (trong)");
        ESP_LOGI(TAG, "%s", line);
    }
}

static void sensors_init(void)
{
    uint8_t addr = 0;
    int bus;

    /* Chi quet I2C dump o lan boot dau (tranh ton thoi gian thuc moi lan wake deep sleep) */
    if (s_boot_count <= 1) {
        i2c_scan_dump();
    }

    bus = find_on_buses((const uint8_t[]){ 0x44, 0x45 }, 2, &addr);
    if (bus >= 0 && sht3x_init(s_bus[bus], addr, &s_sht) == ESP_OK) {
        s_has_sht = true; s_sht_bus = bus; s_sht_addr = addr;
        ESP_LOGI(TAG, "SHT30  : bus%d, dia chi 0x%02X", bus, addr);
    } else {
        ESP_LOGW(TAG, "SHT30  : KHONG tim thay (kiem tra J18/J19)");
    }

    bus = find_on_buses((const uint8_t[]){ 0x77 }, 1, &addr);
    if (bus >= 0 && bmp180_init(s_bus[bus], addr, &s_bmp) == ESP_OK) {
        s_has_bmp = true; s_bmp_bus = bus; s_bmp_addr = addr;
        ESP_LOGI(TAG, "BMP180 : bus%d, dia chi 0x%02X", bus, addr);
    } else {
        ESP_LOGW(TAG, "BMP180 : KHONG tim thay (kiem tra J15, dia chi 0x77)");
    }

    bus = find_on_buses((const uint8_t[]){ 0x23, 0x5C }, 2, &addr);
    if (bus >= 0 && bh1750_init(s_bus[bus], addr, &s_bh) == ESP_OK) {
        s_has_bh = true; s_bh_bus = bus; s_bh_addr = addr;
        ESP_LOGI(TAG, "BH1750 : bus%d, dia chi 0x%02X", bus, addr);
    } else {
        ESP_LOGW(TAG, "BH1750 : KHONG tim thay (kiem tra J16/J17)");
    }

    /* RTC DS3231 tren bus I2C0 (SDA8/SCL14, dia chi 0x68) */
    if (ds3231_init(s_bus[0], &s_rtc) == ESP_OK) {
        s_has_rtc = true;
        uint32_t ep = 0; float rt = 0;
        ds3231_get_epoch(&s_rtc, &ep);
        ds3231_read_temp(&s_rtc, &rt);
        ESP_LOGI(TAG, "DS3231 : OK (epoch=%u, chip %.1fC)", (unsigned)ep, (double)rt);
    } else {
        ESP_LOGW(TAG, "DS3231 : KHONG tim thay (kiem tra J13, dia chi 0x68)");
    }

    ESP_LOGI(TAG, "== I2C MAP ==  SHT30=%s(0x%02X)  BMP180=%s(0x%02X)  BH1750=%s(0x%02X)  RTC=%s",
             s_has_sht ? "OK" : "--", s_sht_addr,
             s_has_bmp ? "OK" : "--", s_bmp_addr,
             s_has_bh  ? "OK" : "--", s_bh_addr,
             s_has_rtc ? "OK" : "--");

    /* MQ135 (NH3) tren ADC2_CH5 (IO16) */
    if (mq135_init(&s_mq) == ESP_OK) {
        s_has_mq = true;
        ESP_LOGI(TAG, "MQ135  : OK (IO16). Luu y warm-up >=30s de on dinh.");
    } else {
        ESP_LOGW(TAG, "MQ135  : khoi tao ADC that bai");
    }

    /* Cam bien CO2 UART (MH-Z19) tren UART1 (RX=IO5, TX=IO4) */
    if (mhz19_init(&s_co2) == ESP_OK) {
        s_has_co2 = s_co2.present;
        ESP_LOGI(TAG, "CO2    : UART1 (RX=IO5 TX=IO4) %s", s_has_co2 ? "OK" : "(cho warm-up)");
    }

    /* (actuator_init da chay som trong app_main de relay OFF ngay dau boot) */
}

/* ---------------- Uoc luong khoang cach ---------------- */

static uint16_t estimate_distance_dm(int rssi_dbm)
{
    double e = (RSSI_REF_1M - (double)rssi_dbm) / (10.0 * PATH_LOSS_N);
    double d_m = pow(10.0, e);
    if (d_m < 0) d_m = 0;
    if (d_m > 6000.0) d_m = 6000.0;
    return (uint16_t)(d_m * 10.0 + 0.5);   /* decimet */
}

/* ---------------- Xu ly LENH downlink (Gateway -> Node) ----------------
 * Goi CMD da ma hoa/xac thuc (cmd_open). Chong replay bang cmd_seq tang dan.
 * Ho tro: dieu khien actuator thu cong, tra ve auto, kich hoat OTA, reboot. */
static bool handle_downlink_cmd(const uint8_t *buf, size_t len)
{
    cmd_payload_t cp;
    uint8_t algo = 0;
    if (cmd_open(buf, len, CRYPTO_KEY, &cp, &algo) != 0) {
        return false;   /* khong phai lenh hop le / sai chu ky */
    }
    if (cp.node_id != s_node_id && cp.node_id != 0xFF) {
        return false;   /* khong danh cho node nay */
    }
    /* Chong replay: chi chap nhan lenh co cmd_seq LON HON lenh cuoi */
    if (cp.cmd_seq != 0 && cp.cmd_seq <= s_last_cmd_seq) {
        ESP_LOGW(TAG, "Bo lenh REPLAY (cmd_seq %u <= %u)",
                 (unsigned)cp.cmd_seq, (unsigned)s_last_cmd_seq);
        return true;
    }
    s_last_cmd_seq = cp.cmd_seq;

    switch (cp.cmd) {
        case CMD_ACT_SET: {
            actuator_set_manual(true);
            uint8_t m = actuator_get_mask();
            m = (uint8_t)((m & ~cp.act_mask) | (cp.act_val & cp.act_mask));
            actuator_set_mask(m);
            s_act_state = actuator_get_mask();
            ESP_LOGW(TAG, "LENH: dat actuator thu cong -> mask 0x%02X", s_act_state);
            led_blink(PIN_LED_B, 60);
            break;
        }
        case CMD_ACT_AUTO:
            actuator_set_manual(false);
            ESP_LOGW(TAG, "LENH: tra actuator ve che do FSM tu dong");
            break;
        case CMD_OTA:
            strncpy(s_ota_url, cp.ota_url, sizeof(s_ota_url) - 1);
            s_ota_url[sizeof(s_ota_url) - 1] = '\0';
            ESP_LOGW(TAG, "LENH: kich hoat OTA -> %s", s_ota_url);
            if (s_ota_sem) xSemaphoreGive(s_ota_sem);
            break;
        case CMD_REBOOT:
            ESP_LOGW(TAG, "LENH: reboot node");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
            break;
        case CMD_PING:
        default:
            led_blink(PIN_LED_G, 30);
            break;
    }
    return true;
}

/* ---------------- Gui kem ACK ---------------- */

/* Gui frame da seal, doi ACK. Tra ve true neu nhan ACK dung seq.
 * Xuat rtt_ms, dl_rssi (RSSI cua ACK), ul_rssi (RSSI gateway bao). */
static bool send_with_ack(const uint8_t *frame, size_t flen, uint16_t seq,
                          uint16_t *out_rtt_ms, int16_t *out_dl_rssi, int16_t *out_ul_rssi,
                          float *out_dl_snr, int max_retries)
{
    uint8_t buf[SX127X_MAX_PAYLOAD];

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        esp_task_wdt_reset();

        int64_t t_tx = esp_timer_get_time();
        esp_err_t err = sx127x_send(frame, (uint8_t)flen, LORA_TX_TIMEOUT_MS);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TX #%u that bai (%s), thu lai", (unsigned)seq, esp_err_to_name(err));
            continue;
        }

        if (sx127x_start_rx() != ESP_OK) {
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        int64_t limit_us = (int64_t)CONFIG_NODE_ACK_TIMEOUT_MS * 1000;
        while ((esp_timer_get_time() - t0) < limit_us) {
            uint8_t len = 0;
            int16_t rssi = 0;
            float snr = 0;
            esp_err_t r = sx127x_receive(buf, sizeof(buf), &len, &rssi, &snr);
            if (r == ESP_OK && len != sizeof(ack_packet_t) &&
                len >= sizeof(env_header_t) + ENV_TAG_LEN) {
                /* Goi khong phai ACK -> thu giai ma nhu LENH downlink */
                handle_downlink_cmd(buf, len);
            }
            if (r == ESP_OK && len == sizeof(ack_packet_t)) {
                ack_packet_t ack;
                memcpy(&ack, buf, sizeof(ack));
                if (ack_pkt_valid(&ack) && ack.node_id == s_node_id && ack.seq == seq) {
                    /* Dong bo gio tu gateway */
                    if (ack.epoch > 0) {
                        s_epoch_base = ack.epoch;
                        s_epoch_base_us = esp_timer_get_time();
                        /* Neu co RTC nhung chua duoc set gio -> nap gio that vao DS3231 */
                        if (s_has_rtc) {
                            uint32_t rep = 0;
                            if (ds3231_get_epoch(&s_rtc, &rep) != ESP_OK || rep < 1600000000) {
                                ds3231_set_epoch(&s_rtc, ack.epoch);
                            }
                        }
                    }
                    uint32_t rtt = (uint32_t)((esp_timer_get_time() - t_tx) / 1000);
                    if (rtt > 65000) rtt = 65000;
                    if (out_rtt_ms)  *out_rtt_ms = (uint16_t)rtt;
                    if (out_dl_rssi) *out_dl_rssi = rssi;
                    if (out_ul_rssi) *out_ul_rssi = ack.ul_rssi;
                    if (out_dl_snr)  *out_dl_snr = snr;
                    ESP_LOGI(TAG, "ACK #%u OK (dl_rssi %d, ul_rssi %d, rtt %u ms)",
                             (unsigned)seq, (int)rssi, (int)ack.ul_rssi, (unsigned)rtt);
                    return true;
                }
            }
            vTaskDelay(1);
            esp_task_wdt_reset();
        }
        ESP_LOGW(TAG, "Khong co ACK cho #%u (lan %d/%d)",
                 (unsigned)seq, attempt + 1, max_retries + 1);
    }
    return false;
}

/* Gui lai du lieu da luu offline (toi da N/chu ky). Tra ve so ban ghi da flush. */
static int flush_backlog(void)
{
    int flushed = 0;
    for (int i = 0; i < OFFLINE_FLUSH_PER_CYCLE && s_off_count > 0; i++) {
        offrec_t *rec = &s_off[s_off_tail];
        app_payload_t bp = rec->pl;
        bp.flags    |= PKT_F_BACKLOG;          /* danh dau du lieu luu lai */
        bp.epoch     = rec->epoch;             /* thoi diem GOC khi do */
        bp.buf_count = (uint16_t)s_off_count;
        bp.buf_cap   = OFFLINE_CAP;
        bp.buf_stored  = s_off_stored;
        bp.buf_flushed = s_off_flushed;
        bp.buf_dropped = s_off_dropped;

        uint8_t fr[ENV_MAX_LEN];
        size_t fl = 0;
        if (packet_seal(&bp, CRYPTO_ALGO, CRYPTO_KEY, fr, &fl) != 0) break;

        uint16_t rtt = 0; int16_t dl = 0, ul = 0;
        if (send_with_ack(fr, fl, bp.seq, &rtt, &dl, &ul, NULL, CONFIG_NODE_ACK_RETRIES)) {
            s_off_tail = (s_off_tail + 1) % OFFLINE_CAP;   /* giai phong ban ghi */
            s_off_count--;
            s_off_flushed++;
            flushed++;
            ESP_LOGI(TAG, "Flush offline seq#%u (epoch %u) -> con %d ban ghi",
                     (unsigned)bp.seq, (unsigned)bp.epoch, s_off_count);
        } else {
            ESP_LOGW(TAG, "Flush that bai, dung (con %d ban ghi)", s_off_count);
            break;
        }
        esp_task_wdt_reset();
    }
    return flushed;
}

/* ---------------- Hybrid light sleep ---------------- */

static void wake_pin_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_WAKE_EVENT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en   = (WAKE_EVENT_LEVEL == 0) ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE,
        .pull_down_en = (WAKE_EVENT_LEVEL == 0) ? GPIO_PULLDOWN_DISABLE: GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

/* Chan test WDT: input pull-up. Chap xuong GND (=0) de kich hoat test. */
static void wdt_test_pin_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_WDT_TEST,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

/* Neu chan test bi keo xuong 0 -> gia lap TREO: vong lap vo han khong reset WDT.
 * Task WDT se kich hoat va reset chip de chung minh co che watchdog hoat dong. */
static void wdt_test_check(void)
{
    if (gpio_get_level(PIN_WDT_TEST) == 0) {
        ESP_LOGE(TAG, "*** WDT TEST: gia lap TREO he thong - cho watchdog reset... ***");
        led_blink(PIN_LED_R, 100);
        /* Khong goi esp_task_wdt_reset() -> sau CONFIG_NODE_WDT_TIMEOUT_S giay
         * Task WDT se panic & reset chip. */
        for (;;) {
            __asm__ __volatile__("nop");   /* busy loop, khong feed watchdog */
        }
    }
}

#if NODE_SLEEP_MODE >= 1
static void hybrid_light_sleep(uint32_t sleep_ms)
{
    if (sleep_ms == 0) return;

    sx127x_start_rx();   /* arm radio de DIO0 keo len khi co goi den */

    esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * 1000ULL);
    gpio_wakeup_enable(PIN_WAKE_EVENT,
                       (WAKE_EVENT_LEVEL == 1) ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_task_wdt_reset();
    esp_light_sleep_start();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG, "** LIGHT-SLEEP: thuc day boi SU KIEN (GPIO/DIO0) **");
        led_blink(PIN_LED_B, 40);
    }
    esp_task_wdt_reset();
}
#endif

#if NODE_SLEEP_MODE == 2
/* DEEP SLEEP: chip tat nguon, RAM mat, reset khi wake (app_main chay lai).
 * Wake bang timer HOAC ext1 (DIO0 cua LoRa keo len) = hybrid deep sleep. */
static void enter_deep_sleep(uint32_t sleep_ms)
{
    if (sleep_ms == 0) sleep_ms = MIN_BACKOFF_SLEEP_MS;
    ESP_LOGW(TAG, "*** DEEP SLEEP %u ms (wake: timer hoac DIO0). Chip se reset khi day. ***",
             (unsigned)sleep_ms);
    sx127x_start_rx();   /* de DIO0 co the keo len khi co goi den */
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(20));   /* cho UART day het */

    esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * 1000ULL);
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_WAKE_EVENT, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();          /* KHONG tro lai */
}
#endif

/* Dispatcher: deep sleep CHI khi can_deep (online + buffer rong); con lai dung light
 * sleep de GIU RAM (buffer offline khong mat). */
static void node_sleep(uint32_t sleep_ms, bool can_deep)
{
#if NODE_SLEEP_MODE == 2
    if (can_deep) {
        enter_deep_sleep(sleep_ms);        /* khong tro lai */
    } else {
        hybrid_light_sleep(sleep_ms);      /* giu RAM cho buffer offline */
    }
#elif NODE_SLEEP_MODE == 1
    (void)can_deep;
    hybrid_light_sleep(sleep_ms);
#else
    (void)can_deep;
    vTaskDelay(pdMS_TO_TICKS(sleep_ms));
#endif
}

/* ---------------- Phat telemetry JSON ra USB cua NODE (doc lap gateway) ----
 * Tool cam thang vao NODE se doc duoc day du, KHONG can gateway.
 * Khi MAT KET NOI (online=0): cac thong so LoRa (rssi/snr/rtt/dist) = null. */
static void node_emit_telemetry(const app_payload_t *pl, bool online,
                                int16_t dl_rssi, float dl_snr, uint16_t rtt)
{
    char rssi_s[16], snr_s[16], rtt_s[16], dist_s[16];
    if (online) {
        snprintf(rssi_s, sizeof(rssi_s), "%d", (int)dl_rssi);
        snprintf(snr_s,  sizeof(snr_s),  "%.1f", (double)dl_snr);
        snprintf(rtt_s,  sizeof(rtt_s),  "%u", (unsigned)rtt);
        snprintf(dist_s, sizeof(dist_s), "%.1f", (double)pl->dist_dm / 10.0);
    } else {
        strcpy(rssi_s, "null"); strcpy(snr_s, "null");
        strcpy(rtt_s,  "null"); strcpy(dist_s, "null");
    }
    float t = (pl->flags & PKT_F_SHT30_OK) ? pl->sht_temp_c : pl->bmp_temp_c;

    printf("{\"type\":\"node\",\"src\":\"node\",\"node\":%u,"
           "\"sys\":{\"heap\":%u,\"c0_cpu\":%u,\"c1_cpu\":0},"
           "\"sensor\":{\"t\":%.1f,\"h\":%.1f,\"lux\":%.0f,\"press\":%.1f,\"bmp_t\":%.1f,"
           "\"nh3\":%u,\"co2\":%u,\"ch4\":%u},"
           "\"lora\":{\"link\":%d,\"rssi\":%s,\"snr\":%s,\"rtt\":%s},"
           "\"metrics\":{\"dist\":%s,\"proc\":%u,\"enc\":%u,\"pph\":%u,\"sps\":%.2f,"
           "\"tx\":%u,\"ack\":%u,\"algo\":\"%s\",\"thi\":%.1f,\"decide\":%u,\"act\":%u},"
           "\"buf\":{\"count\":%u,\"cap\":%u,\"stored\":%u,\"flushed\":%u,\"dropped\":%u},"
           "\"wdt\":{\"timeout_s\":%u,\"reset\":\"%s\",\"boot\":%u,\"wdt_resets\":%u},"
           "\"power\":{\"mode\":\"%s\",\"awake_ms\":%u,\"sleep_ms\":%u,\"cpu\":%u},"
           "\"filt\":{\"algo\":\"%s\",\"t\":%.2f,\"h\":%.1f,\"press\":%.2f,\"lux\":%.0f},"
           "\"fw\":{\"ver\":%u,\"ota_epoch\":%u},\"act_mode\":\"%s\",\"manual_left\":%u,"
           "\"epoch\":%u,\"fsm\":\"%s\",\"online\":%d}\n",
           (unsigned)pl->node_id,
           (unsigned)pl->heap_kb * 1024U, (unsigned)s_cpu_pct,
           (double)t, (double)pl->sht_hum_pct, (double)pl->lux,
           (double)pl->bmp_press_hpa, (double)pl->bmp_temp_c,
           (unsigned)pl->nh3_ppm, (unsigned)pl->co2_ppm, (unsigned)pl->ch4_ppm,
           online ? 1 : 0, rssi_s, snr_s, rtt_s,
           dist_s, (unsigned)pl->proc_us, (unsigned)pl->enc_us, (unsigned)pl->pph,
           (double)pl->sps_x100 / 100.0, (unsigned)pl->tx_total, (unsigned)pl->ack_total,
           crypto_algo_name(pl->algo),
           (double)pl->thi_x10 / 10.0, (unsigned)pl->decide_us, (unsigned)pl->act_state,
           (unsigned)s_off_count, (unsigned)OFFLINE_CAP, (unsigned)s_off_stored,
           (unsigned)s_off_flushed, (unsigned)s_off_dropped,
           (unsigned)CONFIG_NODE_WDT_TIMEOUT_S, s_reset_reason,
           (unsigned)s_boot_count, (unsigned)s_wdt_resets,
           s_sleep_used, (unsigned)s_last_awake_ms, (unsigned)s_last_sleep_ms, (unsigned)s_cpu_pct,
           s_filt_desc, (double)s_f_t, (double)s_f_h,
           (double)s_f_press, (double)s_f_lux,
           (unsigned)pl->fw_ver, (unsigned)pl->ota_epoch,
           (pl->act_mode == ACT_MODE_MANUAL) ? "manual" : "auto", (unsigned)pl->manual_left_s,
           (unsigned)node_now_epoch(), fsm_name(pl->fsm), online ? 1 : 0);
    fflush(stdout);
}

/* ---------------- Task chinh ---------------- */

static void sensor_cycle_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    uint16_t seq = s_seq;              /* tiep tuc seq tu RTC (giu qua deep sleep) */
    while (1) {
        int64_t t_start = esp_timer_get_time();
        esp_task_wdt_reset();
        wdt_test_check();                  /* chap GPIO0->GND de test watchdog */

        app_payload_t pl;
        memset(&pl, 0, sizeof(pl));
        pl.node_id = s_node_id;
        pl.seq = seq;
        pl.algo = CRYPTO_ALGO;

        float v1, v2;
        if (s_has_sht && sht3x_read(&s_sht, &v1, &v2) == ESP_OK) {
            pl.sht_temp_c = v1; pl.sht_hum_pct = v2; pl.flags |= PKT_F_SHT30_OK;
            ESP_LOGI(TAG, "SHT30  : %.2f C, %.1f %%RH", (double)v1, (double)v2);
        }
        if (s_has_bmp && bmp180_read(&s_bmp, &v1, &v2) == ESP_OK) {
            pl.bmp_temp_c = v1; pl.bmp_press_hpa = v2; pl.flags |= PKT_F_BMP180_OK;
            ESP_LOGI(TAG, "BMP180 : %.2f C, %.2f hPa", (double)v1, (double)v2);
        }
        if (s_has_bh && bh1750_read(&s_bh, &v1) == ESP_OK) {
            pl.lux = v1; pl.flags |= PKT_F_BH1750_OK;
            ESP_LOGI(TAG, "BH1750 : %.1f lux", (double)v1);
        }

        /* === SCHEDULING doc cam bien khi (tranh don dong/airtime) ===
         * MQ135 (ADC nhanh): doc moi chu ky. CO2 (UART ~200ms): doc luan phien
         * moi CO2_EVERY chu ky, giua cac lan giu gia tri cu -> giam tai & dien. */
        #define CO2_EVERY  3
        static uint32_t s_cyc = 0;
        s_cyc++;

        if (s_has_mq) {
            uint16_t nh3 = 0, ch4 = 0; int mv = 0;
            if (mq135_read(&s_mq, &nh3, &ch4, &mv) == ESP_OK) {
                s_nh3_ppm = nh3; s_ch4_ppm = ch4;
                pl.flags |= PKT_F_MQ135_OK;
                ESP_LOGI(TAG, "MQ135  : NH3 %u ppm, CH4 %u ppm (%d mV)", nh3, ch4, mv);
            }
        }
        if (s_has_co2 && (s_cyc % CO2_EVERY == 1)) {
            uint16_t co2 = 0;
            if (mhz19_read_co2(&s_co2, &co2) == ESP_OK) {
                s_co2_ppm = co2;
                pl.flags |= PKT_F_CO2_OK;
                ESP_LOGI(TAG, "CO2    : %u ppm", co2);
            }
        } else if (s_has_co2 && s_co2_ppm > 0) {
            pl.flags |= PKT_F_CO2_OK;   /* dung gia tri CO2 cu (chu ky khong doc) */
        }
        pl.nh3_ppm = s_nh3_ppm;
        pl.co2_ppm = s_co2_ppm;
        pl.ch4_ppm = s_ch4_ppm;
        memcpy(pl.mac, s_mac, 6);
        if (s_has_rtc) pl.flags |= PKT_F_RTC_OK;

        /* === LOC DU LIEU TAI NODE (edge processing) - raw -> filtered === */
        s_f_t     = filter_apply(&s_filt_t,     pl.sht_temp_c);
        s_f_h     = filter_apply(&s_filt_h,     pl.sht_hum_pct);
        s_f_press = filter_apply(&s_filt_press, pl.bmp_press_hpa);
        s_f_lux   = filter_apply(&s_filt_lux,   pl.lux);

        /* So lieu mang chu ky truoc */
        pl.rtt_ms  = s_last_rtt_ms;
        pl.dist_dm = s_last_dist_dm;
        pl.dl_rssi = s_last_dl_rssi;
        pl.enc_us  = s_last_enc_us;

        /* === NODE tu tinh toan pipeline (edge computing) === */
        s_tx_total++;                          /* chu ky nay = 1 goi gui */
        pl.tx_total  = (uint16_t)s_tx_total;
        pl.ack_total = (uint16_t)s_ack_total;
        uint32_t cyc_ms = s_last_cycle_ms ? s_last_cycle_ms
                                          : (uint32_t)CONFIG_NODE_SEND_INTERVAL_S * 1000U;
        pl.pph      = (uint16_t)((cyc_ms > 0) ? (3600000UL / cyc_ms) : 0);   /* goi/gio */
        pl.sps_x100 = (uint16_t)((cyc_ms > 0) ? (100000UL / cyc_ms) : 0);    /* sample/s x100 */
        pl.heap_kb  = (uint16_t)(esp_get_free_heap_size() / 1024);
        /* Manual het han 60s -> tu dong ve AUTO truoc khi FSM chay */
        actuator_tick();

        /* === EDGE COMPUTING: FSM ra quyet dinh + dieu khien actuator Act1..Act3 ===
         * Do luon THOI GIAN RA QUYET DINH (decide_us) - do tre thuat toan edge. */
        fsm_input_t fin = {
            .temp_c  = (pl.flags & PKT_F_SHT30_OK)  ? pl.sht_temp_c
                     : (pl.flags & PKT_F_BMP180_OK) ? pl.bmp_temp_c : 0.0f,
            .hum_pct = pl.sht_hum_pct,
            .nh3_ppm = s_nh3_ppm,
            .co2_ppm = s_co2_ppm,
            .have_th  = (pl.flags & (PKT_F_SHT30_OK | PKT_F_BMP180_OK)) != 0,
            .have_nh3 = (pl.flags & PKT_F_MQ135_OK) != 0,
            .have_co2 = (pl.flags & PKT_F_CO2_OK) != 0,
        };
        fsm_output_t fout;
        fsm_update(&fin, s_fsm_state, true, &fout);
        s_fsm_state  = fout.state;
        s_act_state  = fout.act_mask;
        pl.fsm       = fout.state;
        pl.thi_x10   = fout.thi_x10;
        pl.decide_us = fout.decide_us;
        pl.act_state = fout.act_mask;

        /* Che do actuator + thong tin firmware/OTA */
        pl.act_mode      = actuator_is_manual() ? ACT_MODE_MANUAL : ACT_MODE_AUTO;
        pl.manual_left_s = actuator_manual_left_s();
        pl.fw_ver        = NODE_FW_VERSION;

        /* Ghi thoi diem firmware nay bat dau chay (khi da co gio hop le tu RTC) */
        if (s_ota_record_pending) {
            uint32_t now = node_now_epoch();
            if (now > 1600000000) {
                s_ota_epoch = now;
                ota_epoch_save(now);
                s_ota_record_pending = false;
                ESP_LOGI(TAG, "Ghi thoi diem OTA/flash: epoch %u, fw v%d", (unsigned)now, NODE_FW_VERSION);
            }
        }
        pl.ota_epoch = s_ota_epoch;

        /* Thoi diem + trang thai buffer offline */
        pl.epoch       = node_now_epoch();
        pl.buf_count   = (uint16_t)s_off_count;
        pl.buf_cap     = OFFLINE_CAP;
        pl.buf_stored  = s_off_stored;
        pl.buf_flushed = s_off_flushed;
        pl.buf_dropped = s_off_dropped;

        /* proc_us = thoi gian doc sensor + dung goi (truoc khi ma hoa) */
        uint32_t proc_us = (uint32_t)(esp_timer_get_time() - t_start);
        if (proc_us > 65000) proc_us = 65000;
        pl.proc_us = (uint16_t)proc_us;

        /* Ma hoa (do thoi gian) */
        uint8_t frame[ENV_MAX_LEN];
        size_t flen = 0;
        int64_t e0 = esp_timer_get_time();
        int sr = packet_seal(&pl, CRYPTO_ALGO, CRYPTO_KEY, frame, &flen);
        uint32_t enc_us = (uint32_t)(esp_timer_get_time() - e0);
        if (enc_us > 65000) enc_us = 65000;
        s_last_enc_us = (uint16_t)enc_us;

        if (sr != 0) {
            ESP_LOGE(TAG, "Ma hoa loi (%d)", sr);
            led_blink(PIN_LED_R, 200);
            seq++; s_seq = seq;
            node_sleep(1000, false);
            continue;
        }

        ESP_LOGI(TAG, "Goi #%u (%s, %u byte) proc %u us, enc %u us",
                 (unsigned)seq, crypto_algo_name(CRYPTO_ALGO), (unsigned)flen,
                 (unsigned)proc_us, (unsigned)enc_us);

        /* Offline thi gui nhanh (it retry) de do ton CPU/dien; online thi retry day du */
        int retries = (s_offline_streak > 0) ? 0 : CONFIG_NODE_ACK_RETRIES;
        uint16_t rtt = 0; int16_t dl = 0, ul = 0; float dl_snr = 0;
        bool acked = send_with_ack(frame, flen, seq, &rtt, &dl, &ul, &dl_snr, retries);
        if (acked) {
            s_ack_total++;                     /* gui thanh cong */
            s_offline_streak = 0;
            s_last_rtt_ms = rtt;
            s_last_dl_rssi = dl;
            s_last_dist_dm = estimate_distance_dm(dl);
            pl.dist_dm = s_last_dist_dm;        /* cap nhat de telemetry phan anh */
            ESP_LOGI(TAG, "Chu ky #%u OK | rtt %u ms | ~%.1f m | TX %u/ACK %u | da xac nhan",
                     (unsigned)seq, (unsigned)rtt, (double)s_last_dist_dm / 10.0,
                     (unsigned)s_tx_total, (unsigned)s_ack_total);
            led_blink(PIN_LED_G, 80);

            /* Co ket noi -> GUI LAI toan bo du lieu da luu offline */
            if (s_off_count > 0) {
                int f = flush_backlog();
                if (f > 0) {
                    ESP_LOGI(TAG, "Da gui lai %d ban ghi offline (con %d)", f, s_off_count);
                }
            }
        } else {
            /* MAT KET NOI -> luu ban ghi vao bo nho cuc bo (kem thoi gian) */
            s_offline_streak++;
            off_push(&pl, pl.epoch ? pl.epoch : node_now_epoch());
            ESP_LOGW(TAG, "Chu ky #%u: MAT KET NOI (LoRa) -> luu OFFLINE (buffer %d/%d, da bo %u, streak %u)",
                     (unsigned)seq, s_off_count, OFFLINE_CAP, (unsigned)s_off_dropped,
                     (unsigned)s_offline_streak);
            led_blink(PIN_LED_R, 200);
        }
        seq++; s_seq = seq;

        /* ----- Quyet dinh che do ngu + thoi gian ----- */
        uint32_t cycle_ms = (uint32_t)CONFIG_NODE_SEND_INTERVAL_S * 1000U;
        uint32_t awake_ms = (uint32_t)((esp_timer_get_time() - t_start) / 1000);

        /* DEEP sleep CHI khi online, buffer rong, VA khong co actuator dang bat/manual
         * (chan IO38/42/45 khong phai RTC nen deep sleep se lam relay TAT -> nhay).
         * Khi actuator ON hoac dang manual -> light sleep (gpio_hold giu relay on). */
        bool can_deep = acked && (s_off_count == 0) &&
                        (s_act_state == 0) && !actuator_is_manual();

        /* Giu nhip lay mau ~chu ky o ca online va offline (luon ngu phan con lai).
         * Offline da fail nhanh (0 retry) nen awake nho -> CPU thap, van lay mau deu. */
        uint32_t sleep_ms = (awake_ms < cycle_ms) ? (cycle_ms - awake_ms) : MIN_BACKOFF_SLEEP_MS;
        (void)MAX_BACKOFF_SLEEP_MS;

#if   NODE_SLEEP_MODE == 2
        s_sleep_used = can_deep ? "deep" : "light";
#elif NODE_SLEEP_MODE == 1
        s_sleep_used = "light";
#else
        s_sleep_used = "none";
#endif
        s_last_awake_ms = awake_ms;
        s_last_sleep_ms = sleep_ms;
        s_last_cycle_ms = awake_ms + sleep_ms;
        s_cpu_pct = (uint8_t)((awake_ms + sleep_ms > 0)
                              ? (awake_ms * 100U / (awake_ms + sleep_ms)) : 0);

        /* Phat telemetry (kem thong tin sleep) TRUOC khi ngu */
        node_emit_telemetry(&pl, acked, dl, dl_snr, rtt);
        rtos_stats_emit_json(s_node_id);   /* danh sach task + %CPU tung core */

        ESP_LOGI(TAG, "Awake %u ms -> %s-sleep %u ms (CPU %u%%, off_streak %u, buf %d, heap %u)",
                 (unsigned)awake_ms, s_sleep_used, (unsigned)sleep_ms, (unsigned)s_cpu_pct,
                 (unsigned)s_offline_streak, s_off_count, (unsigned)esp_get_free_heap_size());
        node_sleep(sleep_ms, can_deep);   /* deep -> reset; light -> giu RAM */
    }
}

/* ---------------- Task OTA (kich hoat bang lenh downlink) ----------------
 * Cho semaphore -> tam dung task cam bien -> nap firmware -> thanh cong thi
 * esp_restart(); that bai thi khoi phuc hoat dong. Go task cam bien khoi Task
 * WDT trong luc nap (OTA lau hon timeout WDT) de tranh reset giua chung. */
static void ota_task(void *arg)
{
    (void)arg;
    while (1) {
        if (xSemaphoreTake(s_ota_sem, portMAX_DELAY) != pdTRUE) continue;
        ESP_LOGW(TAG, "*** OTA: tam dung doc cam bien, tat actuator an toan ***");
        actuator_set_manual(true);
        actuator_set_mask(0);                       /* tat chap hanh khi nap */
        if (s_sensor_task_handle) {
            esp_task_wdt_delete(s_sensor_task_handle);   /* khong bi WDT reset khi nap */
            vTaskSuspend(s_sensor_task_handle);
        }
        esp_err_t r = ota_do_update(s_ota_url);     /* thanh cong -> esp_restart() */
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "OTA that bai (%s) -> tiep tuc hoat dong", esp_err_to_name(r));
            if (s_sensor_task_handle) {
                vTaskResume(s_sensor_task_handle);
                esp_task_wdt_add(s_sensor_task_handle);
            }
            actuator_set_manual(false);
        }
    }
}

/* ---------------- main ---------------- */

void app_main(void)
{
    /* Dinh danh phan cung: doc MAC efuse, dan xuat Node ID (moi mach ID rieng) */
    crypto_get_mac(s_mac);
#if CONFIG_NODE_ID_FROM_MAC
    s_node_id = crypto_node_id_from_mac();
#else
    s_node_id = CONFIG_NODE_ID;
#endif

    /* NVS: doc thoi diem OTA/flash da luu (giu qua mat dien) */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ota_epoch_load();

    ESP_LOGI(TAG, "=== NODE SENSOR (id=%d, MAC %02X:%02X:%02X:%02X:%02X:%02X) ===",
             s_node_id, s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5]);
    ESP_LOGI(TAG, "Payload %u byte, envelope toi da %u byte, ma hoa: %s",
             (unsigned)sizeof(app_payload_t), (unsigned)ENV_MAX_LEN,
             crypto_algo_name(CRYPTO_ALGO));
    ESP_LOGI(TAG, "Chu ky %d s, ACK timeout %d ms, retries %d, sleep mode: %s",
             CONFIG_NODE_SEND_INTERVAL_S, CONFIG_NODE_ACK_TIMEOUT_MS,
             CONFIG_NODE_ACK_RETRIES, NODE_SLEEP_NAME);
#if NODE_SLEEP_MODE == 2
    ESP_LOGW(TAG, "DEEP SLEEP mode: chip reset moi chu ky; buffer offline (RAM) KHONG duoc giu.");
#endif

    /* Theo doi reset / watchdog */
    esp_reset_reason_t rr = esp_reset_reason();
    s_reset_reason = reset_reason_str(rr);
    s_boot_count++;
    if (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT) {
        s_wdt_resets++;
        ESP_LOGW(TAG, ">>> Node vua duoc WATCHDOG reset! (lan thu %u) <<<", (unsigned)s_wdt_resets);
    }
    ESP_LOGI(TAG, "Boot #%u, ly do reset truoc: %s, tong WDT resets: %u",
             (unsigned)s_boot_count, s_reset_reason, (unsigned)s_wdt_resets);

    /* Khoi tao bo loc RIENG tung tin hieu, CHI o lan boot dau
     * (deep sleep -> giu trang thai filter qua RTC). */
    if (s_boot_count <= 1) {
        filter_init(&s_filt_t, NODE_FILTER_TEMP);
        filter_init(&s_filt_h, NODE_FILTER_HUM);
        filter_init(&s_filt_press, NODE_FILTER_PRESS);
        filter_init(&s_filt_lux, NODE_FILTER_LUX);
    }
    snprintf(s_filt_desc, sizeof(s_filt_desc), "T=%s H=%s P=%s L=%s",
             filter_name(NODE_FILTER_TEMP), filter_name(NODE_FILTER_HUM),
             filter_name(NODE_FILTER_PRESS), filter_name(NODE_FILTER_LUX));
    ESP_LOGI(TAG, "Bo loc du lieu: %s", s_filt_desc);

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = CONFIG_NODE_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    if (esp_task_wdt_init(&wdt_cfg) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_reconfigure(&wdt_cfg);
    }

    leds_init();
    actuator_init();   /* SOM: dua relay ve OFF ngay dau boot (tranh chop khi active-low) */
    ESP_LOGI(TAG, "Actuator: Act1=IO%d Act2=IO%d Act3=IO%d, muc BAT=%d (mac dinh TAT)",
             PIN_ACT1, PIN_ACT2, PIN_ACT3, ACT_ON_LEVEL);
    wake_pin_init();
    wdt_test_pin_init();
    i2c_init();
    sensors_init();

    sx127x_config_t lora = {
        .spi_host = SPI2_HOST,
        .pin_sck  = PIN_LORA_SCK,
        .pin_miso = PIN_LORA_MISO,
        .pin_mosi = PIN_LORA_MOSI,
        .pin_nss  = PIN_LORA_NSS,
        .pin_rst  = PIN_LORA_RST,
        .freq_hz  = CONFIG_LORA_FREQ_HZ,
        .sf       = CONFIG_LORA_SF,
        .bw_hz    = LORA_BW_HZ,
        .cr_denom = LORA_CR_DENOM,
        .tx_power_dbm = CONFIG_LORA_TX_POWER_DBM,
        .sync_word    = LORA_SYNC_WORD,
        .preamble_len = 8,
    };
    while (sx127x_init(&lora) != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao LoRa that bai, thu lai sau 5s...");
        led_blink(PIN_LED_R, 200);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    /* ===== OTA rollback self-check (Rollback State Machine, Ch4.4) =====
     * Neu firmware dang o trang thai PENDING_VERIFY (vua nap qua OTA) va TOAN BO
     * ngoai vi (I2C/cam bien/LoRa) da khoi tao thanh cong toi day -> XAC NHAN hop le.
     * Neu bi treo trong luc init do loi ban moi, WDT reset -> bootloader ROLLBACK ban cu. */
    ota_set_node_id(s_node_id);
    if (ota_image_pending()) {
        ESP_LOGW(TAG, "Firmware moi (PENDING_VERIFY): ngoai vi OK -> mark_valid (huy rollback)");
        ota_mark_valid();
        s_ota_record_pending = true;   /* firmware moi -> ghi thoi diem khi co gio hop le */
    }

    /* Task OTA + semaphore (kich hoat boi lenh downlink CMD_OTA) */
    s_ota_sem = xSemaphoreCreateBinary();
    configASSERT(s_ota_sem != NULL);
    xTaskCreatePinnedToCore(ota_task, "ota", 8192, NULL, 6, NULL, 0);

    xTaskCreatePinnedToCore(sensor_cycle_task, "sensor_cycle", 8192, NULL, 5,
                            &s_sensor_task_handle, 0);
}
