/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : les effets joués en direct sur la sortie (après les puces), écrits pour ROMsounds. Entiers seulement.
 *
 *   filtre   fx.filt -20..20 : 0 éteint ; < 0 passe-bas (8 kHz -> 150 Hz) ; > 0 passe-haut (40 Hz -> 5 kHz).
 *            Filtre d'état variable (Chamberlin), un peu de résonance.
 *   grain    fx.grain 0..12 : moins de bits et échantillons tenus (le son « 8 bits » poussé plus loin).
 *   écho     fx.echo 0..10 (mélange et retour), fx.time 1..11 (x 50 ms).
 *   boucle   fx_stutter(1) : rejoue en boucle les fx.time derniers x 50 ms tant que c'est tenu (le morceau continue
 *            derrière : au relâché, on retombe où il en est).
 * L'écho et la boucle utilisent une mémoire de FX_FRAMES trames stéréo (fx_attach) ; sans elle, filtre et grain
 * marchent seuls. */
#ifndef ROMSOUNDS_FX_H
#define ROMSOUNDS_FX_H
#include <stdint.h>

#define FX_FRAMES 24576u                       /* 0,557 s stéréo, 96 Ko */
#define FX_BYTES (FX_FRAMES * 4u)

typedef struct {
    int8_t filt;
    uint8_t grain, echo, time;
    int32_t lo[2], bd[2];                       /* filtre */
    uint32_t hold_n; int32_t hold[2];           /* grain */
    int16_t *buf; uint32_t w;                   /* mémoire : écho, boucle */
    uint8_t stut; uint32_t st_from, st_len, st_pos;
} fx_t;

/* fréquence du filtre : f = 2 sin(pi fc / 44100), Q14, pour |filt| = 1..20 */
static const uint16_t FX_LP[21] = {0, 17680, 14614, 12002, 9814, 8003, 6514, 5296, 4303, 3494, 2836,
                                   2301, 1867, 1515, 1229, 997, 809, 656, 532, 432, 350};
static const uint16_t FX_HP[21] = {0, 93, 120, 155, 200, 258, 333, 429, 553, 713, 919,
                                   1185, 1528, 1969, 2538, 3270, 4212, 5420, 6967, 8938, 11426};
/* les fréquences, pour l'écran (Hz) */
static const uint16_t FX_LP_HZ[21] = {0, 8000, 6489, 5264, 4270, 3463, 2809, 2279, 1849, 1499, 1216, 987, 800, 649, 527, 427, 346, 281, 228, 185, 150};
static const uint16_t FX_HP_HZ[21] = {0, 40, 52, 66, 86, 111, 143, 184, 237, 305, 394, 508, 655, 844, 1088, 1403, 1809, 2333, 3008, 3878, 5000};
#define FX_Q 11000                              /* amortissement (Q14) : 0,67, une résonance douce */

static void fx_reset(fx_t *f)                   /* tout éteint (la mémoire reste attachée) */
{
    int16_t *b = f->buf;
    uint32_t i;
    for (i = 0; i < sizeof *f; i++) ((uint8_t *)f)[i] = 0;
    f->buf = b;
    f->time = 3;
}
static void fx_attach(fx_t *f, int16_t *buf)    /* la mémoire (ou 0), vidée */
{
    uint32_t i;
    f->buf = buf; f->w = 0; f->stut = 0;
    if (buf) for (i = 0; i < FX_FRAMES * 2u; i++) buf[i] = 0;
}
static uint32_t fx_len(const fx_t *f) { uint32_t n = (uint32_t)f->time * 2205u; return n < FX_FRAMES ? n : FX_FRAMES - 1u; }
static void fx_stutter(fx_t *f, uint32_t on)
{
    if (!f->buf) return;
    if (on && !f->stut) { f->st_len = fx_len(f); f->st_from = (f->w + FX_FRAMES - f->st_len) % FX_FRAMES; f->st_pos = 0; }
    f->stut = (uint8_t)(on ? 1u : 0u);
}
static int32_t fx_clip16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

static void fx_process(fx_t *f, int32_t *mix, uint32_t n)
{
    uint32_t i, c;
    int32_t fc = f->filt < 0 ? FX_LP[-f->filt] : FX_HP[f->filt];
    uint32_t bits = f->grain ? (uint32_t)f->grain + 3u : 0u, hold = 1u + f->grain / 3u;
    uint32_t d = fx_len(f);
    for (i = 0; i < n; i++) {
        int32_t *x = mix + 2u * i;
        if (f->filt) for (c = 0; c < 2u; c++) {
            int32_t in = x[c], hi;
            f->lo[c] += fc * (f->bd[c] >> 2) >> 12;
            hi = in - f->lo[c] - (FX_Q * (f->bd[c] >> 2) >> 12);
            if (hi > 400000) hi = 400000; else if (hi < -400000) hi = -400000;   /* (les produits restent sur 32 bits) */
            f->bd[c] += fc * (hi >> 2) >> 12;
            if (f->bd[c] > 400000) f->bd[c] = 400000; else if (f->bd[c] < -400000) f->bd[c] = -400000;
            if (f->lo[c] > 400000) f->lo[c] = 400000; else if (f->lo[c] < -400000) f->lo[c] = -400000;
            x[c] = f->filt < 0 ? f->lo[c] : hi;
        }
        if (bits) {
            if (++f->hold_n >= hold) { f->hold_n = 0; f->hold[0] = x[0] >> bits << bits; f->hold[1] = x[1] >> bits << bits; }
            x[0] = f->hold[0]; x[1] = f->hold[1];
        }
        if (!f->buf) continue;
        if (f->stut) {                          /* boucle : on rejoue, on n'enregistre pas */
            uint32_t r = (f->st_from + f->st_pos) % FX_FRAMES;
            x[0] = f->buf[2u * r]; x[1] = f->buf[2u * r + 1u];
            if (++f->st_pos >= f->st_len) f->st_pos = 0;
            continue;
        }
        {
            uint32_t r = (f->w + FX_FRAMES - d) % FX_FRAMES;
            int32_t dl = f->buf[2u * r], dr = f->buf[2u * r + 1u], g = f->echo * 3;   /* retour : 0 .. 30/48 */
            int32_t l = x[0], rr = x[1];
            if (f->echo) { x[0] = l + dl * g / 48; x[1] = rr + dr * g / 48; l += dl * g / 48; rr += dr * g / 48; }
            f->buf[2u * f->w] = (int16_t)fx_clip16(l);
            f->buf[2u * f->w + 1u] = (int16_t)fx_clip16(rr);
            if (++f->w >= FX_FRAMES) f->w = 0;
        }
    }
}
#endif
