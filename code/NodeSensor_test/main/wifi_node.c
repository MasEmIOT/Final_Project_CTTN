#include <string.h>
#include "wifi_node.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_node";
static EventGroupHandle_t s_eg;
#define BIT_CONNECTED  BIT0
#define BIT_FAIL       BIT1

static bool s_inited = false;
static bool s_connected = false;
static int  s_retry = 0;
#define MAX_RETRY 8

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry++ < MAX_RETRY) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_eg, BIT_FAIL);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        s_connected = true;
        xEventGroupSetBits(s_eg, BIT_CONNECTED);
    }
}

esp_err_t wifi_node_connect(const char *ssid, const char *pass, int timeout_ms)
{
    if (!s_inited) {
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase(); nvs_flash_init();
        }
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));
        s_eg = xEventGroupCreate();
        s_inited = true;
    }
    s_retry = 0;
    xEventGroupClearBits(s_eg, BIT_CONNECTED | BIT_FAIL);

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Dang ket noi WiFi '%s' cho OTA...", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_eg, BIT_CONNECTED | BIT_FAIL,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONNECTED) {
        ESP_LOGI(TAG, "WiFi da ket noi (co IP)");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "WiFi ket noi that bai");
    return ESP_FAIL;
}

void wifi_node_stop(void)
{
    if (s_inited) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_connected = false;
        ESP_LOGI(TAG, "Da tat WiFi (tra ve tiet kiem dien)");
    }
}

bool wifi_node_is_connected(void) { return s_connected; }
