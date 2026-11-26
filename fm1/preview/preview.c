/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le lecteur du FM-1, pour la page (WebAssembly). Mêmes puces, même lecture des commandes, mêmes niveaux
 * que firmware/src/romsounds.c (render_block, sans les effets) : ce qu'on écoute dans la page est ce que jouera le FM-1.
 *
 *   pv_buf()           l'endroit où la page copie le morceau préparé (RSM1 à RSM7, RSMD : web/romsounds.js)
 *   pv_load(taille)    0, ou un code d'erreur (comme song_load)
 *   pv_render(n)       calcule n trames (n <= PV_BLOCK) dans pv_out() (int16 stéréo, 44,1 kHz) ; rend le nombre de
 *                      trames calculées (moins que n : fin du morceau)
 *   pv_played()        trames jouées (le temps du morceau, la boucle continue après la fin)
 *   pv_mute(masque)    voix coupées (même ordre que sur le FM-1 : FM 1-6, DAC, PSG 1-3, bruit ; Game Boy ; NES ; YM2610 : FM, ADPCM-A, ADPCM-B ; YM2151 ; YM2203 ; OKI ; OPL ; AY / SSG)
 *
 *   tools/build_preview.sh  ->  web/preview.wasm  (clang --target=wasm32, sans bibliothèque C) */
#include <stdint.h>
#include <string.h>
#include "../firmware/ext/fmopn/fmopn_shim.h"
#include "../firmware/ext/fmopn/fmopn.h"
#include "../firmware/ext/fmopl/fmopl.h"
#include "../firmware/ext/ym2151/ym2151.h"
#include "psg.h"
#include "gb_apu.h"
#include "nes_apu.h"
#include "ay_apu.h"
#include "oki.h"
#include "mod.h"
#include "vgm.h"
#include "../firmware/ext/uzlib/uzlib.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define VGM_MAX (268u * 1024u)
#define PV_BLOCK 4096u

/* ce qu'il faut de la bibliothèque C (fmopn.c, tinflate.c) */
void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *p = d; const uint8_t *q = s;
    if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n) { const uint8_t *p = a, *q = b; while (n--) { if (*p != *q) return *p - *q; p++; q++; } return 0; }
uint32_t uzlib_adler32(const void *data, unsigned int length, uint32_t prev_sum) { (void)data; (void)length; return prev_sum; }
uint32_t uzlib_crc32(const void *data, unsigned int length, uint32_t crc) { (void)data; (void)length; return crc; }

static uint8_t vgm_buf[VGM_MAX];
static uint8_t dict[32768];
static int16_t out[PV_BLOCK * 2u];
static const uint8_t *part_a, *part_b;
static uint32_t len_a, len_b, cur_part;
static TINF_DATA inf;
static uint8_t obuf[256];
static uint32_t obuf_r, obuf_n, inf_done, part_waited;
static vgm_t song;
static uint32_t wait, played, ended, loaded;
static void *ym;
static psg_t psg;
static gb_t gb;
static nes_t nes;
static ay_t ay;
static mod_t mod;
static uint32_t mute;

static void vgm_ym(uint32_t port, uint32_t reg, uint32_t val)
{
    if (!ym) return;
    ym2612_write(ym, (UINT8)(port * 2u), (UINT8)reg);
    ym2612_write(ym, (UINT8)(port * 2u + 1u), (UINT8)val);
}
static void vgm_psg(uint32_t val) { psg_write(&psg, val); }
static void vgm_psg_stereo(uint32_t val) { psg_stereo(&psg, val); }
static void vgm_gb(uint32_t reg, uint32_t val) { gb_write(&gb, reg, val); }
static void vgm_nes(uint32_t reg, uint32_t val) { nes_write(&nes, reg, val); }
static void vgm_ay(uint32_t reg, uint32_t val) { ay_write(&ay, reg, val); }
static void *opl, *opnb, *opm, *opn;
static oki_t oki;
static void vgm_opm(uint32_t reg, uint32_t val) { if (opm) ym2151_write_reg(opm, (UINT8)reg, (UINT8)val); }
static void vgm_opn(uint32_t reg, uint32_t val)
{
    if (!opn) return;
    if (reg < 0x10u) { ay_write(&ay, reg, val); return; }
    ym2203_write(opn, 0, (UINT8)reg); ym2203_write(opn, 1, (UINT8)val);
}
static void vgm_oki(uint32_t reg, uint32_t val) { oki_write(&oki, reg, val); }
static void vgm_opnb(uint32_t port, uint32_t reg, uint32_t val)
{
    if (!opnb) return;
    if (!port && reg < 0x10u) { ay_write(&ay, reg, val); return; }
    ym2610_write(opnb, (UINT8)(port * 2u), (UINT8)reg);
    ym2610_write(opnb, (UINT8)(port * 2u + 1u), (UINT8)val);
}
static void vgm_opl(uint32_t reg, uint32_t val)
{
    if (!opl) return;
    if (song.opl_type == 2u) { ym3526_write(opl, 0, (UINT8)reg); ym3526_write(opl, 1, (UINT8)val); }
    else { ym3812_write(opl, 0, (UINT8)reg); ym3812_write(opl, 1, (UINT8)val); }
}

