/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : un lecteur de modules Amiga (ProTracker « M.K. », 31 échantillons ; Soundtracker 15 échantillons ; 6 et 8
 * voies des trackers PC), écrit pour ROMsounds d'après la description publique du format. Paula : échantillons 8 bits,
 * période -> 3 546 895 / période Hz (PAL), voies 1 et 4 à gauche, 2 et 3 à droite (un peu mêlées). Entiers seulement.
 * Effets : 0 arpège, 1 / 2 / 3 portamentos, 4 vibrato, 5 / 6, 7 tremolo, 9 décalage, A volume, B saut, C volume,
 * D cassure, E (1, 2, 5, 6, 9, A, B, C, D, E), F vitesse / tempo. Le morceau reboucle sur sa position de reprise.
 *
 *   mod_load(&m, données, taille)   0, ou -1 (pas un module)
 *   mod_render(&m, out, n)          ajoute n trames stéréo (int32, échelle Q15)
 *   m.tempo_q8 : 256 = vitesse normale ; m.rate_q16 : 65536 = hauteur normale ; m.mute : bit = voie coupée */
#ifndef ROMSOUNDS_MOD_H
#define ROMSOUNDS_MOD_H
#include <stdint.h>

#define MOD_MAXCH 8u
#define MOD_PAULA_Q16 5270959u                  /* 3 546 895 / 44 100, Q16 */

typedef struct { const int8_t *d; uint32_t len, ls, ll; uint8_t ft, vol, sh; } mod_smp_t;   /* sh : données gardées 1 octet sur 2^sh (module réduit par la page) */
typedef struct {
    const mod_smp_t *s;
    uint32_t pos, frac, step;
    int32_t per, target, vol, out_per, out_vol;
    uint8_t on, ft, eff, prm, port, vspd, vdep, vpos, tspd, tdep, tpos, offs, lrow, lcnt;
    uint8_t dl_smp; uint16_t dl_per;            /* note retardée (EDx) */
} mod_ch_t;
typedef struct {
    mod_smp_t smp[32];
    const uint8_t *ord, *pat;
    uint32_t nch, nord, restart, npat;
    uint32_t speed, tempo, tick, row, cur, pdelay, rows_done;
    int32_t brk, jump, loopj;
    uint32_t left_q8;                           /* trames de sortie avant le prochain tic (Q8) */
    uint32_t tempo_q8, rate_q16, mute;
    mod_ch_t c[MOD_MAXCH];
} mod_t;

