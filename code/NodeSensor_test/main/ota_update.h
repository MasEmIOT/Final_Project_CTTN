/*
 * ota_update.h - Cap nhat firmware qua mang (OTA) theo mo hinh Hybrid (Ch4.4).
 *
 * Kich hoat bang lenh downlink LoRa (CMD_OTA + URL). Luong:
 *   bat WiFi -> esp_https_ota tai theo khoi (chunked, ~10KB RAM) -> verify
 *   SHA-256 -> esp_restart -> tu kiem tra ngoai vi -> mark valid / rollback.
 * Trong qua trinh tai, phat JSON tien trino ra USB de HIL EdgeProfiler theo doi.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Node ID hien tai (de gan vao JSON tien trinh OTA). */
void ota_set_node_id(uint8_t id);

/* Thuc hien OTA tu URL (http:// hoac https://). Blocking; ket thuc bang
 * esp_restart() neu thanh cong. Tra ve loi neu that bai (khong reset). */
esp_err_t ota_do_update(const char *url);

/* Goi som trong app_main: neu image dang o trang thai PENDING_VERIFY thi
 * chua confirm; sau khi tu kiem tra ngoai vi thanh cong hay goi ota_mark_valid(). */
bool ota_image_pending(void);

/* Xac nhan firmware moi chay tot -> huy rollback (o lai ban moi vinh vien). */
void ota_mark_valid(void);

/* Bao firmware moi loi -> chu dong rollback ve ban cu (reset ngay). */
void ota_rollback(void);
