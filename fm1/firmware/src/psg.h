/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le PSG SN76489 de la Mega Drive et de la Master System (version Sega), écrit pour ROMsounds.
 * 3 voies carrées + 1 bruit, atténuation par pas de 2 dB. Rendu directement à 44,1 kHz, nombres entiers seulement.
 *
 *   psg_init(&p, horloge)        horloge du PSG (3 579 545 Hz en NTSC)
 *   psg_write(&p, octet)         une écriture sur le port de la puce (commande VGM 0x50)
 *   psg_render(&p, out, n)       ajoute n trames stéréo (int32 L R L R ..., échelle Q15) à out
 *
 * Bruit Sega : registre à décalage de 16 bits, rebouclage sur les bits 0 et 3, remis à 0x8000 à chaque écriture du
 * registre de bruit. Une période nulle compte comme 1 (comportement de la puce Sega). */
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
    uint16_t lfsr;
} psg_t;

/* 2 dB par pas ; voie au maximum : 4032, le niveau du PSG face au YM2612 dans libvgm (vgm2wav), mesuré sur des pistes
 * Mega Drive (17/11/2026) */
static const uint16_t PSG_VOL[16] = {4032, 3203, 2544, 2021, 1605, 1275, 1013, 805, 639, 508, 403, 320, 254, 202, 160, 0};

static void psg_init(psg_t *p, uint32_t clock)
{
    uint32_t t = clock / 16u, i;
    for (i = 0; i < sizeof *p; i++) ((uint8_t *)p)[i] = 0;
    p->step_q16 = (t / 44100u) << 16 | (((t % 44100u) << 16) / 44100u);   /* sans division 64 bits */
    for (i = 0; i < 4u; i++) { p->vol[i] = 15; p->period[i] = i < 3u ? 1u : 0u; p->count[i] = 1; }
    p->lfsr = 0x8000u;
}

static void psg_write(psg_t *p, uint32_t d)
{
    uint32_t ch;
    if (d & 0x80u) {                           /* octet de commande : registre + 4 bits bas */
        p->latch = (uint8_t)((d >> 4) & 7u);
        ch = p->latch >> 1;
        if (p->latch & 1u) p->vol[ch] = (uint8_t)(d & 15u);
        else if (ch < 3u) p->period[ch] = (uint16_t)((p->period[ch] & 0x3F0u) | (d & 15u));
        else { p->period[3] = (uint16_t)(d & 7u); p->lfsr = 0x8000u; }
        return;
    }
    ch = p->latch >> 1;                         /* octet de données : 6 bits hauts de la période (ou le volume) */
    if (p->latch & 1u) p->vol[ch] = (uint8_t)(d & 15u);
    else if (ch < 3u) p->period[ch] = (uint16_t)((p->period[ch] & 15u) | (d & 0x3Fu) << 4);
    else { p->period[3] = (uint16_t)(d & 7u); p->lfsr = 0x8000u; }
}

static void psg_render(psg_t *p, int32_t *out, uint32_t n)
{
    uint32_t i, c;
    for (i = 0; i < n; i++) {
        int32_t ticks, s = 0;
        p->acc += p->step_q16;
        ticks = (int32_t)(p->acc >> 16);
        p->acc &= 0xFFFFu;
        for (c = 0; c < 3u; c++) {              /* voies carrées : la sortie s'inverse à chaque fin de période */
            int32_t per = p->period[c] ? p->period[c] : 1;
            p->count[c] -= ticks;
            while (p->count[c] <= 0) { p->count[c] += per; p->out[c] ^= 1u; }
            if (p->period[c] > 1u)
                s += p->out[c] ? PSG_VOL[p->vol[c]] : -PSG_VOL[p->vol[c]];
            else                                /* période 0 ou 1 : sortie haute continue (sert aux samples par volume) */
                s += PSG_VOL[p->vol[c]];
        }
        {                                       /* bruit : 0x10, 0x20, 0x40 tops, ou la période de la voie 3 */
            uint32_t r = p->period[3] & 3u;
            int32_t per = r == 3u ? (p->period[2] ? p->period[2] : 1) : 0x10 << r;
            p->count[3] -= ticks;
            while (p->count[3] <= 0) {
                p->count[3] += per;
                p->out[3] ^= 1u;
                if (p->out[3]) {                /* le registre avance à chaque front montant */
                    uint32_t fb = p->period[3] & 4u ? ((p->lfsr ^ (p->lfsr >> 3)) & 1u) : (p->lfsr & 1u);
                    p->lfsr = (uint16_t)((p->lfsr >> 1) | fb << 15);
                }
            }
            s += p->lfsr & 1u ? PSG_VOL[p->vol[3]] : -PSG_VOL[p->vol[3]];
        }
        out[2u * i] += s;
        out[2u * i + 1u] += s;
    }
}
#endif
