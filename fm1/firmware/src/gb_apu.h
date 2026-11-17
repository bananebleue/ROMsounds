/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le son de la Game Boy (DMG), écrit pour ROMsounds d'après la documentation publique (Pan Docs).
 * 2 voies carrées (la 1 avec balayage de fréquence), 1 voie d'onde (32 échantillons de 4 bits), 1 bruit ; enveloppes,
 * compteurs de durée, séquenceur à 512 Hz, panoramique (NR51), volume général (NR50), interrupteur (NR52).
 * Rendu directement à 44,1 kHz (chaque voie avance de ~95 cycles par trame), nombres entiers seulement.
 *
 *   gb_init(&g, horloge)      4 194 304 Hz
 *   gb_write(&g, reg, val)    reg : 0x00-0x2F = 0xFF10-0xFF3F (commande VGM 0xB3)
 *   gb_render(&g, out, n)     ajoute n trames stéréo (int32, échelle Q15) */
#ifndef ROMSOUNDS_GB_APU_H
#define ROMSOUNDS_GB_APU_H
#include <stdint.h>
#ifndef GB_GAIN
#define GB_GAIN 48                              /* niveau calé sur libvgm (gb_mame) : à 2 % près */
#endif

typedef struct {
    uint8_t on, dac, duty, pos, vol, env_vol, env_dir, env_per, env_t, len_en, sweep_on;
    uint16_t freq, shadow, len;
    int32_t timer;                              /* cycles avant le prochain pas */
    uint8_t sw_per, sw_neg, sw_shift, sw_t;
    uint16_t lfsr;                              /* bruit */
    uint8_t shift, wide7, div;
} gb_ch_t;

typedef struct {
    uint32_t step_q16, acc;                     /* cycles par trame (Q16) */
    int32_t fs_timer;                           /* séquenceur : 8192 cycles par pas */
    uint8_t fs_step, power, nr50, nr51;
    uint8_t regs[0x30];
    uint8_t wave[32];                           /* 4 bits chacun */
    gb_ch_t c[4];
} gb_t;

static const uint8_t GB_DUTY[4] = {0x01, 0x81, 0x87, 0x7E};   /* 12,5 %, 25 %, 50 %, 75 % (bit 7 : premier pas) */
static const uint8_t GB_DIV[8] = {8, 16, 32, 48, 64, 80, 96, 112};

static void gb_init(gb_t *g, uint32_t clock)
{
    uint32_t i;
    for (i = 0; i < sizeof *g; i++) ((uint8_t *)g)[i] = 0;
    if (!clock) clock = 4194304u;
    g->step_q16 = (clock / 44100u) << 16 | (((clock % 44100u) << 16) / 44100u);
    g->fs_timer = 8192;
    g->power = 1;
    g->nr50 = 0x77; g->nr51 = 0xFF;
    for (i = 0; i < 4u; i++) g->c[i].timer = 1;
}

static int32_t gb_period(const gb_t *g, uint32_t ch)
{
    const gb_ch_t *c = &g->c[ch];
    if (ch < 2u) return (int32_t)(2048u - c->freq) * 4;    /* carré : 8 pas par période de l'onde */
    if (ch == 2u) return (int32_t)(2048u - c->freq) * 2;   /* onde : 32 pas */
    return (int32_t)GB_DIV[c->div] << c->shift;            /* bruit */
}

static uint32_t gb_sweep_calc(gb_ch_t *c)
{
    uint32_t d = c->shadow >> c->sw_shift;
    uint32_t n = c->sw_neg ? c->shadow - d : c->shadow + d;
    if (n > 2047u) c->on = 0;
    return n;
}

static void gb_trigger(gb_t *g, uint32_t ch)
{
    gb_ch_t *c = &g->c[ch];
    c->on = c->dac;
    if (!c->len) c->len = ch == 2u ? 256u : 64u;
    c->timer = gb_period(g, ch);
    c->env_vol = c->vol; c->env_t = c->env_per ? c->env_per : 8u;
    if (ch == 2u) c->pos = 0;
    if (ch == 3u) c->lfsr = 0x7FFFu;
    if (ch == 0u) {
        c->shadow = c->freq;
        c->sw_t = c->sw_per ? c->sw_per : 8u;
        c->sweep_on = c->sw_per || c->sw_shift;
        if (c->sw_shift) gb_sweep_calc(c);
    }
}

