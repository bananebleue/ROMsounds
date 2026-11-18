/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : les effets joués en direct sur la sortie (après les puces), écrits pour ROMsounds. Entiers seulement.
 *
 *   filtre     fx.filt -20..20 : 0 éteint ; < 0 passe-bas (8 kHz -> 150 Hz) ; > 0 passe-haut (40 Hz -> 5 kHz).
 *              Filtre d'état variable (Chamberlin), un peu de résonance.
 *   grain      fx.grain 0..12 : moins de bits et échantillons tenus (le son « 8 bits » poussé plus loin).
 *   modulation fx.mod -10..10 : < 0 phaser (4 passe-tout balayés), > 0 flanger (retard de 0,3 à 6 ms balayé, réinjecté).
 *   écho       fx.echo 0..10 : le nombre de répétitions (0 éteint) ; fx.time 1..11 (x 50 ms) : leur espacement.
 *   boucle     fx_loop : rejoue en boucle un bout de ce qui vient d'être joué tant que c'est tenu (le morceau continue
 *              derrière : au relâché, on retombe où il en est). Beat roll : le bout commence sur le dernier temps et dure
 *              un demi-temps ou un temps ; il est d'abord joué en entier, puis répété.
 *   backspin   fx_backspin : ce qui vient d'être joué, à l'envers, de plus en plus vite, puis le morceau revient (~0,4 s).
 * L'écho, la boucle et le backspin utilisent une mémoire de FX_FRAMES trames stéréo (fx_attach) ; sans elle, filtre,
 * grain et modulation marchent seuls. */
#ifndef ROMSOUNDS_FX_H
#define ROMSOUNDS_FX_H
#include <stdint.h>

#define FX_FRAMES 32768u                       /* 0,743 s stéréo, 128 Ko */
#define FX_BYTES (FX_FRAMES * 4u)
#define FX_MODN 512u                            /* retard du flanger : 11,6 ms au plus */
#define FX_SPIN 16000u                          /* backspin : 0,36 s */

