/*
 * gw_config.h - CHINH SUA NHANH O DAY
 * ====================================
 * Gateway ESP32 DevKit: LOCAL SERVER (da BO Firebase). Gateway noi WiFi de App/Web
 * trong cung mang truy cap qua IP gateway. Doi WiFi + tan so LoRa ngay tai day.
 */
#pragma once

/* ---------------- WiFi (gateway noi vao mang LAN de phuc vu App/Web) ---------------- */
#define GW_WIFI_SSID        "huu nam"
#define GW_WIFI_PASS        "matkhau987"

/* ---------------- Local HTTP Server ---------------- */
#define GW_HTTP_PORT        80
#define GW_MDNS_HOST        "gateway"     /* truy cap http://gateway.local neu bat mDNS */
#define GW_STORE_HISTORY    120           /* so ban ghi lich su giu trong RAM / node */

/* Tai khoan dang nhap App (phan quyen Admin/User) - kiem tra o tang App/Server */
#define GW_ADMIN_USER       "admin"
#define GW_ADMIN_PASS       "admin123"
#define GW_USER_USER        "user"
#define GW_USER_PASS        "user123"

/* ---------------- LoRa (PHAI GIONG NODE) ---------------- */
#define GW_LORA_FREQ_HZ     433000000
#define GW_LORA_SF          9

/* ---------------- Watchdog ---------------- */
#define GW_WDT_TIMEOUT_S    10
