/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : l'OKI MSM6295 (échantillons ADPCM des bornes d'arcade : Capcom CPS1, Toaplan, Data East, Kaneko…), écrit
 * pour ROMsounds en suivant okim6295.c / okiadpcm.c de MAME (via libvgm) : mêmes commandes (numéro de phrase, puis voix et
 * volume ; arrêt par masque), même table des phrases en tête de ROM (8 octets : début, fin sur 18 bits), même ADPCM OKI
 * (12 bits, 49 pas), mêmes volumes. La page a déjà retiré les banques : la ROM reçue fait 256 Ko au plus. Calcul à la
 * fréquence de la puce (horloge / 132, ou / 165 broche 7 basse), ramené à 44,1 kHz par interpolation ; entiers seulement.
 *
 *   oki_init(&o, horloge, pin7, rom, taille)   (horloge : 1 000 000 Hz sur le CPS1)
 *   oki_write(&o, reg, val)                    commande VGM 0xB8 (reg 0 : la commande de la puce ; le reste : ignoré)
 *   oki_render(&o, out, n)                     ajoute n trames stéréo (int32 ; la puce est mono : les deux côtés) */
#ifndef ROMSOUNDS_OKI_H
#define ROMSOUNDS_OKI_H
#include <stdint.h>
#ifndef OKI_GAIN
#define OKI_GAIN 1                              /* niveau face aux autres puces (calé sur libvgm) */
#endif

typedef struct {
    uint8_t playing; int8_t step; int16_t signal;
    uint32_t base, sample, count, vol;
} oki_voice_t;
typedef struct {
    const uint8_t *rom; uint32_t rom_len;
    uint32_t step_q16, acc;                     /* échantillons de la puce par trame de sortie, Q16 */
    int32_t cmd;                                /* phrase en attente de sa voix, -1 : aucune */
    int32_t prev, cur;                          /* les deux derniers échantillons de la puce (interpolation) */
    uint8_t mute;                               /* bits 0-3 : voix 1-4 */
    oki_voice_t v[4];
} oki_t;

static const int16_t OKI_STEP[49] = {16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552};                         /* floor(16 x 1,1^n), comme compute_tables() de MAME */
static const int8_t OKI_SHIFT[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint8_t OKI_VOL[16] = {0x20, 0x16, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03, 0x02, 0, 0, 0, 0, 0, 0, 0};

static void oki_init(oki_t *o, uint32_t clock, uint32_t pin7, const uint8_t *rom, uint32_t len)
{
    uint32_t i, r;
    for (i = 0; i < sizeof *o; i++) ((uint8_t *)o)[i] = 0;
    if (!clock) clock = 1000000u;
    r = clock / (pin7 ? 132u : 165u);
    o->step_q16 = (r / 44100u) << 16 | (((r % 44100u) << 16) / 44100u);
    o->rom = rom; o->rom_len = rom ? len : 0u;
    o->cmd = -1;
}

static uint32_t oki_rd(const oki_t *o, uint32_t a) { return a < o->rom_len ? o->rom[a] : 0u; }

static void oki_write(oki_t *o, uint32_t reg, uint32_t d)
{
    uint32_t k;
    if (reg) return;                            /* (horloge, broche 7, banques : déjà réglés par la page) */
    if (o->cmd >= 0) {                          /* deuxième octet : les voix (bits 4-7) et le volume */
        for (k = 0; k < 4u; k++) if (d >> (4u + k) & 1u) {
            oki_voice_t *v = &o->v[k];
            uint32_t b = (uint32_t)o->cmd * 8u, s, e;
            if (v->playing) continue;           /* (comme MAME : une voix qui joue n'est pas relancée) */
            s = (oki_rd(o, b) << 16 | oki_rd(o, b + 1u) << 8 | oki_rd(o, b + 2u)) & 0x3FFFFu;
            e = (oki_rd(o, b + 3u) << 16 | oki_rd(o, b + 4u) << 8 | oki_rd(o, b + 5u)) & 0x3FFFFu;
            if (s < e) {
                v->playing = 1; v->base = s; v->sample = 0; v->count = 2u * (e - s + 1u);
                v->signal = -2; v->step = 0; v->vol = OKI_VOL[d & 15u];
            } else v->playing = 0;
        }
        o->cmd = -1;
    } else if (d & 0x80u) o->cmd = (int32_t)(d & 0x7Fu);
    else for (k = 0; k < 4u; k++) if (d >> (3u + k) & 1u) o->v[k].playing = 0;
}

static int32_t oki_clock(oki_t *o)              /* un échantillon de la puce : la somme des voix */
{
    int32_t sum = 0;
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        oki_voice_t *v = &o->v[k];
        uint32_t nib;
        int32_t sv, d, s;
        if (!v->playing) continue;
        nib = oki_rd(o, v->base + (v->sample >> 1)) >> ((v->sample & 1u) ? 0 : 4) & 15u;
        sv = OKI_STEP[v->step];
        d = sv / 8 + (nib & 4u ? sv : 0) + (nib & 2u ? sv / 2 : 0) + (nib & 1u ? sv / 4 : 0);
        s = v->signal + (nib & 8u ? -d : d);
        v->signal = (int16_t)(s > 2047 ? 2047 : s < -2048 ? -2048 : s);
        s = v->step + OKI_SHIFT[nib & 7u];
        v->step = (int8_t)(s > 48 ? 48 : s < 0 ? 0 : s);
        if (!(o->mute >> k & 1u)) sum += v->signal * (int32_t)v->vol / 2;
        if (++v->sample >= v->count) v->playing = 0;
    }
    return sum;
}

static void oki_render(oki_t *o, int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x;
        o->acc += o->step_q16;
        while (o->acc >= 65536u) { o->acc -= 65536u; o->prev = o->cur; o->cur = oki_clock(o); }
        x = o->prev + ((o->cur - o->prev) >> 4) * (int32_t)(o->acc >> 4) / 256;   /* (sans 64 bits) */
        x *= OKI_GAIN;
        out[2u * i] += x; out[2u * i + 1u] += x;
    }
}
#endif
