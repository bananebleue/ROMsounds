/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : lecture des commandes d'un fichier VGM (https://vgmrips.net/wiki/VGM_Specification) pour la Mega Drive :
 * YM2612 (FM + DAC) et SN76489 (PSG). La page (web/romsounds.js) a déjà retiré l'en-tête, les étiquettes, les blocs de
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

typedef struct {
    uint32_t ym_clock, psg_clock;
    uint32_t total, loop_samples;       /* en trames à 44,1 kHz (loop_samples 0 : pas de boucle) */
    const uint8_t *pcm;                 /* banque PCM du YM2612 */
    uint32_t pcm_len, pcm_pos;
    uint32_t warn;
} vgm_t;

static int vgm_get(void);
static int vgm_restart(void);
static void vgm_ym(uint32_t port, uint32_t reg, uint32_t val);
static void vgm_psg(uint32_t val);

static uint32_t vgm_next(vgm_t *v)
{
    for (;;) {
        int c = vgm_get(), a, b;
        if (c < 0 || c == 0x66) {              /* fin de la partie : la suivante (la boucle), sinon la fin du morceau */
            if (vgm_restart()) continue;
            return 0;
        }
        if (c == 0x50) { if ((a = vgm_get()) >= 0) vgm_psg((uint32_t)a); continue; }
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