typedef struct {
    int8_t filt, mod;
    uint8_t grain, echo, time;
    int32_t lo[2], bd[2];                       /* filtre */
    uint32_t hold_n; int32_t hold[2];           /* grain */
    uint32_t lfo;                               /* modulation : phase */
    int16_t md[2][FX_MODN]; uint32_t mw;        /* flanger */
    int32_t ap_x[2][4], ap_y[2][4], ph_fb[2];   /* phaser */
    int16_t *buf; uint32_t w;                   /* mémoire : écho, boucle, backspin */
    uint8_t loop;                               /* 0 ; 1 : en boucle ; 2 : beat roll, premier passage (on enregistre) */
    uint32_t st_from, st_len, st_pos;
    uint32_t spin, spin_pos_q8, spin_v_q8;      /* backspin : trames restantes, position (Q8, à reculons), vitesse (Q8) */
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
static const uint8_t FX_ECHO_FB[11] = {0, 0, 14, 20, 24, 28, 31, 34, 36, 38, 40};   /* retour (/48) : 1 .. ~10 répétitions */

static void fx_reset(fx_t *f)                   /* réglages à zéro (la mémoire reste attachée, ce qui sonne s'arrête) */
{
    f->filt = 0; f->mod = 0; f->grain = 0; f->echo = 0; f->time = 3;
    f->lo[0] = f->lo[1] = f->bd[0] = f->bd[1] = 0;
    f->loop = 0; f->spin = 0;
}
static void fx_attach(fx_t *f, int16_t *buf)    /* la mémoire (ou 0), vidée */
{
    uint32_t i;
    f->buf = buf; f->w = 0; f->loop = 0; f->spin = 0;
    if (buf) for (i = 0; i < FX_FRAMES * 2u; i++) buf[i] = 0;
}
static uint32_t fx_len(const fx_t *f) { uint32_t n = (uint32_t)f->time * 2205u; return n < FX_FRAMES ? n : FX_FRAMES - 1u; }
/* boucler len trames. Boucle simple : les len dernières. Beat roll : le bout a commencé il y a back trames ; on le joue
 * jusqu'au bout, puis on le répète */
static void fx_loop(fx_t *f, uint32_t len, uint32_t back, uint32_t roll)
{
    if (!f->buf || !len) return;
    while (len >= FX_FRAMES) len >>= 1;
    if (back >= len) back %= len;
    f->st_len = len; f->spin = 0;
    if (roll) { f->st_from = (f->w + FX_FRAMES - back) % FX_FRAMES; f->st_pos = back; f->loop = 2; }
    else { f->st_from = (f->w + FX_FRAMES - len) % FX_FRAMES; f->st_pos = 0; f->loop = 1; }
}
static void fx_backspin(fx_t *f)
{
    if (!f->buf) return;
    f->spin = FX_SPIN; f->spin_pos_q8 = ((f->w + FX_FRAMES - 1u) % FX_FRAMES) << 8; f->spin_v_q8 = 256u; f->loop = 0;
}
static int32_t fx_clip16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

static void fx_process(fx_t *f, int32_t *mix, uint32_t n)
{
    uint32_t i, c;
    int32_t fc = f->filt < 0 ? FX_LP[-f->filt] : FX_HP[f->filt];
    uint32_t bits = f->grain ? (uint32_t)f->grain + 3u : 0u, hold = 1u + f->grain / 3u;
    uint32_t d = fx_len(f);
    int32_t m = f->mod < 0 ? -f->mod : f->mod;
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
        if (m) {                                 /* LFO triangle 0..65535 : flanger 0,25 Hz, phaser 0,4 Hz */
            uint32_t ph, tri;
            f->lfo += f->mod > 0 ? 24348u : 38956u;   /* 2^32 x Hz / 44100 */
            ph = f->lfo >> 15;
            tri = ph < 65536u ? ph : 131071u - ph;
            x[0] = fx_clip16(x[0]); x[1] = fx_clip16(x[1]);
            if (f->mod > 0) {                    /* flanger : retard de 14 à 14 + 25 x m trames, réinjecté */
                uint32_t dq8 = (14u << 8) + (tri * (uint32_t)m * 25u >> 8);
                for (c = 0; c < 2u; c++) {
                    uint32_t di = dq8 >> 8, fr = dq8 & 255u;
                    uint32_t r0 = (f->mw + FX_MODN - di) % FX_MODN, r1 = (r0 + FX_MODN - 1u) % FX_MODN;
                    int32_t dl = (f->md[c][r0] * (int32_t)(256u - fr) + f->md[c][r1] * (int32_t)fr) >> 8;
                    int32_t in = x[c];
                    f->md[c][f->mw] = (int16_t)fx_clip16(in + dl * (16 + 3 * m) / 64);
                    x[c] = (in + dl) * 3 / 4;
                }
                f->mw = (f->mw + 1u) % FX_MODN;
            } else {                             /* phaser : 4 passe-tout, coefficient balayé de -0,2 à 0,9 (Q15) */
                int32_t a = -6554 + (int32_t)(tri * 36045u >> 16);
                for (c = 0; c < 2u; c++) {
                    int32_t in = x[c], y = fx_clip16(in + f->ph_fb[c] * m / 20), k;
                    for (k = 0; k < 4; k++) {   /* y[n] = a (x[n] - y[n-1]) + x[n-1] */
                        int32_t o = fx_clip16((a * ((y - f->ap_y[c][k]) >> 1) >> 14) + f->ap_x[c][k]);
                        f->ap_x[c][k] = y; f->ap_y[c][k] = o; y = o;
                    }
                    f->ph_fb[c] = y;
                    x[c] = (in + y * m / 10) * 2 / 3;
                }
            }
        }
        if (!f->buf) continue;
        if (f->spin) {                           /* backspin : à reculons, de 1 à 3 fois plus vite, la fin en fondu */
            uint32_t r = (f->spin_pos_q8 >> 8) % FX_FRAMES, g = f->spin > 6000u ? 256u : f->spin * 256u / 6000u;
            x[0] = f->buf[2u * r] * (int32_t)g >> 8; x[1] = f->buf[2u * r + 1u] * (int32_t)g >> 8;
            f->spin_pos_q8 = (f->spin_pos_q8 + (FX_FRAMES << 8) - f->spin_v_q8) % (FX_FRAMES << 8);
            if (!(f->spin % 31u)) f->spin_v_q8++;
            f->spin--;
            continue;
        }
        if (f->loop == 1u) {                     /* boucle : on rejoue, on n'enregistre pas */
            uint32_t r = (f->st_from + f->st_pos) % FX_FRAMES;
            x[0] = f->buf[2u * r]; x[1] = f->buf[2u * r + 1u];
            if (++f->st_pos >= f->st_len) f->st_pos = 0;
            continue;
        }
        if (f->loop == 2u && ++f->st_pos >= f->st_len) { f->st_pos = 0; f->loop = 1; }   /* beat roll : le bout est complet */
        {
            uint32_t r = (f->w + FX_FRAMES - d) % FX_FRAMES;
            int32_t dl = f->buf[2u * r], dr = f->buf[2u * r + 1u], fb = FX_ECHO_FB[f->echo];
            int32_t l = x[0], rr = x[1];
            if (f->echo) { x[0] = l + dl / 2; x[1] = rr + dr / 2; l += dl * fb / 48; rr += dr * fb / 48; }
            f->buf[2u * f->w] = (int16_t)fx_clip16(l);
            f->buf[2u * f->w + 1u] = (int16_t)fx_clip16(rr);
            if (++f->w >= FX_FRAMES) f->w = 0;
        }
    }
}
#endif