static const uint16_t MOD_PER[16][36] = {
    {856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226, 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113},
    {850, 802, 757, 715, 675, 637, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357, 337, 318, 300, 284, 268, 253, 238, 225, 212, 201, 189, 179, 169, 159, 150, 142, 134, 126, 119, 113},
    {844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355, 335, 316, 298, 282, 266, 251, 237, 223, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118, 112},
    {838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352, 332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118, 111},
    {832, 785, 741, 699, 660, 623, 588, 555, 524, 494, 467, 441, 416, 392, 370, 350, 330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 124, 117, 110},
    {826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347, 328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116, 109},
    {820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345, 325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115, 109},
    {814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342, 323, 305, 288, 272, 256, 242, 228, 216, 203, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114, 108},
    {907, 856, 808, 763, 720, 679, 641, 605, 571, 539, 509, 480, 453, 428, 404, 381, 360, 340, 321, 303, 286, 270, 254, 240, 227, 214, 202, 191, 180, 170, 160, 151, 143, 135, 127, 120},
    {900, 850, 802, 757, 715, 675, 637, 601, 567, 535, 505, 477, 450, 425, 401, 379, 357, 337, 318, 300, 284, 268, 253, 238, 225, 212, 201, 189, 179, 169, 159, 150, 142, 134, 126, 119},
    {894, 844, 796, 752, 709, 670, 632, 597, 563, 532, 502, 474, 447, 422, 398, 376, 355, 335, 316, 298, 282, 266, 251, 237, 223, 211, 199, 188, 177, 167, 158, 149, 141, 133, 125, 118},
    {887, 838, 791, 746, 704, 665, 628, 592, 559, 528, 498, 470, 444, 419, 395, 373, 352, 332, 314, 296, 280, 264, 249, 235, 222, 209, 198, 187, 176, 166, 157, 148, 140, 132, 125, 118},
    {881, 832, 785, 741, 699, 660, 623, 588, 555, 524, 494, 467, 441, 416, 392, 370, 350, 330, 312, 294, 278, 262, 247, 233, 220, 208, 196, 185, 175, 165, 156, 147, 139, 131, 124, 117},
    {875, 826, 779, 736, 694, 655, 619, 584, 551, 520, 491, 463, 437, 413, 390, 368, 347, 328, 309, 292, 276, 260, 245, 232, 219, 206, 195, 184, 174, 164, 155, 146, 138, 130, 123, 116},
    {868, 820, 774, 730, 689, 651, 614, 580, 547, 516, 487, 460, 434, 410, 387, 365, 345, 325, 307, 290, 274, 258, 244, 230, 217, 205, 193, 183, 172, 163, 154, 145, 137, 129, 122, 115},
    {862, 814, 768, 725, 684, 646, 610, 575, 543, 513, 484, 457, 431, 407, 384, 363, 342, 323, 305, 288, 272, 256, 242, 228, 216, 203, 192, 181, 171, 161, 152, 144, 136, 128, 121, 114}};
static const uint8_t MOD_SIN[32] = {0, 25, 50, 74, 98, 120, 142, 162, 180, 197, 212, 225, 236, 244, 250, 254,
                                    255, 254, 250, 244, 236, 225, 212, 197, 180, 162, 142, 120, 98, 74, 50, 25};

static uint32_t mod_be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t mod_note(uint32_t per)          /* la note (0..35) la plus proche d'une période (sans réglage fin) */
{
    uint32_t i, best = 0, d = ~0u;
    for (i = 0; i < 36u; i++) { uint32_t e = per > MOD_PER[0][i] ? per - MOD_PER[0][i] : MOD_PER[0][i] - per; if (e < d) { d = e; best = i; } }
    return best;
}
static uint32_t mod_tag_ch(const uint8_t *t)    /* nombre de voies d'après la signature (0 : pas de signature) */
{
    if ((t[0] == 'M' && (t[1] == '.' || t[1] == '!') && t[2] == 'K' && (t[3] == '.' || t[3] == '!')) ||
        (t[0] == 'F' && t[1] == 'L' && t[2] == 'T' && t[3] == '4')) return 4;
    if ((t[0] == 'F' && t[1] == 'L' && t[2] == 'T' && t[3] == '8') || (t[0] == 'O' && t[1] == 'C' && t[2] == 'T' && t[3] == 'A') ||
        (t[0] == 'C' && t[1] == 'D' && t[2] == '8' && t[3] == '1')) return 8;
    if (t[1] == 'C' && t[2] == 'H' && t[3] == 'N' && t[0] >= '1' && t[0] <= '8') return (uint32_t)(t[0] - '0');
    if (t[2] == 'C' && t[3] == 'H' && t[0] == '0' && t[1] >= '1' && t[1] <= '8') return (uint32_t)(t[1] - '0');
    return 0;
}

