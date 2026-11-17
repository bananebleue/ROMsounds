/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le PSG SN76489 de la Mega Drive, de la Master System et de la Game Gear (et des autres SN76489 :
 * rebouclage et largeur du bruit réglables, comme l'en-tête VGM), écrit pour ROMsounds.
 * 3 voies carrées + 1 bruit, atténuation par pas de 2 dB. Rendu directement à 44,1 kHz, nombres entiers seulement.
 *
 *   psg_init(&p, horloge, fb, w) horloge du PSG (3 579 545 Hz en NTSC) ; bruit : masque de rebouclage et largeur du
 *                                registre (Sega : 0x0009 et 16 ; TI : 0x0003 et 15 ; 0 : Sega)
 *   psg_write(&p, octet)         une écriture sur le port de la puce (commande VGM 0x50)
 *   psg_stereo(&p, octet)        Game Gear : quelles voies à gauche (bits 4-7) et à droite (bits 0-3) (commande 0x4F)
 *   psg_render(&p, out, n)       ajoute n trames stéréo (int32 L R L R ..., échelle Q15) à out
 *
 * Bruit : le registre est remis à son bit de tête à chaque écriture du registre de bruit. Une période nulle compte comme
 * 1 (comportement de la puce Sega). */
#ifndef ROMSOUNDS_PSG_H
#define ROMSOUNDS_PSG_H
#include <stdint.h>

typedef struct {
    uint32_t step_q16;          /* tops d'horloge (horloge / 16) par trame de sortie, Q16 */
    uint32_t acc;               /* fraction en cours, Q16 */
    uint16_t period[4];         /* 3 voies (10 bits) ; [3] : le registre de bruit (3 bits) */
    int32_t count[4];           /* compteurs, en tops */
    uint8_t vol[4];             /* atténuation 0 (fort) .. 15 (muet) */
    uint8_t out[4];             /* état de la sortie de chaque voie */
    uint8_t latch;              /* registre visé par le dernier octet de commande : voie * 2 + (1 = volume) */
    uint8_t width;              /* largeur du registre de bruit */
    uint8_t stereo;             /* Game Gear : bits 4-7 gauche, 0-3 droite (0xFF : tout partout) */
    uint16_t lfsr, feedback;
} psg_t;

/* 2 dB par pas ; voie au maximum : 4032, le niveau du PSG face au YM2612 dans libvgm (vgm2wav), mesuré sur des pistes
 * Mega Drive (17/11/2026) */
static const uint16_t PSG_VOL[16] = {4032, 3203, 2544, 2021, 1605, 1275, 1013, 805, 639, 508, 403, 320, 254, 202, 160, 0};

static void psg_init(psg_t *p, uint32_t clock, uint32_t feedback, uint32_t width)
{
    uint32_t t = clock / 16u, i;
    for (i = 0; i < sizeof *p; i++) ((uint8_t *)p)[i] = 0;
    p->step_q16 = (t / 44100u) << 16 | (((t % 44100u) << 16) / 44100u);   /* sans division 64 bits */
    for (i = 0; i < 4u; i++) { p->vol[i] = 15; p->period[i] = i < 3u ? 1u : 0u; p->count[i] = 1; }
    p->feedback = (uint16_t)(feedback ? feedback : 0x0009u);
    p->width = (uint8_t)(width >= 2u && width <= 16u ? width : 16u);
    p->lfsr = (uint16_t)(1u << (p->width - 1u));
    p->stereo = 0xFFu;
}

static void psg_stereo(psg_t *p, uint32_t d) { p->stereo = (uint8_t)d; }

static uint32_t psg_parity(uint32_t v) { v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return v & 1u; }

static void psg_write(psg_t *p, uint32_t d)
{
    uint32_t ch;
    if (d & 0x80u) {                           /* octet de commande : registre + 4 bits bas */
        p->latch = (uint8_t)((d >> 4) & 7u);
        ch = p->latch >> 1;
        if (p->latch & 1u) p->vol[ch] = (uint8_t)(d & 15u);
        else if (ch < 3u) p->period[ch] = (uint16_t)((p->period[ch] & 0x3F0u) | (d & 15u));
        else { p->period[3] = (uint16_t)(d & 7u); p->lfsr = (uint16_t)(1u << (p->width - 1u)); }
        return;
    }
    ch = p->latch >> 1;                         /* octet de données : 6 bits hauts de la période (ou le volume) */
    if (p->latch & 1u) p->vol[ch] = (uint8_t)(d & 15u);
    else if (ch < 3u) p->period[ch] = (uint16_t)((p->period[ch] & 15u) | (d & 0x3Fu) << 4);
    else { p->period[3] = (uint16_t)(d & 7u); p->lfsr = (uint16_t)(1u << (p->width - 1u)); }
}

static void psg_render(psg_t *p, int32_t *out, uint32_t n)
{
    uint32_t i, c, st = p->stereo;
    for (i = 0; i < n; i++) {
        int32_t ticks, l = 0, r = 0, v;
        p->acc += p->step_q16;
        ticks = (int32_t)(p->acc >> 16);
        p->acc &= 0xFFFFu;
        for (c = 0; c < 3u; c++) {              /* voies carrées : la sortie s'inverse à chaque fin de période */
            int32_t per = p->period[c] ? p->period[c] : 1;
            p->count[c] -= ticks;
            while (p->count[c] <= 0) { p->count[c] += per; p->out[c] ^= 1u; }
            if (p->period[c] > 1u)
                v = p->out[c] ? PSG_VOL[p->vol[c]] : -PSG_VOL[p->vol[c]];
            else                                /* période 0 ou 1 : sortie haute continue (sert aux samples par volume) */
                v = PSG_VOL[p->vol[c]];
            if (st >> (4u + c) & 1u) l += v;
            if (st >> c & 1u) r += v;
        }
        {                                       /* bruit : 0x10, 0x20, 0x40 tops, ou la période de la voie 3 */
            uint32_t rr = p->period[3] & 3u;
            int32_t per = rr == 3u ? (p->period[2] ? p->period[2] : 1) : 0x10 << rr;
            p->count[3] -= ticks;
            while (p->count[3] <= 0) {
                p->count[3] += per;
                p->out[3] ^= 1u;
                if (p->out[3]) {                /* le registre avance à chaque front montant */
                    uint32_t fb = p->period[3] & 4u ? psg_parity(p->lfsr & p->feedback) : (p->lfsr & 1u);
                    p->lfsr = (uint16_t)((p->lfsr >> 1) | fb << (p->width - 1u));
                }
            }
            v = p->lfsr & 1u ? PSG_VOL[p->vol[3]] : -PSG_VOL[p->vol[3]];
            if (st >> 7 & 1u) l += v;
            if (st >> 3 & 1u) r += v;
        }
        out[2u * i] += l;
        out[2u * i + 1u] += r;
    }
}
#endif
