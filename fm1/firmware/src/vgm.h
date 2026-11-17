/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : lecture d'un fichier VGM (format libre de relevé des puces son, https://vgmrips.net/wiki/VGM_Specification)
 * pour la Mega Drive : YM2612 (FM + DAC) et SN76489 (PSG). Les autres puces sont ignorées (et signalées).
 *
 *   vgm_open(&v, données, taille)  lit l'en-tête : 0 ou une erreur VGM_E_*
 *   vgm_next(&v, ym, psg)          exécute les commandes jusqu'à la prochaine attente ; rend son nombre de trames
 *                                  à 44,1 kHz (0 : fin, sans boucle)
 * Les écritures passent par deux fonctions de l'app : vgm_ym(port, reg, val) et vgm_psg(val). */
#ifndef ROMSOUNDS_VGM_H
#define ROMSOUNDS_VGM_H
#include <stdint.h>

enum { VGM_OK = 0, VGM_E_MAGIC = 1, VGM_E_SHORT = 2, VGM_E_NOCHIP = 3 };
enum { VGM_W_OTHERCHIPS = 1, VGM_W_DACSTREAM = 2 };   /* avertissements (bits de v.warn) */

typedef struct {
    const uint8_t *d;
    uint32_t len, pos, start, loop;     /* loop : position de boucle (0 : pas de boucle) */
    uint32_t ym_clock, psg_clock, version;
    uint32_t total, loop_samples;       /* en trames à 44,1 kHz */
    const uint8_t *pcm;                 /* banque PCM du YM2612 (bloc de données 0x67 type 0) */
    uint32_t pcm_len, pcm_pos;
    uint32_t loops;                     /* boucles déjà faites */
    uint8_t warn;
} vgm_t;

static void vgm_ym(uint32_t port, uint32_t reg, uint32_t val);
static void vgm_psg(uint32_t val);

static uint32_t vgm_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int vgm_open(vgm_t *v, const uint8_t *d, uint32_t len)
{
    uint32_t i, hdr;
    for (i = 0; i < sizeof *v; i++) ((uint8_t *)v)[i] = 0;
    if (len < 0x40u) return VGM_E_SHORT;
    if (d[0] != 'V' || d[1] != 'g' || d[2] != 'm' || d[3] != ' ') return VGM_E_MAGIC;
    v->d = d;
    v->len = len;
    v->version = vgm_rd32(d + 8);
    v->psg_clock = vgm_rd32(d + 0x0C) & 0x3FFFFFFFu;
    v->ym_clock = v->version >= 0x110u ? vgm_rd32(d + 0x2C) & 0x3FFFFFFFu : vgm_rd32(d + 0x10) & 0x3FFFFFFFu;
    v->total = vgm_rd32(d + 0x18);
    v->loop_samples = vgm_rd32(d + 0x20);
    v->start = v->version >= 0x150u && vgm_rd32(d + 0x34) ? 0x34u + vgm_rd32(d + 0x34) : 0x40u;
    if (v->start >= len) return VGM_E_SHORT;
    if (vgm_rd32(d + 0x1C)) v->loop = 0x1Cu + vgm_rd32(d + 0x1C);
    if (v->loop >= len) v->loop = 0;
    if (!v->ym_clock && !v->psg_clock) return VGM_E_NOCHIP;
    {   /* les horloges des autres puces (spécification VGM 1.71) : présentes = ignorées, on le signale */
        static const uint8_t OTHER[] = {0x10, 0x30, 0x38, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68,
                                        0x6C, 0x70, 0x74, 0x80, 0x84, 0x88, 0x8C, 0x90, 0x98, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC,
                                        0xB0, 0xB4, 0xB8, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8, 0xDC, 0xE0};
        hdr = v->start < 0x100u ? v->start : 0x100u;
        for (i = 0; i < sizeof OTHER; i++)
            if (OTHER[i] + 4u <= hdr && (OTHER[i] != 0x10u || v->version >= 0x110u) && (vgm_rd32(d + OTHER[i]) & 0x3FFFFFFFu))
                v->warn |= VGM_W_OTHERCHIPS;
    }
    v->pos = v->start;
    return VGM_OK;
}

static uint32_t vgm_next(vgm_t *v)
{
    const uint8_t *d = v->d;
    for (;;) {
        uint32_t c, p = v->pos;
        if (p >= v->len) goto end;
        c = d[p];
        switch (c) {
        case 0x50: if (p + 2u > v->len) goto end; vgm_psg(d[p + 1]); v->pos += 2; continue;
        case 0x52: case 0x53:
            if (p + 3u > v->len) goto end;
            vgm_ym(c - 0x52u, d[p + 1], d[p + 2]);
            v->pos += 3;
            continue;
        case 0x61: if (p + 3u > v->len) goto end; v->pos += 3; if (d[p + 1] | d[p + 2]) return (uint32_t)d[p + 1] | (uint32_t)d[p + 2] << 8; continue;
        case 0x62: v->pos += 1; return 735;
        case 0x63: v->pos += 1; return 882;
        case 0x66: goto end;
        case 0x67: {                                       /* bloc de données : 67 66 tt ssssssss */
            uint32_t n;
            if (p + 7u > v->len) goto end;
            n = vgm_rd32(d + p + 3) & 0x7FFFFFFFu;
            if (p + 7u + n > v->len) goto end;
            if (d[p + 2] == 0x00u && !v->pcm) { v->pcm = d + p + 7; v->pcm_len = n; }
            v->pos += 7u + n;
            continue;
        }
        case 0xE0: if (p + 5u > v->len) goto end; v->pcm_pos = vgm_rd32(d + p + 1); v->pos += 5; continue;
        default: break;
        }
        if (c >= 0x70u && c <= 0x7Fu) { v->pos += 1; return (c & 15u) + 1u; }
        if (c >= 0x80u && c <= 0x8Fu) {                    /* DAC : un octet de la banque PCM, puis attendre n */
            vgm_ym(0, 0x2A, v->pcm && v->pcm_pos < v->pcm_len ? v->pcm[v->pcm_pos] : 0x80u);
            v->pcm_pos++;
            v->pos += 1;
            if (c & 15u) return c & 15u;
            continue;
        }
        if (c >= 0x90u && c <= 0x95u) {                    /* flux DAC (VGM 1.60) : pas encore pris en charge */
            static const uint8_t L[6] = {5, 5, 6, 11, 2, 5};
            v->warn |= VGM_W_DACSTREAM;
            v->pos += L[c - 0x90u];
            continue;
        }
        /* les autres puces : sauter la commande selon sa longueur (spécification VGM) */
        if (c == 0x4Fu || (c >= 0x30u && c <= 0x3Fu)) { v->pos += 2; continue; }
        if ((c >= 0x40u && c <= 0x4Eu) || (c >= 0x51u && c <= 0x5Fu) || (c >= 0xA0u && c <= 0xBFu)) { v->pos += 3; continue; }
        if (c >= 0xC0u && c <= 0xDFu) { v->pos += 4; continue; }
        if (c >= 0xE1u) { v->pos += 5; continue; }
        v->pos += 1;                                       /* octet inconnu : on avance */
        continue;
    end:
        if (v->loop && v->loop < v->len && v->pos != v->loop) {   /* boucle */
            v->pos = v->loop;
            v->loops++;
            continue;
        }
        return 0;
    }
}
#endif