/* sh_lo, sh_hi : pour l'échantillon i, sh = bit i de sh_lo + 2 x bit i de sh_hi (0 : module tel quel) */
static int mod_load(mod_t *m, const uint8_t *d, uint32_t size, uint32_t sh_lo, uint32_t sh_hi)
{
    uint32_t i, ns, hdr, nch, at, p;
    for (i = 0; i < sizeof *m; i++) ((uint8_t *)m)[i] = 0;
    if (size < 600u) return -1;
    nch = size >= 1084u ? mod_tag_ch(d + 1080) : 0u;
    if (nch) { ns = 31; hdr = 1084; } else { ns = 15; hdr = 600; nch = 4; }   /* (Soundtracker : 15 échantillons, sans signature) */
    m->nch = nch;
    m->nord = d[20u + ns * 30u]; m->restart = d[21u + ns * 30u];
    if (!m->nord || m->nord > 128u) return -1;
    if (m->restart >= m->nord) m->restart = 0;
    m->ord = d + 22u + ns * 30u;
    for (i = 0; i < 128u; i++) if (m->ord[i] + 1u > m->npat && (i < m->nord || m->ord[i] < 64u)) m->npat = m->ord[i] + 1u;
    m->pat = d + hdr;
    at = hdr + m->npat * 64u * 4u * nch;
    if (at > size) return -1;
    for (i = 1, p = 20; i <= ns; i++, p += 30u) {
        mod_smp_t *s = &m->smp[i];
        s->len = mod_be16(d + p + 22u) * 2u; s->ft = d[p + 24u] & 15u; s->vol = d[p + 25u] > 64u ? 64u : d[p + 25u];
        s->ls = mod_be16(d + p + 26u) * 2u; s->ll = mod_be16(d + p + 28u) * 2u;
        s->sh = (uint8_t)((sh_lo >> i & 1u) | (sh_hi >> i & 1u) << 1);
        s->d = (const int8_t *)(d + at);
        {
            uint32_t st = (s->len + (1u << s->sh) - 1u) >> s->sh;   /* octets rangés */
            if (at + st > size) s->len = at < size ? (size - at) << s->sh : 0u;   /* (fichier coupé : ce qui reste) */
            at += st;
        }
        if (s->ls + s->ll > s->len) { if (s->ls >= s->len) s->ll = 0; else s->ll = s->len - s->ls; }
    }
    m->speed = 6; m->tempo = 125; m->brk = -1; m->jump = -1; m->loopj = -1;
    m->tempo_q8 = 256; m->rate_q16 = 65536;
    m->tick = 0xFFFFu;                          /* le premier tic lit la première ligne */
    return 0;
}

static void mod_trigger(mod_ch_t *c, uint32_t per)
{
    c->per = MOD_PER[c->ft][mod_note(per)];
    c->pos = 0; c->frac = 0; c->on = c->s && c->s->len > 2u;
    c->vpos = 0;
    c->tpos = 0;
}
static int32_t mod_wave(uint8_t pos) { int32_t v = MOD_SIN[pos & 31u]; return pos & 32u ? -v : v; }