static void part_open(uint32_t which)
{
    cur_part = which;
    inf.source = which ? part_b : part_a;
    inf.source_limit = inf.source + (which ? len_b : len_a);
    inf.source_read_cb = 0;
    uzlib_uncompress_init(&inf, dict, sizeof dict);
    obuf_r = obuf_n = 0; inf_done = 0; part_waited = 0;
}
static int vgm_get(void)
{
    int res;
    if (obuf_r < obuf_n) return obuf[obuf_r++];
    if (inf_done) return -1;
    inf.dest_start = inf.dest = obuf;
    inf.dest_limit = obuf + sizeof obuf;
    res = uzlib_uncompress(&inf);
    obuf_n = (uint32_t)(inf.dest - obuf);
    obuf_r = 0;
    if (res != TINF_OK) inf_done = 1;
    if (!obuf_n) return -1;
    return obuf[obuf_r++];
}
static int vgm_restart(void)
{
    if (!len_b || !song.loop_samples) return 0;
    if (cur_part == 1u && !part_waited) return 0;
    part_open(1);
    return 1;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t ym_step, ym_pos, mbuf_n;
static DEV_SMPL mbuf_l[640], mbuf_r[640];

EXPORT("pv_buf") uint8_t *pv_buf(void) { return vgm_buf; }
EXPORT("pv_out") int16_t *pv_out(void) { return out; }
EXPORT("pv_played") uint32_t pv_played(void) { return played; }
EXPORT("pv_total") uint32_t pv_total(void) { return song.total; }
EXPORT("pv_loop") uint32_t pv_loop(void) { return song.loop_samples; }

static void apply_mute(void)
{
    uint32_t n = 0;
    if (song.ym_clock) { ym2612_set_mute_mask(ym, mute & 0x7Fu); n = 7u; }
    else if (song.opnb_clock) {                 /* comme fx_apply_mute (firmware/src/romsounds.c) */
        uint32_t nf = song.opnb_type & 1u ? 6u : 4u, fm = nf == 6u ? mute & 0x3Fu : (mute & 3u) << 1 | (mute >> 2 & 3u) << 4;
        if (opnb) ym2610_set_mute_mask(opnb, fm | (mute >> nf & 0x7Fu) << 6);
        n = nf + 7u;
    }
    else if (song.opm_clock) { if (opm) ym2151_set_mute_mask(opm, mute & 0xFFu); n = 8u; }
    else if (song.opn_clock) { if (opn) ym2203_set_mute_mask(opn, mute & 7u); n = 3u; }
    psg.mute = (uint8_t)(song.psg_clock ? mute >> n & 15u : 0u); if (song.psg_clock) n += 4u;
    gb.mute = (uint8_t)(song.gb_clock ? mute >> n & 15u : 0u); if (song.gb_clock) n += 4u;
    nes.mute = (uint8_t)(song.nes_clock ? mute >> n & 31u : 0u); if (song.nes_clock) n += 5u;
    oki.mute = (uint8_t)(song.oki_clock ? mute >> n & 15u : 0u); if (song.oki_clock) n += 4u;
    if (song.opl_clock) {
        uint32_t mm = mute >> n;
        if (opl) opl_set_mute_mask(opl, (mm & 0x1FFu) | (mm >> 9 & 1u ? 0x3E00u : 0u));
        n += 10u;
    }
    ay.mute = (uint8_t)(song.ay_clock ? mute >> n & 15u : 0u);
    mod.mute = song.is_mod ? mute & 0xFFu : 0u;
}
EXPORT("pv_mute") void pv_mute(uint32_t m) { mute = m; if (loaded) apply_mute(); }

EXPORT("pv_load") int pv_load(uint32_t size)
{
    const uint8_t *m = vgm_buf;
    uint32_t pcm, a, b, hl, yc, rate;
    loaded = 0;
    song.is_mod = 0;
    if (size >= 64u && size <= VGM_MAX && m[0] == 'R' && m[1] == 'S' && m[2] == 'M' && m[3] == 'D') {   /* module Amiga */
        if ((rd32(m + 4) != 1u && rd32(m + 4) != 2u) || rd32(m + 8) + 64u != size
            || mod_load(&mod, m + 64, size - 64u, rd32(m + 4) == 2u ? rd32(m + 56) : 0u, rd32(m + 4) == 2u ? rd32(m + 60) : 0u)) return 4;
        song.is_mod = 1; song.total = rd32(m + 12); song.loop_samples = rd32(m + 16);
        played = 0; ended = 0; loaded = 1;
        apply_mute();
        return 0;
    }
    if (size < 40u || size > VGM_MAX || m[0] != 'R' || m[1] != 'S' || m[2] != 'M' || (m[3] < '1' || m[3] > '7')) return 4;
    hl = m[3] == '1' ? 40u : m[3] == '2' ? 48u : m[3] == '3' ? 56u : m[3] == '4' ? 64u : m[3] == '5' ? 72u : m[3] == '6' ? 88u : 104u;
    if (size < hl) return 4;
    pcm = rd32(m + 20); a = rd32(m + 24); b = rd32(m + 28);
    if (hl + pcm + a + b != size || !a) return 4;
    song.ym_clock = rd32(m + 4); song.psg_clock = rd32(m + 8);
    song.total = rd32(m + 12); song.loop_samples = rd32(m + 16);
    song.warn = rd32(m + 32); song.psg_cfg = rd32(m + 36);
    song.gb_clock = hl > 40u ? rd32(m + 40) : 0u;
    song.nes_clock = hl > 40u ? rd32(m + 44) : 0u;
    song.ay_clock = hl > 56u ? rd32(m + 56) : 0u;
    song.ay_type = hl > 56u ? rd32(m + 60) : 0u;
    song.opl_clock = hl > 64u ? rd32(m + 64) : 0u;
    song.opl_type = hl > 64u ? rd32(m + 68) : 0u;
    song.opnb_clock = hl > 72u ? rd32(m + 72) : 0u;
    song.opnb_type = hl > 72u ? rd32(m + 76) : 0u;
    song.adpcm_a_len = hl > 72u ? rd32(m + 80) : 0u;
    if (song.adpcm_a_len > pcm) return 4;
    song.opm_clock = hl > 88u ? rd32(m + 88) : 0u;
    song.opn_clock = hl > 88u ? rd32(m + 92) : 0u;
    song.oki_clock = hl > 88u ? rd32(m + 96) : 0u;
    if (song.opnb_clock && !song.ay_clock) { song.ay_clock = song.opnb_clock / 4u; song.ay_type = 0x10u; }
    if (song.opn_clock && !song.ay_clock) { song.ay_clock = song.opn_clock / 2u; song.ay_type = 0x10u; }
    song.pcm = m + hl; song.pcm_len = pcm;
    part_a = m + hl + pcm; len_a = a;
    part_b = part_a + a; len_b = b;
    if (!song.ym_clock && !song.psg_clock && !song.gb_clock && !song.nes_clock && !song.ay_clock && !song.opl_clock && !song.opnb_clock && !song.opm_clock && !song.opn_clock && !song.oki_clock) return 3;
    yc = song.ym_clock ? song.ym_clock : 7670453u;
    if (yc > 9000000u) yc = 9000000u;
    rate = yc / 144u;
    ym = 0; opnb = 0; opm = 0; opn = 0;
    if (song.opnb_clock) {                      /* comme song_start (firmware/src/romsounds.c) */
        uint32_t oc = song.opnb_clock > 9000000u ? 9000000u : song.opnb_clock;
        rate = oc / 144u;
        opnb = ym2610_init(NULL, oc, rate, NULL, NULL);
        ym2610_reset_chip(opnb);
        ym2610_set_roms(opnb, song.adpcm_a_len ? song.pcm : 0, song.adpcm_a_len,
                        song.pcm_len > song.adpcm_a_len ? song.pcm + song.adpcm_a_len : 0, song.pcm_len - song.adpcm_a_len);
    } else if (song.opm_clock && !song.ym_clock) {
        uint32_t oc = song.opm_clock > 4500000u ? 4500000u : song.opm_clock;
        rate = oc / 64u;
        opm = ym2151_init(oc, rate);
        ym2151_reset_chip(opm);
    } else if (song.opn_clock && !song.ym_clock) {
        uint32_t oc = song.opn_clock > 6000000u ? 6000000u : song.opn_clock;
        rate = oc / 72u;
        opn = ym2203_init(NULL, oc, rate, NULL, NULL);
        ym2203_reset_chip(opn);
    } else {
        ym = ym2612_init(NULL, yc, rate, NULL, NULL);
        ym2612_reset_chip(ym);
    }
    oki_init(&oki, song.oki_clock & 0x7FFFFFFFu, song.oki_clock >> 31, song.oki_clock ? song.pcm : 0, song.pcm_len);
    opl = 0;
    if (song.opl_clock && !song.ym_clock && !opnb && !opm && !opn) {     /* comme song_start (firmware/src/romsounds.c) */
        uint32_t oc = song.opl_clock > 4000000u ? 4000000u : song.opl_clock;
        rate = oc / 72u;
        if (song.opl_type == 2u) { opl = ym3526_init(oc, rate); if (opl) ym3526_reset_chip(opl); }
        else { opl = ym3812_init(oc, rate); if (opl) ym3812_reset_chip(opl); }
    }
    psg_init(&psg, song.psg_clock ? song.psg_clock : 3579545u, song.psg_cfg & 0xFFFFu, song.psg_cfg >> 16 & 0xFFu);
    gb_init(&gb, song.gb_clock);
    nes_init(&nes, song.nes_clock, !song.ym_clock && song.pcm_len >= 0x8000u ? song.pcm : 0);
    ay_init(&ay, song.ay_clock, (song.ay_type & 0xF0u) != 0u);
    ym_step = (rate << 16) / 44100u;
    ym_pos = 0; mbuf_n = 0; mbuf_l[0] = mbuf_r[0] = 0;
    song.pcm_pos = 0;
    part_open(0);
    wait = 0; played = 0; ended = 0; loaded = 1;
    apply_mute();
    return 0;
}

/* comme render_block (firmware/src/romsounds.c), tempo 100 %, hauteur 0, sans effets */
EXPORT("pv_render") uint32_t pv_render(uint32_t frames)
{
    static int32_t mix[PV_BLOCK * 2u], tmp[PV_BLOCK * 2u];
    uint32_t left = frames > PV_BLOCK ? PV_BLOCK : frames, o = 0, i, done;
    for (i = 0; i < 2u * left; i++) mix[i] = 0;
    if (song.is_mod && loaded) { mod_render(&mod, mix, left); played += left; o = left; left = 0; }
    while (left && loaded && !ended) {
        uint32_t n, k = 0, j, p = ym_pos, kk = 0;
        DEV_SMPL *bb[2] = {mbuf_l + 1, mbuf_r + 1};
        if (!wait && !(wait = vgm_next(&song))) { ended = 1; break; }
        part_waited = 1;
        n = wait < left ? wait : left;
        if (n > 256u) n = 256u;                 /* (mbuf : 256 trames au plus d'un coup, comme sur le FM-1) */
        for (j = 0; j < n; j++) { p += ym_step; k += p >> 16; p &= 0xFFFFu; }
        if (song.ym_clock || opl || opnb || opm || opn) {
            int32_t g = song.ym_clock ? 2 : opnb ? OPNB_GAIN : opm ? OPM_GAIN : opn ? OPN_GAIN : OPL_GAIN;
            mbuf_l[0] = mbuf_l[mbuf_n]; mbuf_r[0] = mbuf_r[mbuf_n];
            if (k) {
                if (song.ym_clock) ym2612_update_one(ym, k, bb);
                else if (opnb) { if (song.opnb_type & 1u) ym2610b_update_one(opnb, k, bb); else ym2610_update_one(opnb, k, bb); }
                else if (opm) ym2151_update_one(opm, k, bb);
                else if (opn) ym2203_update_one(opn, k, bb);
                else if (song.opl_type == 2u) ym3526_update_one(opl, k, bb);
                else ym3812_update_one(opl, k, bb);
            }
            mbuf_n = k;
            for (j = 0; j < n; j++) {
                ym_pos += ym_step; kk += ym_pos >> 16; ym_pos &= 0xFFFFu;
                mix[2u * (o + j)] += mbuf_l[kk] * g;
                mix[2u * (o + j) + 1u] += mbuf_r[kk] * g;
            }
        }
        if (song.psg_clock) {
            if (song.ym_clock) psg_render(&psg, mix + 2u * o, n);
            else {
                for (j = 0; j < 2u * n; j++) tmp[j] = 0;
                psg_render(&psg, tmp, n);
                for (j = 0; j < 2u * n; j++) mix[2u * o + j] += tmp[j] << 1;
            }
        }
        if (song.gb_clock) gb_render(&gb, mix + 2u * o, n);
        if (song.nes_clock) nes_render(&nes, mix + 2u * o, n);
        if (song.ay_clock) ay_render(&ay, mix + 2u * o, n);
        if (song.oki_clock) oki_render(&oki, mix + 2u * o, n);
        wait -= n; left -= n; o += n; played += n;
    }
    done = o;
    for (i = 0; i < 2u * done; i++) { int32_t v = mix[i]; out[i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }
    return done;
}
