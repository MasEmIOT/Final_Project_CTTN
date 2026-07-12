#include <string.h>
#include <stdlib.h>
#include "crypto.h"
#include "crypto_cfg.h"
#include "ascon.h"

// Thư viện mới của ESP-IDF v6.0+ thay thế cho mbedtls/gcm.h
#include "psa/crypto.h"
#include "esp_mac.h"

void crypto_get_mac(uint8_t mac[6])
{
    /* MAC WiFi STA = MAC goc efuse, on dinh suot doi chip. */
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        memset(mac, 0, 6);
    }
}

uint8_t crypto_node_id_from_mac(void)
{
    uint8_t mac[6];
    crypto_get_mac(mac);
    /* Tron 6 byte MAC -> 1 byte, ep ve dai 1..254 (tranh 0 va 255=broadcast). */
    uint8_t x = 0;
    for (int i = 0; i < 6; i++) x ^= mac[i];
    uint8_t id = (uint8_t)(x % 254) + 1;
    return id;
}

void crypto_derive_key(const uint8_t master[16], const uint8_t mac[6], uint8_t out_key[16])
{
    /* HMAC-SHA256(master, mac) qua PSA (mbedtls 4 da bo mbedtls_md_hmac one-shot).
     * Lay 16 byte dau lam khoa rieng cho node. Loi -> fallback dung khoa chia se. */
    memcpy(out_key, master, 16);   /* mac dinh/fallback */

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attr, 128);

    psa_key_id_t kid = 0;
    if (psa_import_key(&attr, master, 16, &kid) != PSA_SUCCESS) {
        return;
    }
    uint8_t full[32];
    size_t olen = 0;
    if (psa_mac_compute(kid, PSA_ALG_HMAC(PSA_ALG_SHA_256), mac, 6,
                        full, sizeof(full), &olen) == PSA_SUCCESS && olen >= 16) {
        memcpy(out_key, full, 16);
    }
    psa_destroy_key(kid);
}

const char *crypto_algo_name(uint8_t algo)
{
    switch (algo) {
        case CRYPTO_ALGO_AES:   return "AES-128-GCM";
        case CRYPTO_ALGO_ASCON: return "ASCON-128";
        default:                return "NONE";
    }
}

static int aes_gcm_enc(const uint8_t *key, const uint8_t *nonce,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *pt, size_t pt_len,
                       uint8_t *ct, uint8_t *tag)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    psa_status_t status;

    // 1. Cấu hình thuộc tính cho AES-128 GCM
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);

    // 2. Import Key
    status = psa_import_key(&attributes, key, 16, &key_id); // 128 bits = 16 bytes
    if (status != PSA_SUCCESS) {
        return (int)status;
    }

    // 3. Cấp phát bộ đệm tạm vì PSA gộp chung ciphertext và tag
    size_t buf_size = pt_len + CRYPTO_TAG_LEN;
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        psa_destroy_key(key_id);
        return -1; // Lỗi cấp phát bộ nhớ
    }

    // 4. Thực thi mã hóa
    size_t out_len = 0;
    status = psa_aead_encrypt(key_id, PSA_ALG_GCM,
                              nonce, CRYPTO_NONCE_LEN,
                              aad, aad_len,
                              pt, pt_len,
                              buf, buf_size,
                              &out_len);

    // 5. Tách dữ liệu trả về đúng các con trỏ yêu cầu
    if (status == PSA_SUCCESS) {
        memcpy(ct, buf, pt_len);
        memcpy(tag, buf + pt_len, CRYPTO_TAG_LEN);
    }

    // 6. Dọn dẹp
    free(buf);
    psa_destroy_key(key_id);
    return (int)status;
}

static int aes_gcm_dec(const uint8_t *key, const uint8_t *nonce,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *ct, size_t ct_len,
                       const uint8_t *tag, uint8_t *pt)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key_id = 0;
    psa_status_t status;

    // 1. Cấu hình thuộc tính cho AES-128 GCM
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);

    // 2. Import Key
    status = psa_import_key(&attributes, key, 16, &key_id);
    if (status != PSA_SUCCESS) {
        return (int)status;
    }

    // 3. Cấp phát bộ đệm tạm và gộp chung ciphertext + tag vào nhau
    size_t buf_size = ct_len + CRYPTO_TAG_LEN;
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        psa_destroy_key(key_id);
        return -1;
    }

    memcpy(buf, ct, ct_len);
    memcpy(buf + ct_len, tag, CRYPTO_TAG_LEN);

    // 4. Thực thi giải mã (Xác thực Tag tự động xảy ra bên trong hàm này)
    size_t out_len = 0;
    status = psa_aead_decrypt(key_id, PSA_ALG_GCM,
                              nonce, CRYPTO_NONCE_LEN,
                              aad, aad_len,
                              buf, buf_size,
                              pt, ct_len,
                              &out_len);

    // 5. Dọn dẹp
    free(buf);
    psa_destroy_key(key_id);
    return (int)status;
}

int crypto_aead_encrypt(uint8_t algo, const uint8_t *key, const uint8_t *nonce,
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *pt, size_t pt_len,
                        uint8_t *ct, uint8_t *tag)
{
    switch (algo) {
        case CRYPTO_ALGO_AES:
            return aes_gcm_enc(key, nonce, aad, aad_len, pt, pt_len, ct, tag);
        case CRYPTO_ALGO_ASCON:
            ascon128_encrypt(ct, tag, pt, pt_len, aad, aad_len, nonce, key);
            return 0;
        case CRYPTO_ALGO_NONE:
        default:
            memcpy(ct, pt, pt_len);
            memset(tag, 0, CRYPTO_TAG_LEN);
            return 0;
    }
}

int crypto_aead_decrypt(uint8_t algo, const uint8_t *key, const uint8_t *nonce,
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *ct, size_t ct_len,
                        const uint8_t *tag, uint8_t *pt)
{
    switch (algo) {
        case CRYPTO_ALGO_AES:
            return aes_gcm_dec(key, nonce, aad, aad_len, ct, ct_len, tag, pt);
        case CRYPTO_ALGO_ASCON:
            return ascon128_decrypt(pt, ct, ct_len, aad, aad_len, tag, nonce, key);
        case CRYPTO_ALGO_NONE:
        default:
            memcpy(pt, ct, ct_len);
            return 0;
    }
}