static void mod_row(mod_t *m)                   /* tic 0 : la ligne */
{
    uint32_t k;
    const uint8_t *r = m->pat + ((uint32_t)m->ord[m->cur] * 64u + m->row) * 4u * m->nch;
    for (k = 0; k < m->nch; k++, r += 4) {
        mod_ch_t *c = &m->c[k];
        uint32_t s = (r[0] & 0xF0u) | r[2] >> 4, per = (uint32_t)(r[0] & 15u) << 8 | r[1], x, y;
        c->eff = r[2] & 15u; c->prm = r[3];
        x = c->prm >> 4; y = c->prm & 15u;
        if (s && s < 32u) { c->s = &m->smp[s]; c->vol = c->s->vol; c->ft = c->s->ft; }
        if (c->eff == 0xEu && x == 5u) c->ft = (uint8_t)y;
        if (per) {
            if (c->eff == 3u || c->eff == 5u) c->target = MOD_PER[c->ft][mod_note(per)];
            else if (c->eff == 0xEu && x == 0xDu && y) { c->dl_per = (uint16_t)per; c->dl_smp = 1; }
            else { mod_trigger(c, per); if (c->eff == 9u) { if (c->prm) c->offs = c->prm; c->pos = (uint32_t)c->offs << 8; if (c->s && c->pos >= c->s->len) c->on = 0; } }
        }
        switch (c->eff) {
        case 3: if (c->prm) c->port = c->prm; break;
        case 4: if (x) c->vspd = (uint8_t)x; if (y) c->vdep = (uint8_t)y; break;
        case 7: if (x) c->tspd = (uint8_t)x; if (y) c->tdep = (uint8_t)y; break;
        case 9: if (c->prm) c->offs = c->prm; break;
        case 0xB: m->jump = (int32_t)c->prm; if (m->brk < 0) m->brk = 0; break;
        case 0xC: c->vol = c->prm > 64u ? 64 : (int32_t)c->prm; break;
        case 0xD: m->brk = (int32_t)(x * 10u + y); if (m->brk > 63) m->brk = 0; if (m->jump < 0) m->jump = (int32_t)m->cur + 1; break;
        case 0xE:
            switch (x) {
            case 1: c->per -= (int32_t)y; if (c->per < 113) c->per = 113; break;
            case 2: c->per += (int32_t)y; if (c->per > 856) c->per = 856; break;
            case 6:
                if (!y) c->lrow = (uint8_t)m->row;
                else if (!c->lcnt) { c->lcnt = (uint8_t)y; m->loopj = c->lrow; }
                else if (--c->lcnt) m->loopj = c->lrow;
                break;
            case 0xA: c->vol += (int32_t)y; if (c->vol > 64) c->vol = 64; break;
            case 0xB: c->vol -= (int32_t)y; if (c->vol < 0) c->vol = 0; break;
            case 0xC: if (!y) c->vol = 0; break;
            case 0xE: m->pdelay = y; break;
            default: break;
            }
            break;
        case 0xF: if (c->prm && c->prm < 32u) m->speed = c->prm; else if (c->prm) m->tempo = c->prm; break;
        default: break;
        }
        c->out_per = c->per; c->out_vol = c->vol;
    }
}

static void mod_volslide(mod_ch_t *c) { uint32_t x = c->prm >> 4, y = c->prm & 15u; c->vol += x ? (int32_t)x : -(int32_t)y; if (c->vol < 0) c->vol = 0; if (c->vol > 64) c->vol = 64; }
static void mod_porta(mod_ch_t *c)
{
    if (!c->target) return;
    if (c->per < c->target) { c->per += c->port; if (c->per > c->target) c->per = c->target; }
    else if (c->per > c->target) { c->per -= c->port; if (c->per < c->target) c->per = c->target; }
}
static void mod_ticks(mod_t *m)                 /* les tics suivants : les effets qui évoluent */
{
    uint32_t k;
    for (k = 0; k < m->nch; k++) {
        mod_ch_t *c = &m->c[k];
        uint32_t x = c->prm >> 4, y = c->prm & 15u;
        c->out_per = c->per; c->out_vol = c->vol;
        switch (c->eff) {
        case 0:
            if (c->prm && c->per) { uint32_t n = mod_note((uint32_t)c->per), a = m->tick % 3u == 1u ? x : m->tick % 3u == 2u ? y : 0u;
                                    c->out_per = MOD_PER[c->ft][n + a > 35u ? 35u : n + a]; }
            break;
        case 1: c->per -= (int32_t)c->prm; if (c->per < 113) c->per = 113; c->out_per = c->per; break;
        case 2: c->per += (int32_t)c->prm; if (c->per > 856) c->per = 856; c->out_per = c->per; break;
        case 3: mod_porta(c); c->out_per = c->per; break;
        case 5: mod_porta(c); c->out_per = c->per; mod_volslide(c); c->out_vol = c->vol; break;
        case 4: case 6:
            c->out_per = c->per + (mod_wave(c->vpos) * c->vdep >> 7);
            c->vpos = (uint8_t)((c->vpos + c->vspd) & 63u);
            if (c->eff == 6u) { mod_volslide(c); c->out_vol = c->vol; }
            break;
        case 7:
            c->out_vol = c->vol + (mod_wave(c->tpos) * c->tdep >> 6);
            if (c->out_vol < 0) c->out_vol = 0;
            if (c->out_vol > 64) c->out_vol = 64;
            c->tpos = (uint8_t)((c->tpos + c->tspd) & 63u);
            break;
        case 0xA: mod_volslide(c); c->out_vol = c->vol; break;
        case 0xE:
            if (x == 9u && y && !(m->tick % y)) { c->pos = 0; c->frac = 0; c->on = c->s && c->s->len > 2u; }
            else if (x == 0xCu && m->tick == y) { c->vol = 0; c->out_vol = 0; }
            else if (x == 0xDu && m->tick == y && c->dl_smp) { c->dl_smp = 0; mod_trigger(c, c->dl_per); c->out_per = c->per; }
            break;
        default: break;
        }
    }
}

