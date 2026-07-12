/*
 * crypto.h - Lop AEAD thong nhat: NONE / AES-128-GCM / ASCON-128.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define CRYPTO_KEY_LEN    16
#define CRYPTO_NONCE_LEN  16
#define CRYPTO_TAG_LEN    16

/* Ma hoa. Tra ve 0 neu OK. ct phai du pt_len byte, tag du 16 byte. */
int crypto_aead_encrypt(uint8_t algo, const uint8_t *key, const uint8_t *nonce,
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *pt, size_t pt_len,
                        uint8_t *ct, uint8_t *tag);

/* Giai ma + xac thuc. Tra ve 0 neu tag dung, khac 0 neu loi/gia mao. */
int crypto_aead_decrypt(uint8_t algo, const uint8_t *key, const uint8_t *nonce,
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *ct, size_t ct_len,
                        const uint8_t *tag, uint8_t *pt);

const char *crypto_algo_name(uint8_t algo);

/* ===== Dinh danh phan cung theo MAC (moi node co MAC efuse rieng) ===== */

/* Doc MAC nha may (6 byte) cua chip nay. */
void crypto_get_mac(uint8_t mac[6]);

/* Dan xuat Node ID on dinh (1..254) tu MAC -> khong can dat tay qua menuconfig.
 * 0 va 255 duoc tranh (255 = broadcast). */
uint8_t crypto_node_id_from_mac(void);

/* (Tuy chon) Dan xuat khoa rieng cho tung node = HMAC-SHA256(master, mac)[0:16].
 * Mac dinh HE THONG DUNG KHOA CHIA SE (de gateway giai ma moi node); ham nay
 * de danh cho nang cap bao mat theo tung node khi da co bang provisioning. */
void crypto_derive_key(const uint8_t master[16], const uint8_t mac[6], uint8_t out_key[16]);
