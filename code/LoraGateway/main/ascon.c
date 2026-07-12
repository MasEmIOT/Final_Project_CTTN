/*
 * ascon.c - ASCON-128 AEAD reference (64-bit, big-endian state).
 * Theo dac ta ASCON v1.2.
 */
#include "ascon.h"
#include <string.h>

typedef uint64_t u64;

#define ROTR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static u64 load64(const uint8_t *b)
{
    u64 r = 0;
    for (int i = 0; i < 8; i++) r = (r << 8) | b[i];
    return r;
}

static void store64(uint8_t *b, u64 x)
{
    for (int i = 7; i >= 0; i--) { b[i] = (uint8_t)(x & 0xff); x >>= 8; }
}

/* nap n byte (0..7) vao cac byte cao cua tu 64-bit */
static u64 loadbytes(const uint8_t *b, int n)
{
    u64 r = 0;
    for (int i = 0; i < n; i++) r |= (u64)b[i] << (56 - 8 * i);
    return r;
}

static void storebytes(uint8_t *b, u64 x, int n)
{
    for (int i = 0; i < n; i++) b[i] = (uint8_t)((x >> (56 - 8 * i)) & 0xff);
}

/* xoa n byte cao cua x */
static u64 clearbytes(u64 x, int n)
{
    for (int i = 0; i < n; i++) x &= ~((u64)0xff << (56 - 8 * i));
    return x;
}

static void permutation(u64 *S, int rounds)
{
    static const u64 RC[12] = {
        0xf0ull, 0xe1ull, 0xd2ull, 0xc3ull, 0xb4ull, 0xa5ull,
        0x96ull, 0x87ull, 0x78ull, 0x69ull, 0x5aull, 0x4bull
    };
    u64 s0 = S[0], s1 = S[1], s2 = S[2], s3 = S[3], s4 = S[4];
    u64 t0, t1, t2, t3, t4;
    int start = 12 - rounds;
    for (int i = start; i < 12; i++) {
        /* hang so vong */
        s2 ^= RC[i];
        /* lop thay the (S-box bitsliced) */
        s0 ^= s4; s4 ^= s3; s2 ^= s1;
        t0 = (~s0) & s1; t1 = (~s1) & s2; t2 = (~s2) & s3;
        t3 = (~s3) & s4; t4 = (~s4) & s0;
        s0 ^= t1; s1 ^= t2; s2 ^= t3; s3 ^= t4; s4 ^= t0;
        s1 ^= s0; s0 ^= s4; s3 ^= s2; s2 = ~s2;
        /* lop khuyech tan tuyen tinh */
        s0 ^= ROTR(s0, 19) ^ ROTR(s0, 28);
        s1 ^= ROTR(s1, 61) ^ ROTR(s1, 39);
        s2 ^= ROTR(s2, 1)  ^ ROTR(s2, 6);
        s3 ^= ROTR(s3, 10) ^ ROTR(s3, 17);
        s4 ^= ROTR(s4, 7)  ^ ROTR(s4, 41);
    }
    S[0] = s0; S[1] = s1; S[2] = s2; S[3] = s3; S[4] = s4;
}

#define IV_ASCON128   0x80400c0600000000ull

static void init_state(u64 *S, const uint8_t *k, const uint8_t *npub,
                       u64 *K0, u64 *K1)
{
    *K0 = load64(k);
    *K1 = load64(k + 8);
    S[0] = IV_ASCON128;
    S[1] = *K0;
    S[2] = *K1;
    S[3] = load64(npub);
    S[4] = load64(npub + 8);
    permutation(S, 12);
    S[3] ^= *K0;
    S[4] ^= *K1;
}

static void absorb_ad(u64 *S, const uint8_t *ad, size_t adlen)
{
    if (adlen > 0) {
        while (adlen >= 8) {
            S[0] ^= load64(ad);
            permutation(S, 6);
            ad += 8; adlen -= 8;
        }
        S[0] ^= loadbytes(ad, (int)adlen);
        S[0] ^= (u64)0x80 << (56 - 8 * (int)adlen);
        permutation(S, 6);
    }
    S[4] ^= 1;   /* domain separation */
}

void ascon128_encrypt(uint8_t *c, uint8_t *tag,
                      const uint8_t *m, size_t mlen,
                      const uint8_t *ad, size_t adlen,
                      const uint8_t *npub, const uint8_t *k)
{
    u64 S[5], K0, K1;
    init_state(S, k, npub, &K0, &K1);
    absorb_ad(S, ad, adlen);

    while (mlen >= 8) {
        S[0] ^= load64(m);
        store64(c, S[0]);
        permutation(S, 6);
        m += 8; c += 8; mlen -= 8;
    }
    int n = (int)mlen;
    S[0] ^= loadbytes(m, n);
    storebytes(c, S[0], n);
    S[0] ^= (u64)0x80 << (56 - 8 * n);

    /* finalization */
    S[1] ^= K0;
    S[2] ^= K1;
    permutation(S, 12);
    S[3] ^= K0;
    S[4] ^= K1;
    store64(tag, S[3]);
    store64(tag + 8, S[4]);
}

int ascon128_decrypt(uint8_t *m,
                     const uint8_t *c, size_t clen,
                     const uint8_t *ad, size_t adlen,
                     const uint8_t *tag, const uint8_t *npub, const uint8_t *k)
{
    u64 S[5], K0, K1;
    init_state(S, k, npub, &K0, &K1);
    absorb_ad(S, ad, adlen);

    while (clen >= 8) {
        u64 cw = load64(c);
        store64(m, S[0] ^ cw);
        S[0] = cw;
        permutation(S, 6);
        c += 8; m += 8; clen -= 8;
    }
    int n = (int)clen;
    u64 cw = loadbytes(c, n);
    storebytes(m, S[0] ^ cw, n);
    S[0] = clearbytes(S[0], n) | cw;
    S[0] ^= (u64)0x80 << (56 - 8 * n);

    /* finalization */
    S[1] ^= K0;
    S[2] ^= K1;
    permutation(S, 12);
    S[3] ^= K0;
    S[4] ^= K1;

    uint8_t t[16];
    store64(t, S[3]);
    store64(t + 8, S[4]);

    /* so sanh tag (constant-time don gian) */
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= (uint8_t)(t[i] ^ tag[i]);
    return diff == 0 ? 0 : -1;
}