static void mod_tick(mod_t *m)
{
    uint32_t k;
    if (++m->tick >= m->speed * (1u + m->pdelay)) {   /* ligne suivante */
        if (m->tick != 65536u) {               /* (0xFFFF + 1 : le tout premier tic, voir mod_load) */
            m->tick = 0; m->pdelay = 0; m->rows_done++;
            if (m->loopj >= 0) { m->row = (uint32_t)m->loopj; m->loopj = -1; m->jump = -1; m->brk = -1; }
            else if (m->jump >= 0) { m->cur = (uint32_t)m->jump; m->row = m->brk >= 0 ? (uint32_t)m->brk : 0u; m->jump = -1; m->brk = -1; }
            else if (++m->row >= 64u) { m->row = 0; m->cur++; }
            if (m->cur >= m->nord) { m->cur = m->restart; }
        } else m->tick = 0;                     /* (le tout premier tic) */
        mod_row(m);
    } else mod_ticks(m);
    for (k = 0; k < m->nch; k++) {
        mod_ch_t *c = &m->c[k];
        uint32_t p = c->out_per < 28 ? 28u : (uint32_t)c->out_per;
        c->step = c->out_per ? ((MOD_PAULA_Q16 / p) >> 16) * m->rate_q16 + (((MOD_PAULA_Q16 / p) & 0xFFFFu) * (m->rate_q16 >> 2) >> 14) : 0u;
    }
}

static void mod_render(mod_t *m, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        int32_t l = 0, r = 0;
        while (m->left_q8 < 256u) { mod_tick(m); m->left_q8 += ((110250u << 8) / m->tempo) * 256u / (m->tempo_q8 ? m->tempo_q8 : 256u); }
        m->left_q8 -= 256u;
        for (k = 0; k < m->nch; k++) {
            mod_ch_t *c = &m->c[k];
            const mod_smp_t *s = c->s;
            int32_t v;
            if (!c->on || !s) continue;
            if (c->pos >= s->len || (s->ll > 2u && c->pos >= s->ls + s->ll)) {
                if (s->ll > 2u) c->pos = s->ls + (c->pos - s->ls) % s->ll;
                else { c->on = 0; continue; }
            }
            if (!s->sh) v = s->d[c->pos];
            else {                              /* échantillon réduit : entre deux points gardés, en ligne droite */
                uint32_t j = c->pos >> s->sh, f = c->pos & ((1u << s->sh) - 1u), st = (s->len + (1u << s->sh) - 1u) >> s->sh;
                int32_t a = s->d[j], b = j + 1u < st ? s->d[j + 1u] : a;
                v = a + (((b - a) * (int32_t)f) >> s->sh);
            }
            v *= c->out_vol;
            c->frac += c->step; c->pos += c->frac >> 16; c->frac &= 0xFFFFu;
            if (m->mute >> k & 1u) continue;
            if ((k & 3u) == 0u || (k & 3u) == 3u) l += v; else r += v;
        }
        out[2u * i] += (l * 3 + r) >> 1;        /* x 2, voies un peu mêlées (75 / 25) */
        out[2u * i + 1u] += (r * 3 + l) >> 1;
    }
}
#endif
