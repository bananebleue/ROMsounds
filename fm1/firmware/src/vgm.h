/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : lecture des commandes d'un fichier VGM (https://vgmrips.net/wiki/VGM_Specification) pour la Mega Drive :
 * YM2612 (FM + DAC) et SN76489 (PSG) ; la Master System et la Game Gear (SN76489 seul, stéréo 0x4F) ; la Game Boy
 * (0xB3), la NES (0xB4) l'AY-3-8910 / YM2149 (0xA0 : Atari ST, Amstrad CPC, ZX Spectrum…) l'OPL2 / OPL (0x5A, 0x5B : AdLib) le YM2610 (0x58, 0x59 : Neo-Geo), le YM2151, le YM2203 et l'OKI MSM6295 (0x54, 0x55, 0xB8 : arcade). La page (web/romsounds.js) a déjà retiré l'en-tête, les étiquettes, les blocs de
 * données (la banque PCM arrive à part) et les commandes des autres puces ; les commandes arrivent une à une, par
 * vgm_get() (décompression au fil de l'eau dans l'app).
 *
 *   vgm_next(&v)   exécute les commandes jusqu'à la prochaine attente ; rend son nombre de trames à 44,1 kHz
 *                  (0 : fin du morceau)
 * L'app fournit : vgm_get() (octet suivant, -1 : fin de la partie en cours), vgm_restart() (1 : une autre partie
 * commence, la boucle par exemple ; 0 : fin), vgm_ym(port, reg, val), vgm_psg(val). */
#ifndef ROMSOUNDS_VGM_H
#define ROMSOUNDS_VGM_H
#include <stdint.h>

enum { VGM_W_OTHERCHIPS = 1, VGM_W_DACSTREAM = 2 };   /* avertissements (bits de v.warn, venus de la page) */

#define OPM_GAIN 1                              /* YM2151 : comme libvgm (vgm2wav, mesuré) */
#define OPN_GAIN 2                              /* YM2203 : de même */
#define OPNB_GAIN 1                             /* YM2610 : FM + ADPCM, comme libvgm (vgm2wav, mesuré) */
#define OPL_GAIN 1                              /* OPL : niveau de sortie, comme libvgm (vgm2wav, mesuré) */

typedef struct {
    uint32_t ym_clock, psg_clock;
    uint32_t total, loop_samples;       /* en trames à 44,1 kHz (loop_samples 0 : pas de boucle) */
    const uint8_t *pcm;                 /* banque PCM du YM2612 */
    uint32_t pcm_len, pcm_pos;
    uint32_t warn;
    uint32_t psg_cfg;                   /* bruit du SN76489 : rebouclage | largeur << 16 (0 : Sega) */
    uint32_t gb_clock, nes_clock;
    uint32_t bpm100, beat0;
    uint32_t ay_clock, ay_type;         /* AY-3-8910 / YM2149 (type VGM : 0x00 AY, 0x10 YM2149...) */
    uint32_t opl_clock, opl_type;
    uint32_t opnb_clock, opnb_type, adpcm_a_len;
    uint32_t opm_clock, opn_clock, oki_clock, kdsc_clock;   /* (kdsc : Konami 053260, à la place de l'OKI) */   /* arcade : YM2151, YM2203, OKI MSM6295 (bit 31 : broche 7 haute) */   /* YM2610 (type bit 0 : YM2610B) ; la banque : ADPCM-A puis ADPCM-B */       /* OPL2 / OPL (type : 1 YM3812, 2 YM3526) */
    uint32_t is_mod;                    /* 1 : un module Amiga (mod.h), pas des commandes VGM */             /* tempo deviné par la page (BPM x 100, 0 : inconnu), premier temps (trames) */
} vgm_t;

static int vgm_get(void);
static int vgm_restart(void);
static void vgm_ym(uint32_t port, uint32_t reg, uint32_t val);
static void vgm_psg(uint32_t val);
static void vgm_psg_stereo(uint32_t val);
static void vgm_gb(uint32_t reg, uint32_t val);
static void vgm_nes(uint32_t reg, uint32_t val);
static void vgm_ay(uint32_t reg, uint32_t val);
static void vgm_opl(uint32_t reg, uint32_t val);
static void vgm_opnb(uint32_t port, uint32_t reg, uint32_t val);
static void vgm_opm(uint32_t reg, uint32_t val);
static void vgm_opn(uint32_t reg, uint32_t val);
static void vgm_oki(uint32_t reg, uint32_t val);
static void vgm_kdsc(uint32_t reg, uint32_t val);

