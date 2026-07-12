#include <stdio.h>
#include <string.h>
#include "node_store.h"
#include "crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static node_slot_t s_nodes[STORE_MAX_NODES];
static SemaphoreHandle_t s_mtx;
static uint32_t s_cmd_seq = 1;

void node_store_init(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));
    s_mtx = xSemaphoreCreateMutex();
}

void node_store_lock(void)   { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
void node_store_unlock(void) { if (s_mtx) xSemaphoreGive(s_mtx); }

static node_slot_t *find_or_create(uint8_t id)
{
    node_slot_t *free_slot = NULL;
    for (int i = 0; i < STORE_MAX_NODES; i++) {
        if (s_nodes[i].used && s_nodes[i].node_id == id) return &s_nodes[i];
        if (!s_nodes[i].used && !free_slot) free_slot = &s_nodes[i];
    }
    if (free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = true;
        free_slot->node_id = id;
    }
    return free_slot;
}

void node_store_update(const app_payload_t *pl, int16_t rssi, float snr)
{
    node_store_lock();
    node_slot_t *s = find_or_create(pl->node_id);
    if (s) {
        s->last = *pl;
        s->rssi = rssi;
        s->snr = snr;
        s->last_seen_us = esp_timer_get_time();
        s->online = true;
        memcpy(s->mac, pl->mac, 6);

        hist_rec_t *r = &s->hist[s->h_head];
        r->epoch = pl->epoch;
        r->temp  = (pl->flags & PKT_F_SHT30_OK) ? pl->sht_temp_c
                 : (pl->flags & PKT_F_BMP180_OK) ? pl->bmp_temp_c : 0;
        r->hum   = pl->sht_hum_pct;
        r->press = pl->bmp_press_hpa;
        r->lux   = pl->lux;
        r->nh3   = pl->nh3_ppm;
        r->co2   = pl->co2_ppm;
        r->fsm   = pl->fsm;
        r->act   = pl->act_state;
        s->h_head = (s->h_head + 1) % STORE_HISTORY;
        if (s->h_count < STORE_HISTORY) s->h_count++;
    }
    node_store_unlock();
}

int node_store_online_count(uint32_t timeout_ms)
{
    int c = 0;
    int64_t now = esp_timer_get_time();
    node_store_lock();
    for (int i = 0; i < STORE_MAX_NODES; i++) {
        if (s_nodes[i].used &&
            (now - s_nodes[i].last_seen_us) < (int64_t)timeout_ms * 1000) c++;
    }
    node_store_unlock();
    return c;
}

int node_store_total(void)
{
    int c = 0;
    for (int i = 0; i < STORE_MAX_NODES; i++) if (s_nodes[i].used) c++;
    return c;
}

const node_slot_t *node_store_slot(int idx)
{
    if (idx < 0 || idx >= STORE_MAX_NODES || !s_nodes[idx].used) return NULL;
    return &s_nodes[idx];
}

static const char *fsm_txt(uint8_t f)
{
    return (f == FSM_EMERGENCY) ? "EMERGENCY" : (f == FSM_WARN) ? "WARN" : "SAFE";
}

size_t node_store_json_list(char *buf, size_t n, uint32_t online_timeout_ms)
{
    int64_t now = esp_timer_get_time();
    size_t w = 0;
    w += snprintf(buf + w, n - w, "[");
    bool first = true;
    node_store_lock();
    for (int i = 0; i < STORE_MAX_NODES; i++) {
        node_slot_t *s = &s_nodes[i];
        if (!s->used) continue;
        const app_payload_t *pl = &s->last;
        bool online = (now - s->last_seen_us) < (int64_t)online_timeout_ms * 1000;
        w += snprintf(buf + w, n - w,
            "%s{\"node\":%u,\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"online\":%d,"
            "\"seq\":%u,\"temp\":%.1f,\"hum\":%.1f,\"press\":%.1f,\"lux\":%.0f,"
            "\"nh3\":%u,\"co2\":%u,\"ch4\":%u,\"thi\":%.1f,"
            "\"rssi\":%d,\"snr\":%.1f,\"rtt\":%u,\"dist_m\":%.1f,"
            "\"proc_us\":%u,\"enc_us\":%u,\"decide_us\":%u,\"pph\":%u,"
            "\"tx\":%u,\"ack\":%u,\"fsm\":\"%s\",\"act\":%u,"
            "\"fw_ver\":%u,\"ota_epoch\":%u,\"act_mode\":\"%s\",\"manual_left\":%u,"
            "\"ota_from\":%u,\"ota_cmd_epoch\":%u,"
            "\"buf\":%u,\"epoch\":%u,\"algo\":\"%s\"}",
            first ? "" : ",",
            (unsigned)pl->node_id,
            s->mac[0], s->mac[1], s->mac[2], s->mac[3], s->mac[4], s->mac[5],
            online ? 1 : 0, (unsigned)pl->seq,
            (double)((pl->flags & PKT_F_SHT30_OK) ? pl->sht_temp_c : pl->bmp_temp_c),
            (double)pl->sht_hum_pct, (double)pl->bmp_press_hpa, (double)pl->lux,
            (unsigned)pl->nh3_ppm, (unsigned)pl->co2_ppm, (unsigned)pl->ch4_ppm,
            (double)pl->thi_x10 / 10.0,
            (int)s->rssi, (double)s->snr, (unsigned)pl->rtt_ms, (double)pl->dist_dm / 10.0,
            (unsigned)pl->proc_us, (unsigned)pl->enc_us, (unsigned)pl->decide_us, (unsigned)pl->pph,
            (unsigned)pl->tx_total, (unsigned)pl->ack_total, fsm_txt(pl->fsm), (unsigned)pl->act_state,
            (unsigned)pl->fw_ver, (unsigned)pl->ota_epoch,
            (pl->act_mode == ACT_MODE_MANUAL) ? "manual" : "auto", (unsigned)pl->manual_left_s,
            (unsigned)s->ota_from_ver, (unsigned)s->ota_cmd_epoch,
            (unsigned)pl->buf_count, (unsigned)pl->epoch, crypto_algo_name(pl->algo));
        first = false;
        if (w > n - 450) break;   /* tranh tran buffer */
    }
    node_store_unlock();
    w += snprintf(buf + w, n - w, "]");
    return w;
}

size_t node_store_json_history(uint8_t node_id, char *buf, size_t n)
{
    size_t w = 0;
    w += snprintf(buf + w, n - w, "[");
    node_store_lock();
    node_slot_t *s = NULL;
    for (int i = 0; i < STORE_MAX_NODES; i++)
        if (s_nodes[i].used && s_nodes[i].node_id == node_id) { s = &s_nodes[i]; break; }
    if (s) {
        int start = (s->h_head - s->h_count + STORE_HISTORY) % STORE_HISTORY;
        for (int k = 0; k < s->h_count; k++) {
            hist_rec_t *r = &s->hist[(start + k) % STORE_HISTORY];
            w += snprintf(buf + w, n - w,
                "%s{\"epoch\":%u,\"temp\":%.1f,\"hum\":%.1f,\"press\":%.1f,\"lux\":%.0f,"
                "\"nh3\":%u,\"co2\":%u,\"fsm\":\"%s\",\"act\":%u}",
                k ? "" : "", (unsigned)r->epoch, (double)r->temp, (double)r->hum,
                (double)r->press, (double)r->lux, (unsigned)r->nh3, (unsigned)r->co2,
                fsm_txt(r->fsm), (unsigned)r->act);
            if (k < s->h_count - 1) w += snprintf(buf + w, n - w, ",");
            if (w > n - 200) break;
        }
    }
    node_store_unlock();
    w += snprintf(buf + w, n - w, "]");
    return w;
}

bool node_store_queue_cmd(uint8_t node_id, uint8_t cmd,
                          uint8_t act_mask, uint8_t act_val, const char *ota_url)
{
    node_store_lock();
    node_slot_t *s = find_or_create(node_id);
    bool ok = false;
    if (s) {
        memset(&s->cmd, 0, sizeof(s->cmd));
        s->cmd.node_id = node_id;
        s->cmd.cmd = cmd;
        s->cmd.cmd_seq = s_cmd_seq++;
        s->cmd.act_mask = act_mask;
        s->cmd.act_val = act_val;
        if (ota_url) strncpy(s->cmd.ota_url, ota_url, CMD_OTA_URL_MAX - 1);
        s->cmd_pending = true;
        ok = true;
    }
    node_store_unlock();
    return ok;
}

bool node_store_take_cmd(uint8_t node_id, cmd_payload_t *out)
{
    bool ok = false;
    node_store_lock();
    for (int i = 0; i < STORE_MAX_NODES; i++) {
        if (s_nodes[i].used && s_nodes[i].node_id == node_id && s_nodes[i].cmd_pending) {
            *out = s_nodes[i].cmd;
            s_nodes[i].cmd_pending = false;
            ok = true;
            break;
        }
    }
    node_store_unlock();
    return ok;
}

void node_store_mark_ota(uint8_t node_id, uint32_t epoch)
{
    node_store_lock();
    for (int i = 0; i < STORE_MAX_NODES; i++) {
        if (s_nodes[i].used && s_nodes[i].node_id == node_id) {
            s_nodes[i].ota_cmd_epoch = epoch;
            s_nodes[i].ota_from_ver = s_nodes[i].last.fw_ver;  /* moc version truoc OTA */
            break;
        }
    }
    node_store_unlock();
}
