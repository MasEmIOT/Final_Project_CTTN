/*
 * LoRa Gateway - ESP32 DevKit (Local Server, KHONG Firebase)
 *
 * Triet ly: NODE tinh toan toan bo (edge computing). Gateway:
 *   1) Nhan goi MA HOA, GIAI MA (AES/ASCON), KIEM TRA CHONG REPLAY (timestamp),
 *      tra ACK kem RSSI/SNR uplink + epoch dong bo gio.
 *   2) Luu vao KHO RAM (node_store) + phuc vu App/Web qua LOCAL HTTP SERVER.
 *   3) Gui LENH DOWNLINK (dieu khien actuator / OTA) tu App -> node qua LoRa.
 *   4) Hien thi trang thai tren OLED SSD1306; forward JSON ra USB cho HIL tool.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "board_pins.h"
#include "packet.h"
#include "crypto.h"
#include "crypto_cfg.h"
#include "sx127x.h"
#include "wifi_sta.h"
#include "cpu_load.h"
#include "serial_cmd.h"
#include "gw_config.h"
#include "node_store.h"
#include "http_server.h"
#include "ssd1306.h"

static const char *TAG = "gateway";

/* Loi mo phong tu HIL tool (chi dung khi chay test) */
static volatile bool s_sim_i2c_fault, s_sim_lora_jam, s_force_emergency;

static int64_t s_boot_us;
static volatile uint32_t s_replay_drops = 0;   /* so goi bi loai do nghi replay */

/* ---- Ho tro HIL kiem thu REPLAY ATTACK (khong can radio thu 2) ----
 * replay_capture: luu RAW byte cua goi hop le ke tiep.
 * replay_now: nap lai goi da luu qua duong giai ma + kiem tra chong replay,
 *   minh chung goi cu (epoch qua han) bi tu choi. */
static volatile bool s_replay_capture = false;
static uint8_t  s_replay_buf[SX127X_MAX_PAYLOAD];
static uint8_t  s_replay_len = 0;

static uint32_t gw_epoch_now(void);      /* forward decl */
static void replay_reinject(void);       /* forward decl */

/* ---------------- LED ---------------- */
static void leds_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LED_R),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_LED_R, !LED_ON_LEVEL);
}
static void led_blink(gpio_num_t pin, uint32_t ms)
{
    gpio_set_level(pin, LED_ON_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(pin, !LED_ON_LEVEL);
}

/* ---------------- Lenh tu HIL tool (mo phong loi) ---------------- */
static bool json_get_str(const char *json, const char *key, char *out, size_t n)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < n - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static void serial_on_line(const char *line)
{
    if (line[0] != '{') return;
    char cmd[32] = {0}, tgt[32] = {0};
    if (!json_get_str(line, "cmd", cmd, sizeof(cmd))) return;
    json_get_str(line, "target", tgt, sizeof(tgt));

    if (strcmp(cmd, "inject_fault") == 0) {
        if (strcmp(tgt, "i2c") == 0 || strcmp(tgt, "sht30") == 0) s_sim_i2c_fault = true;
        else if (strcmp(tgt, "lora") == 0) s_sim_lora_jam = true;
    } else if (strcmp(cmd, "jam_lora") == 0) {
        s_sim_lora_jam = true;
    } else if (strcmp(cmd, "force_fsm") == 0 || strcmp(cmd, "force_emergency") == 0) {
        s_force_emergency = true;
    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "clear_faults") == 0 ||
               strcmp(cmd, "reset") == 0) {
        s_sim_i2c_fault = s_sim_lora_jam = s_force_emergency = false;
    } else if (strcmp(cmd, "send_cmd") == 0) {
        /* HIL co the dat lenh downlink de test: {"cmd":"send_cmd","node":N,"op":C,"mask":M,"val":V,"url":".."} */
        char url[CMD_OTA_URL_MAX] = {0}; json_get_str(line, "url", url, sizeof(url));
        int node = 0, op = 0, mask = 0, val = 0;
        const char *pn = strstr(line, "\"node\"");  if (pn) node = atoi(pn + 7);
        const char *po = strstr(line, "\"op\"");    if (po) op   = atoi(po + 5);
        const char *pm = strstr(line, "\"mask\"");  if (pm) mask = atoi(pm + 7);
        const char *pv = strstr(line, "\"val\"");   if (pv) val  = atoi(pv + 6);
        node_store_queue_cmd((uint8_t)node, (uint8_t)op, (uint8_t)mask, (uint8_t)val,
                             url[0] ? url : NULL);
    } else if (strcmp(cmd, "replay_capture") == 0) {
        s_replay_capture = true;   /* bat 1 goi hop le ke tiep de test replay */
    } else if (strcmp(cmd, "replay_now") == 0) {
        replay_reinject();         /* nap lai goi da bat -> kiem tra chong replay */
    }
    char resp[160];
    snprintf(resp, sizeof(resp), "{\"resp\":\"ok\",\"cmd\":\"%s\",\"target\":\"%s\"}", cmd, tgt);
    serial_cmd_send_line(resp);
}

