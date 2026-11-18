/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le son de la NES (2A03, NTSC), écrit pour ROMsounds d'après la documentation publique (NESdev wiki).
 * 2 voies carrées (balayage), triangle (compteur linéaire), bruit (registre de 15 bits, deux modes), DMC (échantillons
 * 1 bit delta lus dans la mémoire du jeu, $8000-$FFFF) ; enveloppes, durées, compteur de trames (4 et 5 pas) ; mélangeur
 * non linéaire (tables de NESdev), passe-haut pour retirer le continu. Rendu direct à 44,1 kHz, entiers seulement.
 *
 *   nes_init(&a, horloge, mem)  1 789 772 Hz ; mem : 32 Ko, l'image $8000-$FFFF (données du DMC), ou 0
 *   nes_write(&a, reg, val)     reg : 0x00-0x17 = $4000-$4017 (commande VGM 0xB4)
 *   nes_render(&a, out, n)      ajoute n trames stéréo (int32, échelle Q15) */
#ifndef ROMSOUNDS_NES_APU_H
#define ROMSOUNDS_NES_APU_H
#include <stdint.h>
#ifndef NES_GAIN
#define NES_GAIN 65536                          /* niveau calé sur libvgm (NSFPlay) : carrées à 1 %, triangle à 1 % */
#endif

/* mélangeur (NESdev « APU Mixer », approximation par tables), en 1/65535 de la pleine sortie :
 * NES_PULSE[n] = 95,52 / (8128 / n + 100) ; NES_TND[n] = 163,67 / (24329 / n + 100), n = 3 triangle + 2 bruit + DMC */
static const uint16_t NES_PULSE[31] = {
    0, 761, 1503, 2228, 2936, 3628, 4303, 4964, 5609, 6240, 6858, 7462,
    8053, 8632, 9198, 9753, 10296, 10828, 11350, 11861, 12362, 12853, 13334, 13807,
    14270, 14725, 15171, 15609, 16039, 16461, 16876,
};
static const uint16_t NES_TND[203] = {
    0, 439, 875, 1307, 1735, 2160, 2582, 3000, 3415, 3826, 4235, 4640,
    5042, 5441, 5836, 6229, 6619, 7005, 7389, 7770, 8148, 8523, 8895, 9264,
    9631, 9995, 10356, 10715, 11070, 11424, 11774, 12123, 12468, 12811, 13152, 13490,
    13826, 14159, 14490, 14819, 15145, 15469, 15791, 16110, 16428, 16743, 17056, 17366,
    17675, 17981, 18286, 18588, 18888, 19187, 19483, 19777, 20070, 20360, 20648, 20935,
    21220, 21502, 21783, 22062, 22340, 22615, 22889, 23161, 23431, 23699, 23966, 24231,
    24494, 24756, 25016, 25274, 25531, 25786, 26040, 26292, 26542, 26791, 27039, 27285,
    27529, 27772, 28013, 28253, 28492, 28729, 28964, 29198, 29431, 29663, 29893, 30122,
    30349, 30575, 30800, 31023, 31245, 31466, 31685, 31904, 32121, 32336, 32551, 32764,
    32976, 33187, 33397, 33605, 33813, 34019, 34224, 34428, 34630, 34832, 35032, 35232,
    35430, 35627, 35823, 36018, 36212, 36405, 36597, 36788, 36978, 37166, 37354, 37541,
    37727, 37912, 38095, 38278, 38460, 38641, 38821, 39000, 39178, 39355, 39531, 39707,
    39881, 40055, 40227, 40399, 40570, 40740, 40909, 41077, 41245, 41411, 41577, 41742,
    41906, 42069, 42232, 42394, 42554, 42714, 42874, 43032, 43190, 43347, 43503, 43658,
    43813, 43967, 44120, 44272, 44424, 44575, 44725, 44875, 45024, 45172, 45319, 45466,
    45612, 45757, 45902, 46046, 46189, 46331, 46473, 46615, 46755, 46895, 47035, 47173,
    47311, 47449, 47585, 47722, 47857, 47992, 48126, 48260, 48393, 48526, 48658,
};

