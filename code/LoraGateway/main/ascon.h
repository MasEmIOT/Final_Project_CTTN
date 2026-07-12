/*
 * ascon.h - ASCON-128 AEAD (NIST Lightweight Cryptography winner).
 * key = 16 byte, nonce = 16 byte, tag = 16 byte, rate = 8 byte, pa=12, pb=6.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* Ma hoa: ghi ciphertext (mlen byte) vao c, tag 16 byte vao tag. */
void ascon128_encrypt(uint8_t *c, uint8_t *tag,
                      const uint8_t *m, size_t mlen,
                      const uint8_t *ad, size_t adlen,
                      const uint8_t *npub, const uint8_t *k);

/* Giai ma + xac thuc. Tra ve 0 neu tag dung, -1 neu sai. */
int ascon128_decrypt(uint8_t *m,
                     const uint8_t *c, size_t clen,
                     const uint8_t *ad, size_t adlen,
                     const uint8_t *tag, const uint8_t *npub, const uint8_t *k);