/* ---------------- Chuyen tiep 1 goi node ra telemetry JSON (cho HIL) ---------------- */
static void emit_node_line(const app_payload_t *pl, int16_t rssi, float snr)
{
    uint32_t pdr = (pl->tx_total > 0) ? ((uint32_t)pl->ack_total * 100U / pl->tx_total) : 100;

    const char *fsm = fsm_name(pl->fsm);
    if (s_force_emergency) fsm = "EMERGENCY";
    else if ((s_sim_i2c_fault || s_sim_lora_jam) && pl->fsm == FSM_SAFE) fsm = "WARN";
    int alert = (strcmp(fsm, "SAFE") != 0) ? 1 : 0;

    int16_t r = rssi; float sn = snr;
    if (s_sim_lora_jam) { r -= 40; sn -= 10.0f; if (pdr > 20) pdr = 20; }

    float sht_t = (pl->flags & PKT_F_SHT30_OK) ? pl->sht_temp_c : 0.0f;
    float hum   = (pl->flags & PKT_F_SHT30_OK) ? pl->sht_hum_pct : 0.0f;
    float bmp_t = (pl->flags & PKT_F_BMP180_OK) ? pl->bmp_temp_c : 0.0f;
    float press = (pl->flags & PKT_F_BMP180_OK) ? pl->bmp_press_hpa : 0.0f;
    float lux   = (pl->flags & PKT_F_BH1750_OK) ? pl->lux : 0.0f;
    float tshow = (pl->flags & PKT_F_SHT30_OK) ? sht_t : bmp_t;
    int backlog = (pl->flags & PKT_F_BACKLOG) ? 1 : 0;

    char line[760];
    snprintf(line, sizeof(line),
        "{\"type\":\"node\",\"src\":\"gw\",\"node\":%u,"
        "\"sys\":{\"heap\":%u,\"c0_cpu\":0,\"c1_cpu\":0},"
        "\"sensor\":{\"t\":%.1f,\"h\":%.1f,\"lux\":%.0f,\"press\":%.1f,\"bmp_t\":%.1f,"
        "\"nh3\":%u,\"co2\":%u,\"ch4\":%u},"
        "\"lora\":{\"rssi\":%d,\"snr\":%.1f,\"pdr\":%u,\"ul_rssi\":%d},"
        "\"metrics\":{\"rtt\":%u,\"dist\":%.1f,\"proc\":%u,\"enc\":%u,\"decide\":%u,\"pph\":%u,"
        "\"rx\":%u,\"sent\":%u,\"sps\":%.2f,\"thi\":%.1f,\"act\":%u,\"algo\":\"%s\"},"
        "\"buf\":{\"count\":%u,\"cap\":%u,\"stored\":%u,\"flushed\":%u,\"dropped\":%u},"
        "\"fw\":{\"ver\":%u,\"ota_epoch\":%u},\"act_mode\":\"%s\",\"manual_left\":%u,"
        "\"backlog\":%d,\"epoch\":%u,"
        "\"alert\":%d,\"fsm\":\"%s\",\"online\":1}",
        (unsigned)pl->node_id,
        (unsigned)pl->heap_kb * 1024U,
        (double)tshow, (double)hum, (double)lux, (double)press, (double)bmp_t,
        (unsigned)pl->nh3_ppm, (unsigned)pl->co2_ppm, (unsigned)pl->ch4_ppm,
        (int)r, (double)sn, (unsigned)pdr, (int)rssi,
        (unsigned)pl->rtt_ms, (double)pl->dist_dm / 10.0, (unsigned)pl->proc_us,
        (unsigned)pl->enc_us, (unsigned)pl->decide_us, (unsigned)pl->pph,
        (unsigned)pl->ack_total, (unsigned)pl->tx_total, (double)pl->sps_x100 / 100.0,
        (double)pl->thi_x10 / 10.0, (unsigned)pl->act_state, crypto_algo_name(pl->algo),
        (unsigned)pl->buf_count, (unsigned)pl->buf_cap, (unsigned)pl->buf_stored,
        (unsigned)pl->buf_flushed, (unsigned)pl->buf_dropped,
        (unsigned)pl->fw_ver, (unsigned)pl->ota_epoch,
        (pl->act_mode == ACT_MODE_MANUAL) ? "manual" : "auto", (unsigned)pl->manual_left_s,
        backlog, (unsigned)pl->epoch,
        alert, fsm);
    serial_cmd_send_line(line);
}