static void gb_write(gb_t *g, uint32_t r, uint32_t v)
{
    gb_ch_t *c;
    if (r >= 0x30u) return;
    if (r >= 0x20u) { g->wave[(r - 0x20u) * 2u] = (uint8_t)(v >> 4); g->wave[(r - 0x20u) * 2u + 1u] = (uint8_t)(v & 15u); return; }
    if (r == 0x16u) {                           /* NR52 : interrupteur général */
        g->power = (uint8_t)(v >> 7);
        if (!g->power) { uint32_t i; for (i = 0; i < 4u; i++) g->c[i].on = 0; }
        return;
    }
    if (!g->power) return;
    g->regs[r] = (uint8_t)v;
    if (r == 0x14u) { g->nr50 = (uint8_t)v; return; }
    if (r == 0x15u) { g->nr51 = (uint8_t)v; return; }
    if (r < 0x05u) c = &g->c[0];
    else if (r < 0x0Au) c = &g->c[1];
    else if (r < 0x0Fu) c = &g->c[2];
    else c = &g->c[3];
    switch (r) {
    case 0x00: c->sw_per = (uint8_t)(v >> 4 & 7u); c->sw_neg = (uint8_t)(v >> 3 & 1u); c->sw_shift = (uint8_t)(v & 7u); break;
    case 0x01: case 0x06: c->duty = (uint8_t)(v >> 6); c->len = (uint16_t)(64u - (v & 63u)); break;
    case 0x02: case 0x07: case 0x11:
        c->vol = (uint8_t)(v >> 4); c->env_dir = (uint8_t)(v >> 3 & 1u); c->env_per = (uint8_t)(v & 7u);
        c->dac = (v & 0xF8u) != 0u;
        if (!c->dac) c->on = 0;
        break;
    case 0x03: case 0x08: case 0x0D: c->freq = (uint16_t)((c->freq & 0x700u) | v); break;
    case 0x04: case 0x09: case 0x0E: case 0x13:
        if (r != 0x13u) c->freq = (uint16_t)((c->freq & 0xFFu) | (v & 7u) << 8);
        c->len_en = (uint8_t)(v >> 6 & 1u);
        if (v & 0x80u) gb_trigger(g, r == 0x04u ? 0u : r == 0x09u ? 1u : r == 0x0Eu ? 2u : 3u);
        break;
    case 0x0A: c->dac = (uint8_t)(v >> 7); if (!c->dac) c->on = 0; break;
    case 0x0B: c->len = (uint16_t)(256u - v); break;
    case 0x0C: c->vol = (uint8_t)(v >> 5 & 3u); break;              /* onde : 0 muet, 1 100 %, 2 50 %, 3 25 % */
    case 0x10: c->len = (uint16_t)(64u - (v & 63u)); break;
    case 0x12: c->shift = (uint8_t)(v >> 4); c->wide7 = (uint8_t)(v >> 3 & 1u); c->div = (uint8_t)(v & 7u); break;
    default: break;
    }
}

static void gb_frame_step(gb_t *g)              /* séquenceur : 512 Hz */
{
    uint32_t i, s = g->fs_step;
    g->fs_step = (uint8_t)((s + 1u) & 7u);
    if (!(s & 1u))                              /* durée : 256 Hz */
        for (i = 0; i < 4u; i++) {
            gb_ch_t *c = &g->c[i];
            if (c->len_en && c->len && !--c->len) c->on = 0;
        }
    if (s == 2u || s == 6u) {                   /* balayage : 128 Hz */
        gb_ch_t *c = &g->c[0];
        if (c->sw_t && !--c->sw_t) {
            c->sw_t = c->sw_per ? c->sw_per : 8u;
            if (c->sweep_on && c->sw_per) {
                uint32_t n = gb_sweep_calc(c);
                if (n <= 2047u && c->sw_shift) { c->shadow = (uint16_t)n; c->freq = (uint16_t)n; gb_sweep_calc(c); }
            }
        }
    }
    if (s == 7u)                                /* enveloppes : 64 Hz */
        for (i = 0; i < 4u; i++) {
            gb_ch_t *c = &g->c[i];
            if (i == 2u || !c->env_per) continue;
            if (!--c->env_t) {
                c->env_t = c->env_per;
                if (c->env_dir && c->env_vol < 15u) c->env_vol++;
                else if (!c->env_dir && c->env_vol) c->env_vol--;
            }
        }
}

static void gb_render(gb_t *g, int32_t *out, uint32_t n)
{
    uint32_t i, ch;
    for (i = 0; i < n; i++) {
        int32_t cyc, l = 0, r = 0, s[4];
        g->acc += g->step_q16;
        cyc = (int32_t)(g->acc >> 16);
        g->acc &= 0xFFFFu;
        g->fs_timer -= cyc;
        while (g->fs_timer <= 0) { g->fs_timer += 8192; gb_frame_step(g); }
        for (ch = 0; ch < 4u; ch++) {
            gb_ch_t *c = &g->c[ch];
            int32_t per = gb_period(g, ch);
            s[ch] = 0;
            if (!c->on || !g->power) continue;
            if (per < 8) per = 8;                 /* (fréquences inaudibles : pas de boucle sans fin) */
            c->timer -= cyc;
            while (c->timer <= 0) {
                c->timer += per;
                if (ch < 2u) c->pos = (uint8_t)((c->pos + 1u) & 7u);
                else if (ch == 2u) c->pos = (uint8_t)((c->pos + 1u) & 31u);
                else {
                    uint32_t x = (c->lfsr ^ (c->lfsr >> 1)) & 1u;
                    c->lfsr = (uint16_t)((c->lfsr >> 1) | x << 14);
                    if (c->wide7) c->lfsr = (uint16_t)((c->lfsr & ~0x40u) | x << 6);
                }
            }
            if (ch < 2u) s[ch] = GB_DUTY[c->duty] >> (7u - c->pos) & 1u ? (int32_t)c->env_vol : -(int32_t)c->env_vol;
            else if (ch == 2u) s[ch] = c->vol ? ((int32_t)g->wave[c->pos] * 2 - 15) >> (c->vol - 1u) : 0;
            else s[ch] = c->lfsr & 1u ? -(int32_t)c->env_vol : (int32_t)c->env_vol;
        }
        for (ch = 0; ch < 4u; ch++) {
            if (g->nr51 >> (4u + ch) & 1u) l += s[ch];
            if (g->nr51 >> ch & 1u) r += s[ch];
        }
        l *= (int32_t)(g->nr50 >> 4 & 7u) + 1; r *= (int32_t)(g->nr50 & 7u) + 1;
        l *= GB_GAIN; r *= GB_GAIN;
        out[2u * i] += l;
        out[2u * i + 1u] += r;
    }
}
#endif
