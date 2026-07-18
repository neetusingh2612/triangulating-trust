/* ==========================================================================
 * hmac_sha1.c -- HMAC-SHA1 over a CAN frame, Cortex-M reference, for a
 * like-for-like cycle comparison with the Triangulating Trust tag.
 *
 * Message is (ID || D || nonce) = 2 + 8 + 4 = 14 bytes, so a single SHA-1
 * block per hash and two hashes per HMAC (inner + outer). The tag is the low
 * 16 bits of the outer digest, matching the truncation used in evaluation.
 *
 * This is deliberately a straightforward, correct SHA-1; it is NOT hand-
 * optimised, because the point is a fair comparison of the standard primitive
 * a deployer would actually use (an OpenSSL/mbedTLS-derived port) against the
 * TT tag, not a contest of assembly tricks.
 *
 * Build: arm-none-eabi-gcc -O2 -mcpu=cortex-mN -mthumb -c hmac_sha1.c
 * ========================================================================== */

#include <stdint.h>
#include <string.h>

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* --- SHA-1 compression of one 64-byte block --------------------------------*/
static void sha1_block(uint32_t state[5], const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
        uint32_t t = ROL(a,5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b,30); b = a; a = t;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

static void sha1(const uint8_t *msg, uint32_t len, uint8_t out[20])
{
    uint32_t st[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    uint8_t block[64];
    /* single-block fast path: len <= 55 so message + padding fit in 64 bytes */
    memset(block, 0, 64);
    memcpy(block, msg, len);
    block[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        block[63-i] = (uint8_t)(bits >> (8*i));
    sha1_block(st, block);
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(st[i] >> 24);
        out[i*4+1] = (uint8_t)(st[i] >> 16);
        out[i*4+2] = (uint8_t)(st[i] >> 8);
        out[i*4+3] = (uint8_t)(st[i]);
    }
}

/* --- HMAC-SHA1, key <= 64 bytes, single-block message ----------------------*/
uint16_t hmac_sha1_tag(const uint8_t *key, uint32_t keylen,
                       const uint8_t *msg, uint32_t msglen)
{
    uint8_t k_ipad[64], k_opad[64], ihash[20], tmp[64+20];
    memset(k_ipad, 0, 64); memset(k_opad, 0, 64);
    memcpy(k_ipad, key, keylen); memcpy(k_opad, key, keylen);
    for (int i = 0; i < 64; i++) { k_ipad[i] ^= 0x36; k_opad[i] ^= 0x5c; }

    /* inner = SHA1(k_ipad || msg) : two blocks (64 + msglen) */
    uint32_t st[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    sha1_block(st, k_ipad);                 /* first block = k_ipad */
    uint8_t block[64]; memset(block, 0, 64);
    memcpy(block, msg, msglen);
    block[msglen] = 0x80;
    uint64_t bits = (uint64_t)(64 + msglen) * 8;
    for (int i = 0; i < 8; i++) block[63-i] = (uint8_t)(bits >> (8*i));
    sha1_block(st, block);
    for (int i = 0; i < 5; i++) {
        ihash[i*4]=(uint8_t)(st[i]>>24); ihash[i*4+1]=(uint8_t)(st[i]>>16);
        ihash[i*4+2]=(uint8_t)(st[i]>>8); ihash[i*4+3]=(uint8_t)(st[i]);
    }

    /* outer = SHA1(k_opad || ihash) : two blocks */
    uint32_t st2[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    sha1_block(st2, k_opad);
    memset(block, 0, 64);
    memcpy(block, ihash, 20);
    block[20] = 0x80;
    bits = (uint64_t)(64 + 20) * 8;
    for (int i = 0; i < 8; i++) block[63-i] = (uint8_t)(bits >> (8*i));
    sha1_block(st2, block);

    /* low 16 bits of the outer digest */
    return (uint16_t)(((st2[4] & 0xFF) << 8) | ((st2[4] >> 8) & 0xFF));
}

/* Entry point sized to a CAN frame: msg = ID(2) || D(8) || nonce(4) = 14 B */
uint16_t hmac_can_tag(const uint8_t key[20], const uint8_t msg14[14])
{
    return hmac_sha1_tag(key, 20, msg14, 14);
}