/* Bao su kien bao mat (replay) ra USB cho HIL tool */
static void sec_emit_replay(uint8_t node_id, uint32_t pkt_epoch, uint32_t gw_epoch)
{
    char l[160];
    snprintf(l, sizeof(l),
        "{\"type\":\"sec\",\"event\":\"replay_drop\",\"node\":%u,\"pkt_epoch\":%u,"
        "\"gw_epoch\":%u,\"drops\":%u}",
        node_id, (unsigned)pkt_epoch, (unsigned)gw_epoch, (unsigned)s_replay_drops);
    serial_cmd_send_line(l);
}

/* ---------------- Trang thai gateway (1s/lan) ---------------- */
static void gw_status_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    while (1) {
        uint8_t c0 = 0, c1 = 0;
        cpu_load_sample(&c0, &c1);
        char gw[300];
        snprintf(gw, sizeof(gw),
            "{\"type\":\"gw\",\"sys\":{\"heap\":%u,\"c0_cpu\":%u,\"c1_cpu\":%u},"
            "\"uptime\":%lld,\"wifi\":%d,\"ip\":\"%s\",\"nodes\":%d,\"replay_drops\":%u,\"algo\":\"%s\"}",
            (unsigned)esp_get_free_heap_size(), (unsigned)c0, (unsigned)c1,
            (long long)((esp_timer_get_time() - s_boot_us) / 1000000),
            wifi_sta_is_connected() ? 1 : 0, http_server_get_ip(),
            node_store_online_count(15000), (unsigned)s_replay_drops,
            crypto_algo_name(CRYPTO_ALGO));
        serial_cmd_send_line(gw);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* Lay Unix epoch hien tai; 0 neu chua dong bo SNTP. */
static uint32_t gw_epoch_now(void)
{
    time_t now = time(NULL);
    return (now > 1600000000) ? (uint32_t)now : 0;
}

/* Nap lai goi da bat qua duong giai ma + kiem tra chong replay (cho HIL test). */
static void replay_reinject(void)
{
    if (s_replay_len == 0) {
        serial_cmd_send_line("{\"type\":\"sec\",\"event\":\"replay_test\",\"result\":\"no_capture\"}");
        return;
    }
    app_payload_t pl; uint8_t algo = 0;
    if (packet_open(s_replay_buf, s_replay_len, CRYPTO_KEY, &pl, &algo) != 0) {
        serial_cmd_send_line("{\"type\":\"sec\",\"event\":\"replay_test\",\"result\":\"decode_fail\"}");
        return;
    }
    uint32_t gwep = gw_epoch_now();
    uint32_t age = (gwep > pl.epoch) ? (gwep - pl.epoch) : 0;
    bool blocked = (gwep > 0 && pl.epoch > 0 && age > REPLAY_WINDOW_S);
    if (blocked) s_replay_drops++;
    char l[200];
    snprintf(l, sizeof(l),
        "{\"type\":\"sec\",\"event\":\"replay_test\",\"node\":%u,\"age_s\":%u,"
        "\"window_s\":%u,\"result\":\"%s\",\"drops\":%u}",
        pl.node_id, (unsigned)age, (unsigned)REPLAY_WINDOW_S,
        blocked ? "blocked" : "accepted", (unsigned)s_replay_drops);
    serial_cmd_send_line(l);
    ESP_LOGW(TAG, "REPLAY-TEST: node %u age %us -> %s", pl.node_id, (unsigned)age,
             blocked ? "BLOCKED (chong replay OK)" : "ACCEPTED (con trong cua so)");
}

/* ---------------- LoRa: ACK + Downlink command ---------------- */
static void send_ack(uint8_t node_id, uint16_t seq, int16_t ul_rssi, float ul_snr, uint8_t status)
{
    ack_packet_t ack;
    int8_t snr8 = (int8_t)((ul_snr > 127) ? 127 : (ul_snr < -128 ? -128 : ul_snr));
    ack_pkt_seal(&ack, node_id, seq, ul_rssi, snr8, status, gw_epoch_now());
    sx127x_send((const uint8_t *)&ack, sizeof(ack), 1000);
    sx127x_start_rx();
}

/* Gui lenh downlink (da co trong hang doi) cho node. Tra ve true neu co gui. */
static bool send_downlink_if_any(uint8_t node_id)
{
    cmd_payload_t cp;
    if (!node_store_take_cmd(node_id, &cp)) return false;
    cp.epoch = gw_epoch_now();
    uint8_t frame[ENV_MAX_LEN];
    size_t flen = 0;
    if (cmd_seal(&cp, CRYPTO_ALGO, CRYPTO_KEY, frame, &flen) != 0) return false;
    sx127x_send(frame, (uint8_t)flen, 1000);
    ESP_LOGW(TAG, "Downlink -> node %u: cmd=%u mask=0x%02X val=0x%02X url=%s",
             node_id, cp.cmd, cp.act_mask, cp.act_val, cp.ota_url);
    if (cp.cmd == CMD_OTA) {
        /* Ghi moc de web/HIL biet OTA dang chay va so sanh fw_ver khi node quay lai */
        node_store_mark_ota(node_id, gw_epoch_now());
    }
    return true;
}

static void lora_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[SX127X_MAX_PAYLOAD];
    esp_task_wdt_add(NULL);
    ESP_ERROR_CHECK(sx127x_start_rx());
    ESP_LOGI(TAG, "Dang lang nghe LoRa (giai ma %s)...", crypto_algo_name(CRYPTO_ALGO));

    while (1) {
        esp_task_wdt_reset();
        uint8_t len = 0;
        int16_t rssi = 0;
        float snr = 0;
        esp_err_t err = sx127x_receive(buf, sizeof(buf), &len, &rssi, &snr);

        if (err == ESP_OK && len >= sizeof(env_header_t) + ENV_TAG_LEN) {
            app_payload_t pl;
            uint8_t algo = 0;
            if (packet_open(buf, len, CRYPTO_KEY, &pl, &algo) != 0) {
                ESP_LOGW(TAG, "Giai ma/xac thuc that bai - bo goi");
                vTaskDelay(1);
                continue;
            }

            /* HIL replay test: bat RAW byte cua goi hop le nay de nap lai sau */
            if (s_replay_capture) {
                memcpy(s_replay_buf, buf, len);
                s_replay_len = (uint8_t)len;
                s_replay_capture = false;
                ESP_LOGW(TAG, "REPLAY-TEST: da bat goi node %u (%u byte, epoch %u)",
                         pl.node_id, (unsigned)len, (unsigned)pl.epoch);
            }

            /* ===== CHONG REPLAY theo timestamp (Ch4.3.4) =====
             * Loai goi neu lech gio qua REPLAY_WINDOW_S, TRU goi backlog (offline gui lai). */
            uint32_t gwep = gw_epoch_now();
            bool is_backlog = (pl.flags & PKT_F_BACKLOG) != 0;
            if (!is_backlog && gwep > 0 && pl.epoch > 0) {
                uint32_t diff = (gwep > pl.epoch) ? (gwep - pl.epoch) : (pl.epoch - gwep);
                if (diff > REPLAY_WINDOW_S) {
                    s_replay_drops++;
                    ESP_LOGW(TAG, ">>> REPLAY? node %u lech %us (pkt %u vs gw %u) - BO GOI, khong ACK",
                             pl.node_id, (unsigned)diff, (unsigned)pl.epoch, (unsigned)gwep);
                    sec_emit_replay(pl.node_id, pl.epoch, gwep);
                    vTaskDelay(1);
                    continue;
                }
            }

            /* Luu kho RAM (phuc vu App/Web) */
            node_store_update(&pl, rssi, snr);

            /* Gui LENH downlink truoc (node dang o cua so RX cho ACK), roi ACK.
             * Node se xu ly lenh xong moi nhan ACK ket thuc chu ky. */
            bool sent_cmd = send_downlink_if_any(pl.node_id);
            send_ack(pl.node_id, pl.seq, rssi, snr,
                     sent_cmd ? ACK_STATUS_CMD : ACK_STATUS_OK);
            led_blink(PIN_LED_B, 8);

            /* Forward ra USB cho HIL */
            emit_node_line(&pl, rssi, snr);

            ESP_LOGI(TAG, "Node %u #%u | RSSI %d SNR %.1f | %s | THI %.1f FSM %s act 0x%02X | NH3 %u CO2 %u",
                     (unsigned)pl.node_id, (unsigned)pl.seq, (int)rssi, (double)snr,
                     crypto_algo_name(pl.algo), (double)pl.thi_x10 / 10.0,
                     fsm_name(pl.fsm), pl.act_state, pl.nh3_ppm, pl.co2_ppm);
        }
        vTaskDelay(1);
    }
}

