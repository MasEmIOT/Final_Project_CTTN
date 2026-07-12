/*
 * http_server.h - Local Server tren Gateway (thay Firebase).
 * REST API cho App/Web (cung mang LAN):
 *   GET  /                 -> dashboard nhung san (tien kiem thu nhanh)
 *   GET  /api/status       -> trang thai gateway (heap, uptime, wifi, so node...)
 *   GET  /api/nodes        -> danh sach node + so lieu moi nhat (JSON)
 *   GET  /api/history?node=ID -> lich su 1 node
 *   POST /api/login        -> {user,pass} -> {role} (Admin/User)
 *   POST /api/cmd          -> dat lenh downlink (yeu cau header X-Token = admin pass)
 *                            body: {node,cmd,act_mask,act_val,url}
 */
#pragma once

#include "esp_err.h"

esp_err_t http_server_start(void);
void      http_server_set_ip(const char *ip);   /* de hien thi tren /api/status + OLED */
const char *http_server_get_ip(void);