static const uint8_t NES_LEN[32] = {10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
                                    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
static const uint8_t NES_DUTY[4] = {0x40, 0x60, 0x78, 0x9F};   /* 12,5 %, 25 %, 50 %, 25 % inversé (bit 7 : pas 0) */
static const uint16_t NES_NOISE[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
static const uint16_t NES_DMC[16] = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};
static const uint8_t NES_TRI[32] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                                    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

typedef struct {
    uint8_t on, duty, pos, halt, constant, vol, env_start, env_div, env_vol;
    uint8_t sw_en, sw_per, sw_neg, sw_shift, sw_div, sw_reload;
    uint16_t t, len;
    int32_t timer;
} nes_pulse_t;

typedef struct {
    uint32_t step_q16, acc;                     /* cycles du processeur par trame (Q16) */
    int32_t fc_timer;
    uint8_t fc_mode, fc_step;
    nes_pulse_t p[2];
    /* triangle */
    uint8_t tr_on, tr_ctrl, tr_lin_load, tr_lin, tr_reload, tr_pos; uint16_t tr_t, tr_len; int32_t tr_timer;
    /* bruit */
    uint8_t no_on, no_halt, no_const, no_vol, no_env_start, no_env_div, no_env_vol, no_mode; uint16_t no_per, no_len, no_lfsr; int32_t no_timer;
    /* DMC */
    const uint8_t *mem;
    uint8_t dm_on, dm_loop, dm_out, dm_buf, dm_buf_full, dm_shift, dm_bits, dm_silent;
    uint16_t dm_rate, dm_addr0, dm_addr, dm_len0, dm_left; int32_t dm_timer;
    int32_t hp_x, hp_y;                         /* passe-haut */
    uint8_t mute;                               /* ROMsounds (effets) : bits carré 1, carré 2, triangle, bruit, DMC */
} nes_t;

static void nes_init(nes_t *a, uint32_t clock, const uint8_t *mem)
{
    uint32_t i;
    for (i = 0; i < sizeof *a; i++) ((uint8_t *)a)[i] = 0;
    if (!clock) clock = 1789772u;
    a->step_q16 = (clock / 44100u) << 16 | (((clock % 44100u) << 16) / 44100u);
    a->fc_timer = 7457;
    a->no_lfsr = 1; a->no_per = 4;
    a->dm_rate = 428; a->dm_timer = 428; a->dm_silent = 1; a->dm_bits = 8;
    a->mem = mem;
    a->p[0].timer = a->p[1].timer = a->tr_timer = a->no_timer = 1;
    /* au démarrage, carrés, triangle et bruit sont actifs ($4015 = $0F) comme dans un lecteur NSF et dans libvgm
     * (NSFPlay, « unmute on reset ») : beaucoup de VGM NES n'écrivent jamais $4015 et seraient muets sans ça */
    a->p[0].on = a->p[1].on = a->tr_on = a->no_on = 1;
}

static uint32_t nes_sweep_target(const nes_pulse_t *p, uint32_t ch)
{
    uint32_t d = p->t >> p->sw_shift;
    if (!p->sw_neg) return p->t + d;
    return d + (ch == 0u ? 1u : 0u) > p->t ? 0u : p->t - d - (ch == 0u ? 1u : 0u);   /* la voie 1 : complément à un */
}

static void nes_write(nes_t *a, uint32_t r, uint32_t v)
{
    if (r < 8u) {                               /* carrés */
        nes_pulse_t *p = &a->p[r >> 2];
        switch (r & 3u) {
        case 0: p->duty = (uint8_t)(v >> 6); p->halt = (uint8_t)(v >> 5 & 1u); p->constant = (uint8_t)(v >> 4 & 1u); p->vol = (uint8_t)(v & 15u); break;
        case 1: p->sw_en = (uint8_t)(v >> 7); p->sw_per = (uint8_t)(v >> 4 & 7u); p->sw_neg = (uint8_t)(v >> 3 & 1u); p->sw_shift = (uint8_t)(v & 7u); p->sw_reload = 1; break;
        case 2: p->t = (uint16_t)((p->t & 0x700u) | v); break;
        default:
            p->t = (uint16_t)((p->t & 0xFFu) | (v & 7u) << 8);
            if (p->on) p->len = NES_LEN[v >> 3];
            p->pos = 0; p->env_start = 1;
            break;
        }
        return;
    }
    switch (r) {
    case 0x08: a->tr_ctrl = (uint8_t)(v >> 7); a->tr_lin_load = (uint8_t)(v & 0x7Fu); break;
    case 0x0A: a->tr_t = (uint16_t)((a->tr_t & 0x700u) | v); break;
    case 0x0B: a->tr_t = (uint16_t)((a->tr_t & 0xFFu) | (v & 7u) << 8); if (a->tr_on) a->tr_len = NES_LEN[v >> 3]; a->tr_reload = 1; break;
    case 0x0C: a->no_halt = (uint8_t)(v >> 5 & 1u); a->no_const = (uint8_t)(v >> 4 & 1u); a->no_vol = (uint8_t)(v & 15u); break;
    case 0x0E: a->no_mode = (uint8_t)(v >> 7); a->no_per = NES_NOISE[v & 15u]; break;
    case 0x0F: if (a->no_on) a->no_len = NES_LEN[v >> 3]; a->no_env_start = 1; break;
    case 0x10: a->dm_loop = (uint8_t)(v >> 6 & 1u); a->dm_rate = NES_DMC[v & 15u]; break;
    case 0x11: a->dm_out = (uint8_t)(v & 0x7Fu); break;
    case 0x12: a->dm_addr0 = (uint16_t)(0xC000u + v * 64u); break;
    case 0x13: a->dm_len0 = (uint16_t)(v * 16u + 1u); break;
    case 0x15:
        a->p[0].on = (uint8_t)(v & 1u); if (!a->p[0].on) a->p[0].len = 0;
        a->p[1].on = (uint8_t)(v >> 1 & 1u); if (!a->p[1].on) a->p[1].len = 0;
        a->tr_on = (uint8_t)(v >> 2 & 1u); if (!a->tr_on) a->tr_len = 0;
        a->no_on = (uint8_t)(v >> 3 & 1u); if (!a->no_on) a->no_len = 0;
        a->dm_on = (uint8_t)(v >> 4 & 1u);
        if (!a->dm_on) a->dm_left = 0;
        else if (!a->dm_left) { a->dm_addr = a->dm_addr0; a->dm_left = a->dm_len0; }
        break;
    case 0x17:
        a->fc_mode = (uint8_t)(v >> 7); a->fc_step = 0; a->fc_timer = 7457;
        if (a->fc_mode) { a->fc_step = 0; }      /* (5 pas : un quart et une demi-trame tout de suite, ci-dessous) */
        break;
    default: break;
    }
}

static void nes_env(uint8_t *start, uint8_t *div, uint8_t *vol, uint32_t per, uint32_t loop)
{
    if (*start) { *start = 0; *vol = 15; *div = (uint8_t)per; return; }
    if (*div) { (*div)--; return; }
    *div = (uint8_t)per;
    if (*vol) (*vol)--;
    else if (loop) *vol = 15;
}

static void nes_quarter(nes_t *a)
{
    uint32_t i;
    for (i = 0; i < 2u; i++) nes_env(&a->p[i].env_start, &a->p[i].env_div, &a->p[i].env_vol, a->p[i].vol, a->p[i].halt);
    nes_env(&a->no_env_start, &a->no_env_div, &a->no_env_vol, a->no_vol, a->no_halt);
    if (a->tr_reload) a->tr_lin = a->tr_lin_load;
    else if (a->tr_lin) a->tr_lin--;
    if (!a->tr_ctrl) a->tr_reload = 0;
}

static void nes_half(nes_t *a)
{
    uint32_t i;
    for (i = 0; i < 2u; i++) {
        nes_pulse_t *p = &a->p[i];
        if (!p->halt && p->len) p->len--;
        {
            uint32_t tg = nes_sweep_target(p, i);
            if (!p->sw_div && p->sw_en && p->sw_shift && p->t >= 8u && tg <= 0x7FFu) p->t = (uint16_t)tg;
            if (!p->sw_div || p->sw_reload) { p->sw_div = p->sw_per; p->sw_reload = 0; }
            else p->sw_div--;
        }
    }
    if (!a->tr_ctrl && a->tr_len) a->tr_len--;
    if (!a->no_halt && a->no_len) a->no_len--;
}

static void nes_frame(nes_t *a)                 /* compteur de trames (~240 Hz) */
{
    uint32_t s = a->fc_step;
    if (!a->fc_mode) {                          /* 4 pas : quarts 0-3, demis 1 et 3 */
        nes_quarter(a);
        if (s & 1u) nes_half(a);
        a->fc_step = (uint8_t)((s + 1u) & 3u);
    } else {                                    /* 5 pas : rien au pas 3 ; demis 1 et 4 */
        if (s != 3u) nes_quarter(a);
        if (s == 1u || s == 4u) nes_half(a);
        a->fc_step = (uint8_t)(s >= 4u ? 0u : s + 1u);
    }
}

static void nes_render(nes_t *a, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        int32_t cyc, pul = 0, tnd, x, y;
        uint32_t sq[2], tri, noi;
        a->acc += a->step_q16;
        cyc = (int32_t)(a->acc >> 16);
        a->acc &= 0xFFFFu;
        a->fc_timer -= cyc;
        while (a->fc_timer <= 0) { a->fc_timer += 7457; nes_frame(a); }
        for (k = 0; k < 2u; k++) {              /* carrés : un pas toutes les (t + 1) x 2 cycles */
            nes_pulse_t *p = &a->p[k];
            int32_t per = ((int32_t)p->t + 1) * 2;
            p->timer -= cyc;
            while (p->timer <= 0) { p->timer += per; p->pos = (uint8_t)((p->pos + 1u) & 7u); }
            sq[k] = 0;
            if (p->len && p->t >= 8u && nes_sweep_target(p, k) <= 0x7FFu && (NES_DUTY[p->duty] << p->pos & 0x80u))
                sq[k] = p->constant ? p->vol : p->env_vol;
            if (a->mute >> k & 1u) sq[k] = 0;
            pul += (int32_t)sq[k];
        }
        {                                       /* triangle : un pas toutes les (t + 1) cycles, s'il entend */
            int32_t per = (int32_t)a->tr_t + 1;
            if (a->tr_len && a->tr_lin && a->tr_t >= 2u) {
                a->tr_timer -= cyc;
                while (a->tr_timer <= 0) { a->tr_timer += per; a->tr_pos = (uint8_t)((a->tr_pos + 1u) & 31u); }
            }
            tri = NES_TRI[a->tr_pos];
        }
        {                                       /* bruit */
            a->no_timer -= cyc;
            while (a->no_timer <= 0) {
                uint32_t fb = (a->no_lfsr ^ (a->no_lfsr >> (a->no_mode ? 6u : 1u))) & 1u;
                a->no_timer += a->no_per;
                a->no_lfsr = (uint16_t)((a->no_lfsr >> 1) | fb << 14);
            }
            noi = a->no_len && !(a->no_lfsr & 1u) ? (a->no_const ? a->no_vol : a->no_env_vol) : 0u;
        }
        a->dm_timer -= cyc;                     /* DMC : un bit toutes les « rate » cycles */
        while (a->dm_timer <= 0) {
            a->dm_timer += a->dm_rate;
            if (!a->dm_silent) {
                if (a->dm_shift & 1u) { if (a->dm_out <= 125u) a->dm_out += 2; }
                else if (a->dm_out >= 2u) a->dm_out -= 2;
            }
            a->dm_shift >>= 1;
            if (!--a->dm_bits) {
                a->dm_bits = 8;
                if (a->dm_buf_full) { a->dm_shift = a->dm_buf; a->dm_buf_full = 0; a->dm_silent = 0; }
                else a->dm_silent = 1;
            }
            if (!a->dm_buf_full && a->dm_left) {   /* lecture de l'octet suivant */
                a->dm_buf = a->mem ? a->mem[a->dm_addr & 0x7FFFu] : 0u;
                a->dm_buf_full = 1;
                a->dm_addr = (uint16_t)(a->dm_addr == 0xFFFFu ? 0x8000u : a->dm_addr + 1u);
                if (!--a->dm_left && a->dm_loop) { a->dm_addr = a->dm_addr0; a->dm_left = a->dm_len0; }
            }
        }
        if (a->mute & 4u) tri = 0;
        if (a->mute & 8u) noi = 0;
        tnd = NES_TND[3u * tri + 2u * noi + (a->mute & 16u ? 0u : a->dm_out)];
        /* triangle, bruit et DMC x 0,92 face aux carrées : comme NSFPlay (formule exacte x 0,95, ici l'approximation) */
        x = (int32_t)((NES_PULSE[pul] + ((uint32_t)tnd * 235u >> 8)) * (uint32_t)(NES_GAIN >> 2) >> 14);
        y = x - a->hp_x + a->hp_y - (a->hp_y >> 9);   /* passe-haut ~14 Hz : retirer le continu */
        a->hp_x = x; a->hp_y = y;
        y >>= 1;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
    }
}
#endif