static uint32_t vgm_next(vgm_t *v)
{
    for (;;) {
        int c = vgm_get(), a, b;
        if (c < 0 || c == 0x66) {              /* fin de la partie : la suivante (la boucle), sinon la fin du morceau */
            if (vgm_restart()) continue;
            return 0;
        }
        if (c == 0x50) { if ((a = vgm_get()) >= 0) vgm_psg((uint32_t)a); continue; }
        if (c == 0x4F) { if ((a = vgm_get()) >= 0) vgm_psg_stereo((uint32_t)a); continue; }   /* Game Gear */
        if (c == 0x58 || c == 0x59) {          /* YM2610 (Neo-Geo) : ports 0 et 1 */
            a = vgm_get(); b = vgm_get();
            if (b >= 0) vgm_opnb((uint32_t)c - 0x58u, (uint32_t)a, (uint32_t)b);
            continue;
        }
        if (c == 0x54 || c == 0x55) {          /* YM2151 (OPM), YM2203 (OPN) : arcade */
            a = vgm_get(); b = vgm_get();
            if (b >= 0) { if (c == 0x54) vgm_opm((uint32_t)a, (uint32_t)b); else vgm_opn((uint32_t)a, (uint32_t)b); }
            continue;
        }
        if (c == 0xB8) {                       /* OKI MSM6295 (le 2e circuit, bit 7 : ignoré) */
            a = vgm_get(); b = vgm_get();
            if (b >= 0 && !(a & 0x80)) vgm_oki((uint32_t)a, (uint32_t)b);
            continue;
        }
        if (c == 0xBA) {                       /* Konami 053260 (le 2e circuit, bit 7 : ignoré) */
            a = vgm_get(); b = vgm_get();
            if (b >= 0 && !(a & 0x80)) vgm_kdsc((uint32_t)a, (uint32_t)b);
            continue;
        }
        if (c == 0x5A || c == 0x5B) {          /* YM3812 (OPL2 : AdLib, Sound Blaster), YM3526 (OPL) */
            a = vgm_get(); b = vgm_get();
            if (b >= 0) vgm_opl((uint32_t)a, (uint32_t)b);
            continue;
        }
        if (c == 0xA0) {                       /* AY-3-8910 / YM2149 (le 2e circuit, bit 7 : ignoré) */
            a = vgm_get(); b = vgm_get();
            if (b >= 0 && !(a & 0x80)) vgm_ay((uint32_t)a, (uint32_t)b);
            continue;
        }
        if (c == 0xB3 || c == 0xB4) {          /* Game Boy (0xFF10 + reg), NES ($4000 + reg) */
            a = vgm_get(); b = vgm_get();
            if (b >= 0 && !(a & 0x80)) { if (c == 0xB3) vgm_gb((uint32_t)a, (uint32_t)b); else vgm_nes((uint32_t)a, (uint32_t)b); }
            continue;
        }
        if (c == 0x52 || c == 0x53) {
            a = vgm_get(); b = vgm_get();
            if (b >= 0) vgm_ym((uint32_t)c - 0x52u, (uint32_t)a, (uint32_t)b);
            continue;
        }
        if (c == 0x61) {
            a = vgm_get(); b = vgm_get();
            if (b > 0 || a > 0) return (uint32_t)a | (uint32_t)b << 8;
            continue;
        }
        if (c == 0x62) return 735;
        if (c == 0x63) return 882;
        if (c == 0xE0) {
            uint32_t k, o = 0;
            for (k = 0; k < 4u; k++) { if ((a = vgm_get()) < 0) break; o |= (uint32_t)a << (8u * k); }
            v->pcm_pos = o;
            continue;
        }
        if (c >= 0x70 && c <= 0x7F) return (uint32_t)(c & 15) + 1u;
        if (c >= 0x80 && c <= 0x8F) {          /* DAC : un octet de la banque PCM, puis attendre n */
            vgm_ym(0, 0x2A, v->pcm && v->pcm_pos < v->pcm_len ? v->pcm[v->pcm_pos] : 0x80u);
            v->pcm_pos++;
            if (c & 15) return (uint32_t)(c & 15);
            continue;
        }
        /* (la page n'envoie rien d'autre ; au cas où : ignoré) */
    }
}
#endif
