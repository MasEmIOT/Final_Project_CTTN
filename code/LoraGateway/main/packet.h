/*
 * packet.h - Goi tin LoRa MA HOA giua Node va Gateway (v4).
 * FILE NAY + packet.c + crypto* + ascon* PHAI GIONG HET o ca 2 project.
 *
 *  UPLINK  (Node -> Gateway):  [env_header_t][ciphertext (plen byte)][tag 16]
 *      ciphertext la app_payload_t da ma hoa (AES-GCM hoac ASCON-128).
 *  ACK     (Gateway -> Node):  khong ma hoa, chi CRC8 (ack_packet_t).
 *  DOWNLINK COMMAND (Gateway -> Node, MOI o v4): [env_header_t magic=CMD]
 *      [ciphertext cmd_payload_t][tag 16] - dieu khien actuator / trigger OTA.
 *
 *  v4 bo sung:
 *   - Cam bien moi: NH3 (MQ135), CO2 (MH-Z19 UART), CH4 (uoc luong).
 *   - decide_us: do tre thuat toan FSM ra quyet dinh (edge computing).
 *   - act_state: trang thai 3 kenh chap hanh Act1..Act3.
 *   - mac[6]: dinh danh phan cung node (chong gia mao dinh danh).
 *   - G01 chong REPLAY: gateway kiem tra `epoch` (Delta t < REPLAY_WINDOW_S).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define PKT_VERSION         5
#define ENV_MAGIC0          0xA5
#define ENV_MAGIC1          0x5A
#define ACK_MAGIC0          0x5A
#define ACK_MAGIC1          0xA5
#define CMD_MAGIC0          0xC3   /* downlink command envelope */
#define CMD_MAGIC1          0x3C

/* Cua so chong replay (giay): gateway loai goi neu |gw_epoch - epoch| vuot nguong.
 * Goi luu offline (co PKT_F_BACKLOG) duoc MIEN tru vi mang timestamp cu hop le. */
#define REPLAY_WINDOW_S     5

/* Co sensor doc thanh cong */
#define PKT_F_SHT30_OK      (1u << 0)
#define PKT_F_BMP180_OK     (1u << 1)
#define PKT_F_BH1750_OK     (1u << 2)
#define PKT_F_BACKLOG       (1u << 3)   /* goi nay la du lieu LUU OFFLINE gui lai */
#define PKT_F_MQ135_OK      (1u << 4)   /* MQ135 (NH3) doc ok */
#define PKT_F_CO2_OK        (1u << 5)   /* cam bien CO2 UART doc ok */
#define PKT_F_RTC_OK        (1u << 6)   /* DS3231 RTC hop le (epoch tu RTC that) */

