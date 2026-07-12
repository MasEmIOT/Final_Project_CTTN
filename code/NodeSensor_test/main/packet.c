#include <string.h>
#include "packet.h"
#include "crypto.h"
#include "esp_random.h"

/* ---- Loi chung: seal 1 payload bat ky vao envelope voi magic chi dinh ---- */
static int env_seal(uint8_t magic0, uint8_t magic1,
                    const void *pl, size_t plen, uint8_t node_id,
                    uint8_t algo, const uint8_t key[16],
                    uint8_t *out, size_t *out_len)
{
    env_header_t *h = (env_header_t *)out;
    h->magic0 = magic0;
    h->magic1 = magic1;
    h->version = PKT_VERSION;
    h->algo = algo;
    h->node_id = node_id;
    h->plen = (uint8_t)plen;
    esp_fill_random(h->nonce, sizeof(h->nonce));

    /* AAD = 6 byte header dau (rang buoc toan ven header) */
    const uint8_t *aad = out;
    size_t aad_len = offsetof(env_header_t, nonce);

    uint8_t *ct = out + sizeof(env_header_t);
    uint8_t *tag = ct + h->plen;

    int r = crypto_aead_encrypt(algo, key, h->nonce, aad, aad_len,
                                (const uint8_t *)pl, plen, ct, tag);
    if (r != 0) {
        return r;
    }
    *out_len = sizeof(env_header_t) + h->plen + ENV_TAG_LEN;
    return 0;
}

/* ---- Loi chung: open 1 envelope voi magic chi dinh, do dai payload mong doi ---- */
static int env_open(uint8_t magic0, uint8_t magic1,
                    const uint8_t *buf, size_t len, size_t plen_expect,
                    const uint8_t key[16], void *pl_out, uint8_t *algo_out)
{
    if (len < sizeof(env_header_t) + ENV_TAG_LEN) {
        return -1;
    }
    const env_header_t *h = (const env_header_t *)buf;
    if (h->magic0 != magic0 || h->magic1 != magic1 || h->version != PKT_VERSION) {
        return -2;
    }
    if (h->plen != plen_expect) {
        return -3;
    }
    if (len != sizeof(env_header_t) + h->plen + ENV_TAG_LEN) {
        return -4;
    }

    const uint8_t *aad = buf;
    size_t aad_len = offsetof(env_header_t, nonce);
    const uint8_t *ct = buf + sizeof(env_header_t);
    const uint8_t *tag = ct + h->plen;

    int r = crypto_aead_decrypt(h->algo, key, h->nonce, aad, aad_len,
                                ct, h->plen, tag, (uint8_t *)pl_out);
    if (r != 0) {
        return -5;   /* sai tag / gia mao / loi giai ma */
    }
    if (algo_out) {
        *algo_out = h->algo;
    }
    return 0;
}

/* ================= UPLINK (app_payload_t) ================= */
int packet_seal(const app_payload_t *pl, uint8_t algo, const uint8_t key[16],
                uint8_t *out, size_t *out_len)
{
    return env_seal(ENV_MAGIC0, ENV_MAGIC1, pl, sizeof(app_payload_t),
                    pl->node_id, algo, key, out, out_len);
}

int packet_open(const uint8_t *buf, size_t len, const uint8_t key[16],
                app_payload_t *pl_out, uint8_t *algo_out)
{
    return env_open(ENV_MAGIC0, ENV_MAGIC1, buf, len, sizeof(app_payload_t),
                    key, pl_out, algo_out);
}

/* ================= DOWNLINK COMMAND (cmd_payload_t) ================= */
int cmd_seal(const cmd_payload_t *cp, uint8_t algo, const uint8_t key[16],
             uint8_t *out, size_t *out_len)
{
    return env_seal(CMD_MAGIC0, CMD_MAGIC1, cp, sizeof(cmd_payload_t),
                    cp->node_id, algo, key, out, out_len);
}

int cmd_open(const uint8_t *buf, size_t len, const uint8_t key[16],
             cmd_payload_t *cp_out, uint8_t *algo_out)
{
    return env_open(CMD_MAGIC0, CMD_MAGIC1, buf, len, sizeof(cmd_payload_t),
                    key, cp_out, algo_out);
}
