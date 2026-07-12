/*
 * wifi_node.h - WiFi STA cho NODE, CHI dung khi nap OTA.
 * Luc hoat dong binh thuong node khong bat WiFi (tiet kiem dien, tranh xung
 * dot ADC2). Chi khi nhan lenh OTA moi bat WiFi -> tai firmware -> tat lai.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Bat WiFi STA va cho ket noi (toi da timeout_ms). Tu init netif/event loop
 * neu chua co. Tra ve ESP_OK khi da co IP. */
esp_err_t wifi_node_connect(const char *ssid, const char *pass, int timeout_ms);

/* Ngat WiFi + tat radio de tra node ve trang thai tiet kiem dien. */
void wifi_node_stop(void);

bool wifi_node_is_connected(void);
