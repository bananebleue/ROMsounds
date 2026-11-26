/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le Konami 053260 « KDSC » (échantillons PCM 8 bits et KADPCM 4 bits des bornes Konami : TMNT 2 Turtles in
 * Time, Sunset Riders, The Simpsons, Vendetta…), écrit pour ROMsounds en suivant k053260.c de MAME (via libvgm, BSD-3) :
 * 4 voix, mêmes registres (hauteur 12 bits, longueur, début sur 21 bits, volume, panoramique sur 8 positions, boucle,
 * KADPCM, lecture à l'envers), même lecture qui commence 1 octet après le début, même table KADPCM. La page a déjà gardé
 * dans la ROM seulement les échantillons joués et réécrit leurs adresses. Calcul à la fréquence de la puce (horloge / 64),
 * ramené à 44,1 kHz par interpolation ; entiers seulement.
 *
 *   kdsc_init(&k, horloge, rom, taille)   (horloge : 3 579 545 Hz)
 *   kdsc_write(&k, reg, val)               commande VGM 0xBA
 *   kdsc_render(&k, out, n)                ajoute n trames stéréo (int32) */
#ifndef ROMSOUNDS_K053260_H
#define ROMSOUNDS_K053260_H
#include <stdint.h>
#ifndef KDSC_GAIN
#define KDSC_GAIN 2                             /* niveau face aux autres puces (calé sur libvgm) */
#endif

typedef struct {
    uint32_t start, counter, position;
    uint16_t length, pitch;
    int8_t output;
    uint8_t playing, volume, pan, loop, kadpcm, reverse, force;
    int32_t pv[2];                              /* volume x panoramique (Q15, comme pan_volume de MAME) */
} kdsc_voice_t;
typedef struct {
    const uint8_t *rom; uint32_t rom_len;
    uint32_t step_q16, acc;                     /* échantillons de la puce par trame de sortie, Q16 */
    int32_t prev[2], cur[2];
    uint8_t keyon, mode, mute;                  /* mute : bits 0-3 = voix 1-4 */
    kdsc_voice_t v[4];
} kdsc_t;

static const uint16_t KDSC_PAN[8][2] = {{0, 0}, {65535, 0}, {59870, 26656}, {53684, 37950}, {46341, 46341}, {37950, 53684},
    {26656, 59870}, {0, 65535}};                /* (65536 dans MAME : 65535 ici, pour tenir sur 16 bits) */
static const int8_t KDSC_ADPCM[16] = {0, 1, 2, 4, 8, 16, 32, 64, -128, -64, -32, -16, -8, -4, -2, -1};

static void kdsc_init(kdsc_t *k, uint32_t clock, const uint8_t *rom, uint32_t len)
{
    uint32_t i, r;
    for (i = 0; i < sizeof *k; i++) ((uint8_t *)k)[i] = 0;
    if (!clock) clock = 3579545u;
    r = clock / 64u;
    k->step_q16 = (r / 44100u) << 16 | (((r % 44100u) << 16) / 44100u);
    k->rom = rom; k->rom_len = rom ? len : 0u;
}

static void kdsc_pan(kdsc_voice_t *v)
{
    v->pv[0] = (int32_t)v->volume * KDSC_PAN[v->pan][0] >> 1;   /* volume x pan_mul >> 1 : Q15 */
    v->pv[1] = (int32_t)v->volume * KDSC_PAN[v->pan][1] >> 1;
}

static void kdsc_write(kdsc_t *k, uint32_t reg, uint32_t d)
{
    uint32_t i;
    reg &= 0x3Fu;
    if (reg >= 0x08u && reg <= 0x27u) {
        kdsc_voice_t *v = &k->v[(reg - 8u) >> 3];
        switch (reg & 7u) {
        case 0: v->pitch = (uint16_t)((v->pitch & 0x0F00u) | d); break;
        case 1: v->pitch = (uint16_t)((v->pitch & 0x00FFu) | (d << 8 & 0x0F00u)); break;
        case 2: v->length = (uint16_t)((v->length & 0xFF00u) | d); break;
        case 3: v->length = (uint16_t)((v->length & 0x00FFu) | d << 8); break;
        case 4: v->start = (v->start & 0x1FFF00u) | d; break;
        case 5: v->start = (v->start & 0x1F00FFu) | d << 8; break;
        case 6: v->start = (v->start & 0x00FFFFu) | (d << 16 & 0x1F0000u); break;
        default: v->volume = (uint8_t)(d & 0x7Fu); kdsc_pan(v); break;
        }
        return;
    }
    switch (reg) {
    case 0x28:                                  /* départs (front montant), arrêts, lecture à l'envers */
        for (i = 0; i < 4u; i++) {
            kdsc_voice_t *v = &k->v[i];
            v->reverse = (uint8_t)(d >> (i | 4u) & 1u);
            if ((d & ~(uint32_t)k->keyon) >> i & 1u) {
                v->position = v->kadpcm ? 1u : 0u; v->force = 1; v->output = 0; v->playing = 1;
            } else if (!(d >> i & 1u)) { v->position = 0; v->output = 0; v->playing = 0; }
        }
        k->keyon = (uint8_t)d;
        break;
    case 0x2A: for (i = 0; i < 4u; i++) { k->v[i].loop = (uint8_t)(d >> i & 1u); k->v[i].kadpcm = (uint8_t)(d >> (i | 4u) & 1u); } break;
    case 0x2C: k->v[0].pan = (uint8_t)(d & 7u); k->v[1].pan = (uint8_t)(d >> 3 & 7u); kdsc_pan(&k->v[0]); kdsc_pan(&k->v[1]); break;
    case 0x2D: k->v[2].pan = (uint8_t)(d & 7u); k->v[3].pan = (uint8_t)(d >> 3 & 7u); kdsc_pan(&k->v[2]); kdsc_pan(&k->v[3]); break;
    case 0x2F: k->mode = (uint8_t)d; break;
    default: break;                             /* (ports de communication entre processeurs : sans effet sur le son) */
    }
}

static uint32_t kdsc_rd(const kdsc_t *k, uint32_t a) { return a < k->rom_len ? k->rom[a] : 0u; }

static void kdsc_clock(kdsc_t *k, int32_t *o)   /* un échantillon de la puce (64 cycles) : o[0], o[1] */
{
    uint32_t i;
    o[0] = o[1] = 0;
    if (!(k->mode & 2u)) return;                /* (bit 1 : sortie du son) */
    for (i = 0; i < 4u; i++) {
        kdsc_voice_t *v = &k->v[i];
        if (!v->playing) continue;
        if (v->force) { v->counter = 0x1000u; v->force = 0; } else v->counter += 64u;
        while (v->counter >= 0x1000u) {
            uint32_t bp, rd;
            v->counter = v->counter - 0x1000u + v->pitch;
            bp = ++v->position >> (v->kadpcm ? 1 : 0);
            if (bp > v->length) {
                if (v->loop) { v->position = 0; v->output = 0; bp = 0; }
                else { v->playing = 0; break; }
            }
            rd = kdsc_rd(k, (v->reverse ? v->start - bp : v->start + bp) & 0x1FFFFFu);
            if (v->kadpcm) { if (v->position & 1u) rd >>= 4; v->output = (int8_t)(v->output + KDSC_ADPCM[rd & 15u]); }
            else v->output = (int8_t)rd;
        }
        if (!v->playing || (k->mute >> i & 1u)) continue;
        o[0] += v->output * v->pv[0] >> 15;
        o[1] += v->output * v->pv[1] >> 15;
    }
}

static void kdsc_render(kdsc_t *k, int32_t *out, uint32_t n)
{
    uint32_t i, c;
    for (i = 0; i < n; i++) {
        k->acc += k->step_q16;
        while (k->acc >= 65536u) { k->acc -= 65536u; k->prev[0] = k->cur[0]; k->prev[1] = k->cur[1]; kdsc_clock(k, k->cur); }
        for (c = 0; c < 2u; c++) {
            int32_t x = k->prev[c] + ((k->cur[c] - k->prev[c]) >> 4) * (int32_t)(k->acc >> 4) / 256;   /* (sans 64 bits) */
            out[2u * i + c] += x * KDSC_GAIN;
        }
    }
}
#endif
