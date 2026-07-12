/* wifi_sta.h - Ket noi WiFi station, tu dong reconnect */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Khoi tao va bat dau ket noi (khong blocking). */
esp_err_t wifi_sta_start(const char *ssid, const char *pass);

/* Doi den khi co IP. timeout_ms = 0 -> doi mai. Tra ve true neu da ket noi. */
bool wifi_sta_wait_connected(uint32_t timeout_ms);

/* Kiem tra nhanh (khong block) - true neu dang co IP. */
bool wifi_sta_is_connected(void);
