/*
 * node_store.h - Kho du lieu node trong RAM cua Gateway (thay Firebase).
 * Giu: ban ghi moi nhat + lich su ngan tung node, trang thai online, va HANG DOI
 * LENH downlink cho tung node (App -> Gateway -> (LoRa) -> Node). Co mutex bao ve
 * vi truy cap tu 2 luong: lora_rx_task (ghi) va http server (doc/ghi lenh).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "packet.h"

#define STORE_MAX_NODES   16
#define STORE_HISTORY     120

typedef struct {
    uint32_t epoch;
    float    temp, hum, press, lux;
    uint16_t nh3, co2;
    uint8_t  fsm, act;
} hist_rec_t;

typedef struct {
    bool          used;
    uint8_t       node_id;
    app_payload_t last;          /* payload giai ma moi nhat */
    int16_t       rssi;
    float         snr;
    int64_t       last_seen_us;
    bool          online;
    uint8_t       mac[6];
    /* --- theo doi OTA (de web biet OTA thanh cong chua ma KHONG can cam node) --- */
    uint32_t      ota_cmd_epoch;   /* luc gateway gui lenh OTA (0 = chua) */
    uint16_t      ota_from_ver;    /* fw_ver cua node ngay truoc khi OTA */
    /* lich su vong */
    hist_rec_t    hist[STORE_HISTORY];
    int           h_head, h_count;
    /* hang doi lenh downlink (1 lenh cho -> gui ngay khi node uplink) */
    bool          cmd_pending;
    cmd_payload_t cmd;
} node_slot_t;

void node_store_init(void);

/* Cap nhat tu 1 goi uplink da giai ma. */
void node_store_update(const app_payload_t *pl, int16_t rssi, float snr);

/* So node "online" (thay tin hieu trong <= timeout_ms). */
int  node_store_online_count(uint32_t timeout_ms);
int  node_store_total(void);

/* Lay slot theo chi so (0..STORE_MAX_NODES-1); NULL neu trong. Chi doc nhanh. */
const node_slot_t *node_store_slot(int idx);

/* ---- JSON cho HTTP API (tra ve so byte da viet) ---- */
size_t node_store_json_list(char *buf, size_t n, uint32_t online_timeout_ms);
size_t node_store_json_history(uint8_t node_id, char *buf, size_t n);

/* ---- Hang doi lenh ---- */
/* App dat lenh cho node (ghi de lenh cu chua gui). cmd_seq tu tang. */
bool node_store_queue_cmd(uint8_t node_id, uint8_t cmd,
                          uint8_t act_mask, uint8_t act_val, const char *ota_url);
/* lora_rx_task lay lenh dang cho cho node (xoa khoi hang doi). true neu co. */
bool node_store_take_cmd(uint8_t node_id, cmd_payload_t *out);

/* Danh dau gateway vua gui lenh OTA cho node (luu fw_ver hien tai lam moc so sanh). */
void node_store_mark_ota(uint8_t node_id, uint32_t epoch);

/* Khoa/mo thu cong khi can build JSON dai (dung noi bo). */
void node_store_lock(void);
void node_store_unlock(void);
