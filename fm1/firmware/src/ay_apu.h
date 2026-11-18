/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le PSG AY-3-8910 / YM2149 (Atari ST, Amstrad CPC, ZX Spectrum 128, MSX…), écrit pour ROMsounds en suivant le
 * fonctionnement d'emu2149 (Mitsutaka Okazaki, licence MIT), le cœur qu'utilise libvgm : mêmes compteurs (tops de
 * l'horloge / 8), même enveloppe sur 32 pas, même bruit (registre de 17 bits), mêmes tables de volume. Rendu direct à
 * 44,1 kHz, entiers seulement ; passe-haut pour retirer le continu (la puce ne sort que du positif).
 *
 *   ay_init(&a, horloge, ym)   2 000 000 Hz pour l'Atari ST ; ym : 1 = YM2149 (32 niveaux), 0 = AY-3-8910 (16)
 *   ay_write(&a, reg, val)     reg 0..15 (commande VGM 0xA0 ; fichiers .ym : un jeu de registres par trame)
 *   ay_render(&a, out, n)      ajoute n trames stéréo (int32, échelle Q15 ; la puce est mono : les deux côtés) */
#ifndef ROMSOUNDS_AY_APU_H
#define ROMSOUNDS_AY_APU_H
#include <stdint.h>
#ifndef AY_GAIN
#define AY_GAIN 64                              /* niveau face aux autres puces (calé sur libvgm) */
#endif

typedef struct {
    uint32_t step_q16, acc;                     /* tops (horloge / 8) par trame de sortie, Q16 */
    uint8_t reg[16], ym, mute;                  /* mute : bits 0-2 voies A, B, C ; bit 3 le bruit */
    uint32_t freq[3]; int32_t count[3]; uint8_t edge[3], vol[3];
    uint32_t nfreq, nseed; int32_t ncount;
    uint32_t efreq; int32_t ecount; uint8_t eptr, eface, epause, econt, ealt, ehold;
    int32_t hp_x, hp_y;
} ay_t;

static const uint8_t AY_VOL[2][32] = {
    {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x12,     /* YM2149 */
     0x16, 0x1A, 0x1F, 0x25, 0x2D, 0x35, 0x3F, 0x4C, 0x5A, 0x6A, 0x7F, 0x97, 0xB4, 0xD6, 0xFF, 0xFF},
    {0x00, 0x00, 0x03, 0x03, 0x04, 0x04, 0x06, 0x06, 0x09, 0x09, 0x0D, 0x0D, 0x12, 0x12, 0x1D, 0x1D,     /* AY-3-8910 */
     0x22, 0x22, 0x37, 0x37, 0x4D, 0x4D, 0x62, 0x62, 0x82, 0x82, 0xA6, 0xA6, 0xD0, 0xD0, 0xFF, 0xFF}};
static const uint8_t AY_MASK[16] = {0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0x3F, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF};

static void ay_init(ay_t *a, uint32_t clock, uint32_t ym)
{
    uint32_t i, t;
    for (i = 0; i < sizeof *a; i++) ((uint8_t *)a)[i] = 0;
    if (!clock) clock = 2000000u;
    t = clock / 8u;
    a->step_q16 = (t / 44100u) << 16 | (((t % 44100u) << 16) / 44100u);
    a->ym = (uint8_t)(ym ? 1u : 0u);
    for (i = 0; i < 3u; i++) a->count[i] = 1;
    a->nseed = 0xFFFFu; a->nfreq = 0; a->ncount = 1;
    a->epause = 1; a->ecount = 1;
}

static void ay_write(ay_t *a, uint32_t r, uint32_t v)
{
    if (r > 15u) return;
    v &= AY_MASK[r];
    a->reg[r] = (uint8_t)v;
    if (r < 6u) { uint32_t c = r >> 1; a->freq[c] = (uint32_t)(a->reg[2u * c + 1u] & 15u) << 8 | a->reg[2u * c]; }
    else if (r == 6u) a->nfreq = (v & 31u) << 1;
    else if (r >= 8u && r <= 10u) a->vol[r - 8u] = (uint8_t)(v << 1);
    else if (r == 11u || r == 12u) { a->efreq = (uint32_t)a->reg[12] << 8 | a->reg[11]; a->ecount = (int32_t)(a->efreq ? a->efreq : 1u); }
    else if (r == 13u) {
        a->econt = (uint8_t)(v >> 3 & 1u); a->eface = (uint8_t)(v >> 2 & 1u); a->ealt = (uint8_t)(v >> 1 & 1u); a->ehold = (uint8_t)(v & 1u);
        a->epause = 0; a->ecount = (int32_t)(a->efreq ? a->efreq : 1u);
        a->eptr = a->eface ? 0u : 0x1Fu;
    }
}

static void ay_render(ay_t *a, int32_t *out, uint32_t n)
{
    const uint8_t *vt = AY_VOL[a->ym ? 0 : 1];
    uint32_t i, c, mix = a->reg[7];
    for (i = 0; i < n; i++) {
        int32_t ticks, x = 0, y;
        uint32_t noise;
        a->acc += a->step_q16;
        ticks = (int32_t)(a->acc >> 16);
        a->acc &= 0xFFFFu;
        a->ecount -= ticks;                     /* enveloppe : un pas tous les efreq tops */
        while (a->ecount <= 0) {
            if (!a->epause) a->eptr = (uint8_t)((a->eptr + (a->eface ? 1u : 0x3Fu)) & 0x3Fu);
            if (a->eptr & 0x20u) {              /* débordement : continuer, alterner, tenir */
                if (a->econt) {
                    if (a->ealt ^ a->ehold) a->eface ^= 1u;
                    if (a->ehold) a->epause = 1;
                    a->eptr = a->eface ? 0u : 0x1Fu;
                } else { a->epause = 1; a->eptr = 0; }
            }
            a->ecount += (int32_t)(a->efreq ? a->efreq : 1u);
        }
        a->ncount -= ticks;                     /* bruit : registre de 17 bits */
        while (a->ncount <= 0) {
            if (a->nseed & 1u) a->nseed ^= 0x24000u;
            a->nseed >>= 1;
            a->ncount += (int32_t)(a->nfreq ? a->nfreq : 2u);
        }
        noise = a->nseed & 1u;
        if (a->mute & 8u) noise = 1u;           /* (bruit coupé : comme s'il était éteint) */
        for (c = 0; c < 3u; c++) {
            a->count[c] -= ticks;
            while (a->count[c] <= 0) {
                if (a->freq[c] > 1u) { a->edge[c] ^= 1u; a->count[c] += (int32_t)a->freq[c]; }
                else { a->edge[c] = 1; a->count[c] += 1; }
            }
            if (a->mute >> c & 1u) continue;
            if (((mix >> c & 1u) || a->edge[c]) && ((mix >> (c + 3u) & 1u) || noise))
                x += (int32_t)vt[a->vol[c] & 32u ? a->eptr & 31u : a->vol[c] & 31u] << 5;
        }
        y = x - a->hp_x + a->hp_y - (a->hp_y >> 9);   /* passe-haut ~14 Hz : retirer le continu */
        a->hp_x = x; a->hp_y = y;
        y = y * AY_GAIN >> 6;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
    }
}
#endif
