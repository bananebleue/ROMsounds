/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : outils de la réception d'un morceau depuis la page (firmware/src/romsounds.c, web/index.html) :
 * CRC-32 (IEEE, comme la page), nombres sur 5 x 7 bits, dépaquetage 7 -> 8 bits (comme fm1ota.js : pack7).
 * Testé avec la page : tests/rx_test.c. */
#ifndef ROMSOUNDS_RX_H
#define ROMSOUNDS_RX_H
#include <stdint.h>

static uint32_t rx_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static uint32_t rd35(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}

static uint32_t unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)   /* 7 bits -> 8 (comme fm1ota.js) */
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7u;
        if (nb >= 8u) { if (o < max) out[o] = (uint8_t)acc; o++; acc >>= 8; nb -= 8u; }
    }
    return o;
}
#endif