/* ====== Payload ung dung UPLINK (phan duoc MA HOA) ====== */
typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    uint16_t seq;
    uint8_t  flags;
    float    sht_temp_c;
    float    sht_hum_pct;
    float    bmp_temp_c;
    float    bmp_press_hpa;
    float    lux;
    uint16_t rtt_ms;        /* round-trip chu ky truoc (ms) */
    uint16_t dist_dm;       /* khoang cach uoc luong (decimet = 0.1m) */
    uint16_t proc_us;       /* thoi gian xu ly (doc sensor + ma hoa) (us) */
    uint16_t enc_us;        /* thoi gian ma hoa rieng (us) */
    int16_t  dl_rssi;       /* RSSI cua ACK ma node thu duoc (downlink) */
    uint8_t  algo;          /* thuat toan ma hoa dang dung */
    /* --- So lieu pipeline do NODE tu tinh (edge computing) --- */
    uint16_t tx_total;      /* tong so goi da gui */
    uint16_t ack_total;     /* tong so goi gui THANH CONG (co ACK) */
    uint16_t pph;           /* bang thong: goi/gio (node tu tinh) */
    uint16_t sps_x100;      /* toc do lay mau: sample/giay x100 */
    uint16_t heap_kb;       /* RAM trong cua node (KB) */
    uint8_t  fsm;           /* 0=SAFE, 1=WARN, 2=EMERGENCY (node tu quyet) */
    /* --- Luu tru offline --- */
    uint32_t epoch;         /* thoi diem do (Unix time, 0 = chua dong bo) */
    uint16_t buf_count;     /* so ban ghi dang nam trong buffer offline */
    uint16_t buf_cap;       /* dung luong toi da cua buffer (so ban ghi) */
    uint16_t buf_stored;    /* tong so ban ghi da tung luu */
    uint16_t buf_flushed;   /* tong so ban ghi da gui lai thanh cong */
    uint16_t buf_dropped;   /* tong so ban ghi cu bi bo do day */
    /* --- v4: cam bien khi + FSM/actuator + dinh danh --- */
    uint16_t nh3_ppm;       /* NH3 tu MQ135 (ppm) */
    uint16_t co2_ppm;       /* CO2 tu cam bien UART (ppm) */
    uint16_t ch4_ppm;       /* CH4 uoc luong tu MQ135 (ppm) */
    uint16_t thi_x10;       /* chi so nhiet am THI x10 (node tu tinh) */
    uint16_t decide_us;     /* THOI GIAN FSM RA QUYET DINH (us) - do tre edge */
    uint8_t  act_state;     /* bit0=Act1 bit1=Act2 bit2=Act3 (1=ON) */
    uint8_t  mac[6];        /* MAC efuse cua node (dinh danh phan cung) */
    /* --- v5: che do actuator + thong tin OTA --- */
    uint8_t  act_mode;      /* 0=AUTO (FSM tu quyet), 1=MANUAL (override tam thoi) */
    uint16_t manual_left_s; /* so giay con lai o che do manual (0 neu auto) */
    uint16_t fw_ver;        /* phien ban firmware node (NODE_FW_VERSION) */
    uint32_t ota_epoch;     /* thoi diem firmware nay bat dau chay (Unix, 0=chua ro) */
} app_payload_t;

/* Che do actuator */
#define ACT_MODE_AUTO    0
#define ACT_MODE_MANUAL  1
#define MANUAL_TIMEOUT_S 60   /* manual tu dong het sau 60s -> ve AUTO */

/* Ten trang thai FSM */
#define FSM_SAFE       0
#define FSM_WARN       1
#define FSM_EMERGENCY  2
static inline const char *fsm_name(uint8_t f)
{
    return (f == FSM_EMERGENCY) ? "EMERGENCY" : (f == FSM_WARN) ? "WARN" : "SAFE";
}

/* Bit trang thai actuator */
#define ACT1_BIT   (1u << 0)
#define ACT2_BIT   (1u << 1)
#define ACT3_BIT   (1u << 2)

/* ====== Header tren khong (cleartext, dung chung cho uplink & command) ====== */
typedef struct __attribute__((packed)) {
    uint8_t  magic0;        /* ENV_MAGIC0 (uplink) hoac CMD_MAGIC0 (command) */
    uint8_t  magic1;        /* ENV_MAGIC1 (uplink) hoac CMD_MAGIC1 (command) */
    uint8_t  version;       /* PKT_VERSION */
    uint8_t  algo;          /* CRYPTO_ALGO_xxx */
    uint8_t  node_id;       /* cleartext de loc/dinh tuyen */
    uint8_t  plen;          /* do dai plaintext */
    uint8_t  nonce[16];
} env_header_t;

#define ENV_TAG_LEN     16

/* ====== Downlink COMMAND payload (Gateway -> Node, MA HOA) ====== */
#define CMD_NOP        0
#define CMD_ACT_SET    1   /* dat trang thai actuator thu cong (override FSM tam thoi) */
#define CMD_ACT_AUTO   2   /* tra actuator ve che do FSM tu dong */
#define CMD_OTA        3   /* kich hoat OTA, ota_url la link firmware HTTPS */
#define CMD_REBOOT     4   /* khoi dong lai node */
#define CMD_PING       5   /* kiem tra song */

#define CMD_OTA_URL_MAX 64

