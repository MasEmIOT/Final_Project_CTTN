#include <stdio.h>
#include <string.h>
#include "ota_update.h"
#include "wifi_node.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"

static const char *TAG = "ota";
static uint8_t s_node_id = 1;

/* WiFi cho OTA - dat trong menuconfig hoac sua fallback duoi */
#ifndef CONFIG_NODE_OTA_WIFI_SSID
#define CONFIG_NODE_OTA_WIFI_SSID "huu nam"
#endif
#ifndef CONFIG_NODE_OTA_WIFI_PASS
#define CONFIG_NODE_OTA_WIFI_PASS "matkhau987"
#endif

void ota_set_node_id(uint8_t id) { s_node_id = id; }

static void emit_ota(const char *state, int pct, const char *msg)
{
    printf("{\"type\":\"ota\",\"node\":%u,\"state\":\"%s\",\"pct\":%d,\"msg\":\"%s\"}\n",
           (unsigned)s_node_id, state, pct, msg ? msg : "");
    fflush(stdout);
}

esp_err_t ota_do_update(const char *url)
{
    ESP_LOGW(TAG, "=== BAT DAU OTA tu: %s ===", url);
    emit_ota("start", 0, url);

    if (wifi_node_connect(CONFIG_NODE_OTA_WIFI_SSID, CONFIG_NODE_OTA_WIFI_PASS, 20000) != ESP_OK) {
        emit_ota("error", 0, "wifi_fail");
        return ESP_FAIL;
    }
    emit_ota("wifi_ok", 5, NULL);

    esp_http_client_config_t http = {
        .url = url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    /* HTTPS: dung CA bundle (chong MITM). HTTP local server: khong can cert. */
    if (strncmp(url, "https://", 8) == 0) {
        http.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http,
    };

    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin loi: %s", esp_err_to_name(err));
        emit_ota("error", 5, "begin_fail");
        wifi_node_stop();
        return err;
    }

    int total = esp_https_ota_get_image_size(h);
    int last_pct = -1;
    while (1) {
        err = esp_https_ota_perform(h);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        int done = esp_https_ota_get_image_len_read(h);
        int pct = (total > 0) ? (5 + done * 90 / total) : 50;
        if (pct != last_pct && pct % 2 == 0) {   /* bot spam */
            emit_ota("downloading", pct, NULL);
            last_pct = pct;
        }
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        ESP_LOGE(TAG, "OTA tai loi/thieu du lieu: %s", esp_err_to_name(err));
        emit_ota("error", last_pct < 0 ? 50 : last_pct, "download_incomplete");
        esp_https_ota_abort(h);
        wifi_node_stop();
        return ESP_FAIL;
    }

    emit_ota("verifying", 96, "SHA-256");
    err = esp_https_ota_finish(h);   /* verify + set boot partition */
    wifi_node_stop();

    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Anh firmware KHONG hop le (SHA-256 sai)!");
            emit_ota("error", 96, "validate_failed");
        } else {
            ESP_LOGE(TAG, "esp_https_ota_finish loi: %s", esp_err_to_name(err));
            emit_ota("error", 96, "finish_fail");
        }
        return err;
    }

    ESP_LOGW(TAG, "=== OTA THANH CONG - khoi dong lai vao firmware moi ===");
    emit_ota("reboot", 100, "restarting");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;   /* khong bao gio toi day */
}

bool ota_image_pending(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK) {
        return st == ESP_OTA_IMG_PENDING_VERIFY;
    }
    return false;
}

void ota_mark_valid(void)
{
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "Firmware moi da duoc XAC NHAN hop le (huy rollback).");
        emit_ota("valid", 100, "confirmed");
    }
}

void ota_rollback(void)
{
    ESP_LOGE(TAG, "Firmware moi loi -> ROLLBACK ve ban cu.");
    emit_ota("rollback", 100, "reverting");
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