/* ---------------- OLED trang thai (1s/lan) ---------------- */
static void oled_task(void *arg)
{
    (void)arg;
    if (!ssd1306_present()) vTaskDelete(NULL);
    while (1) {
        ssd1306_clear();
        char l[24];
        ssd1306_text(0, 0, "LoRa Farm Gateway");
        ssd1306_hline(1);
        snprintf(l, sizeof(l), "IP:%s", http_server_get_ip());
        ssd1306_text(0, 1, l);
        int online = node_store_online_count(15000);
        int total = node_store_total();
        snprintf(l, sizeof(l), "Node:%d/%d  WiFi:%s", online, total,
                 wifi_sta_is_connected() ? "OK" : "--");
        ssd1306_text(0, 2, l);

        int page = 3;
        for (int i = 0; i < STORE_MAX_NODES && page <= 7; i++) {
            const node_slot_t *s = node_store_slot(i);
            if (!s) continue;
            const app_payload_t *pl = &s->last;
            float t = (pl->flags & PKT_F_SHT30_OK) ? pl->sht_temp_c : pl->bmp_temp_c;
            snprintf(l, sizeof(l), "N%u %.0fC %u%% %s", pl->node_id, (double)t,
                     (unsigned)pl->sht_hum_pct, fsm_name(pl->fsm));
            ssd1306_text(0, page++, l);
        }
        ssd1306_flush();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---------------- main ---------------- */
void app_main(void)
{
    s_boot_us = esp_timer_get_time();
    ESP_LOGI(TAG, "=== LORA GATEWAY (ESP32 DevKit, Local Server, crypto %s) ===",
             crypto_algo_name(CRYPTO_ALGO));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = GW_WDT_TIMEOUT_S * 1000, .idle_core_mask = 0, .trigger_panic = true,
    };
    if (esp_task_wdt_init(&wdt_cfg) == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_reconfigure(&wdt_cfg);
    }

    leds_init();
    cpu_load_init();
    serial_cmd_init(serial_on_line);
    node_store_init();

    /* OLED (khong bat buoc - neu khong co van chay) */
    ssd1306_init();

    /* WiFi STA de vao mang LAN phuc vu App/Web */
    ESP_ERROR_CHECK(wifi_sta_start(GW_WIFI_SSID, GW_WIFI_PASS));

    /* SNTP: lay gio thuc (timestamp + chong replay + dong bo cho node qua ACK) */
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    sx127x_config_t lora = {
        .spi_host = SPI2_HOST,
        .pin_sck  = PIN_LORA_SCK, .pin_miso = PIN_LORA_MISO, .pin_mosi = PIN_LORA_MOSI,
        .pin_nss  = PIN_LORA_NSS, .pin_rst  = PIN_LORA_RST,
        .freq_hz  = GW_LORA_FREQ_HZ, .sf = GW_LORA_SF,
        .bw_hz    = LORA_BW_HZ, .cr_denom = LORA_CR_DENOM,
        .tx_power_dbm = 17, .sync_word = LORA_SYNC_WORD, .preamble_len = 8,
    };
    while (sx127x_init(&lora) != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao LoRa that bai, thu lai sau 5s...");
        led_blink(PIN_LED_R, 200);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    /* Lay IP + khoi dong Local HTTP Server sau khi co mang */
    if (wifi_sta_wait_connected(15000)) {
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip;
        if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
            char ipstr[20];
            snprintf(ipstr, sizeof(ipstr), IPSTR, IP2STR(&ip.ip));
            http_server_set_ip(ipstr);
        }
    }
    http_server_start();

    xTaskCreatePinnedToCore(lora_rx_task,   "lora_rx",   6144, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(gw_status_task, "gw_status", 4096, NULL, 4,  NULL, 0);
    xTaskCreatePinnedToCore(oled_task,      "oled",      4096, NULL, 3,  NULL, 0);
}
