/*
 * crypto_cfg.h - Cau hinh ma hoa duong truyen LoRa (Node & Gateway PHAI GIONG NHAU).
 *
 * Ho tro 2 thuat toan AEAD (vua ma hoa vua xac thuc):
 *   - AES-128-GCM  (dung mbedTLS, rat on dinh)
 *   - ASCON-128    (lightweight AEAD, chuan NIST cho IoT)
 * Hoac tat ma hoa (NONE) de debug.
 *
 * Doi CRYPTO_ALGO o duoi de chon. Node va Gateway phai chon GIONG nhau,
 * va dung chung CRYPTO_KEY.
 */
#pragma once

#include <stdint.h>

#define CRYPTO_ALGO_NONE    0
#define CRYPTO_ALGO_AES     1
#define CRYPTO_ALGO_ASCON   2

/* >>> CHON THUAT TOAN O DAY <<<  (mac dinh AES cho de bring-up; doi sang
 *     CRYPTO_ALGO_ASCON neu muon demo lightweight crypto). */
#ifndef CRYPTO_ALGO
#define CRYPTO_ALGO   CRYPTO_ALGO_AES
#endif

/* Khoa 128-bit chia se. DOI gia tri nay truoc khi trien khai that. */
static const uint8_t CRYPTO_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};