typedef struct __attribute__((packed)) {
    uint8_t  node_id;       /* node dich (0xFF = broadcast) */
    uint8_t  cmd;           /* CMD_xxx */
    uint32_t epoch;         /* timestamp gateway (chong replay lenh) */
    uint32_t cmd_seq;       /* so thu tu lenh (chong replay lenh) */
    uint8_t  act_mask;      /* kenh muon tac dong (ACT1_BIT..ACT3_BIT) */
    uint8_t  act_val;       /* gia tri tuong ung (bit=1 -> ON) */
    char     ota_url[CMD_OTA_URL_MAX];  /* URL firmware khi cmd=CMD_OTA */
} cmd_payload_t;

/* Kich thuoc envelope toi da phai bao ca uplink lan command */
#define ENV_PL_MAX  ((sizeof(app_payload_t) > sizeof(cmd_payload_t)) ? \
                      sizeof(app_payload_t) : sizeof(cmd_payload_t))
#define ENV_MAX_LEN (sizeof(env_header_t) + ENV_PL_MAX + ENV_TAG_LEN)

/* ====== ACK (Gateway -> Node), khong ma hoa ====== */
#define ACK_STATUS_OK   0
#define ACK_STATUS_BUSY 1
#define ACK_STATUS_CMD  2   /* co lenh downlink kem theo trong chu ky nay */

typedef struct __attribute__((packed)) {
    uint8_t  magic0;        /* 0x5A */
    uint8_t  magic1;        /* 0xA5 */
    uint8_t  version;
    uint8_t  node_id;
    uint16_t seq;
    int16_t  ul_rssi;       /* RSSI gateway thu duoc tu node (uplink) */
    int8_t   ul_snr;        /* SNR (dB, lam tron) */
    uint8_t  status;
    uint32_t epoch;         /* Unix time cua gateway (dong bo gio cho node) */
    uint8_t  crc8;
} ack_packet_t;

/* CRC8 dung chung (poly 0x31, init 0xFF) */
static inline uint8_t pkt_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ====== ACK helpers ====== */
static inline void ack_pkt_seal(ack_packet_t *a, uint8_t node_id, uint16_t seq,
                                int16_t ul_rssi, int8_t ul_snr, uint8_t status,
                                uint32_t epoch)
{
    a->magic0 = ACK_MAGIC0;
    a->magic1 = ACK_MAGIC1;
    a->version = PKT_VERSION;
    a->node_id = node_id;
    a->seq = seq;
    a->ul_rssi = ul_rssi;
    a->ul_snr = ul_snr;
    a->status = status;
    a->epoch = epoch;
    a->crc8 = pkt_crc8((const uint8_t *)a, sizeof(*a) - 1);
}

static inline int ack_pkt_valid(const ack_packet_t *a)
{
    return a->magic0 == ACK_MAGIC0 && a->magic1 == ACK_MAGIC1 &&
           a->version == PKT_VERSION &&
           a->crc8 == pkt_crc8((const uint8_t *)a, sizeof(*a) - 1);
}

/* ====== Envelope seal/open (trong packet.c) ====== */

/* UPLINK: ma hoa app_payload -> out (toi thieu ENV_MAX_LEN byte). 0=OK */
int packet_seal(const app_payload_t *pl, uint8_t algo, const uint8_t key[16],
                uint8_t *out, size_t *out_len);

/* UPLINK: giai ma buf -> pl_out. *algo_out = thuat toan. 0=OK, !=0 loi/gia mao */
int packet_open(const uint8_t *buf, size_t len, const uint8_t key[16],
                app_payload_t *pl_out, uint8_t *algo_out);

/* DOWNLINK COMMAND: ma hoa cmd_payload -> out. 0=OK */
int cmd_seal(const cmd_payload_t *cp, uint8_t algo, const uint8_t key[16],
             uint8_t *out, size_t *out_len);

/* DOWNLINK COMMAND: giai ma buf -> cp_out. 0=OK, !=0 loi/gia mao */
int cmd_open(const uint8_t *buf, size_t len, const uint8_t key[16],
             cmd_payload_t *cp_out, uint8_t *algo_out);
