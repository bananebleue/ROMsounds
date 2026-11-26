/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le lecteur du FM-1, pour la page (WebAssembly). Mêmes puces, même lecture des commandes, mêmes niveaux et
 * mêmes effets que firmware/src/romsounds.c (render_block, fx_enc, fx_controls) : ce qu'on écoute dans la page est ce
 * que jouera le FM-1, molettes et boutons de l'image compris. La page peut aussi y jouer un morceau trop gros pour le
 * FM-1 (jusqu'à PV_MAX), pour l'écouter avant de le réduire.
 *
 *   pv_buf()           l'endroit où la page copie le morceau préparé (RSM1 à RSM7, RSMD : web/romsounds.js)
 *   pv_load(taille)    0, ou un code d'erreur (comme song_load)
 *   pv_render(n)       calcule n trames (n <= PV_BLOCK) dans pv_out() (int16 stéréo, 44,1 kHz) ; rend le nombre de
 *                      trames calculées (moins que n : fin du morceau)
 *   pv_played()        trames jouées (le temps du morceau, la boucle continue après la fin)
 *   pv_enc(fn, crans)  une molette (FN_* de romsounds.c : 1 tempo, 2 hauteur, 3 filtre, 4 écho, 5 temps, 6 grain, 7 modulation)
 *   pv_btn(fn, appui)  un bouton (FB_* : 1-2 hauteur, 3 boucle, 4-5 beat roll, 6 vinyl brake, 7 backspin, 8 remise à zéro) ;
 *                      appui 1 : enfoncé, 0 : relâché
 *   pv_get(i), pv_set(i, v)   l'état des effets (PV_* plus bas : pour l'écran et les presets de la page)
 *   pv_voices()        le nombre de voix (les touches)
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
#include "k053260.h"
#include "mod.h"
#include "fx.h"
#include "vgm.h"
#include "../firmware/ext/uzlib/uzlib.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define VGM_MAX (268u * 1024u)                  /* la place dans le FM-1 (au-dessus de VGM_MAX - FX_BYTES : sans écho, comme lui) */
#define PV_MAX (16u * 1024u * 1024u)            /* ce que la page peut écouter */
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

static uint8_t vgm_buf[PV_MAX];
static int16_t fx_mem[FX_FRAMES * 2u];
static uint8_t dict[32768];
static int16_t out[PV_BLOCK * 2u];
static const uint8_t *part_a, *part_b;
static uint32_t len_a, len_b, cur_part;
static TINF_DATA inf;
static uint8_t obuf[256];
static uint32_t obuf_r, obuf_n, inf_done, part_waited;
static vgm_t song;
static uint32_t played, ended, loaded, song_size, played_frac;
static int32_t wait_q8;
static fx_t fx;
static uint32_t tempo_cur = 256, speed_q16 = 65536u, brake, braked, paused;
static int32_t pitch;
static uint32_t ym_step0, psg_step0, gb_step0, nes_step0, ay_step0, oki_step0, kdsc_step0;
static const uint32_t SEMI_Q16[25] = {32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858,
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072};
static uint32_t mul_q16(uint32_t a, uint32_t r) { return (a >> 16) * r + ((a & 0xFFFFu) * (r >> 2) >> 14); }
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
static kdsc_t kdsc;
static void vgm_opm(uint32_t reg, uint32_t val) { if (opm) ym2151_write_reg(opm, (UINT8)reg, (UINT8)val); }
static void vgm_opn(uint32_t reg, uint32_t val)
{
    if (!opn) return;
    if (reg < 0x10u) { ay_write(&ay, reg, val); return; }
    ym2203_write(opn, 0, (UINT8)reg); ym2203_write(opn, 1, (UINT8)val);
}
static void vgm_oki(uint32_t reg, uint32_t val) { oki_write(&oki, reg, val); }
static void vgm_kdsc(uint32_t reg, uint32_t val) { kdsc_write(&kdsc, reg, val); }
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
    oki.mute = (uint8_t)(song.oki_clock ? mute >> n & 15u : 0u);
    kdsc.mute = (uint8_t)(song.kdsc_clock ? mute >> n & 15u : 0u); if (song.oki_clock || song.kdsc_clock) n += 4u;
    if (song.opl_clock) {
        uint32_t mm = mute >> n;
        if (opl) opl_set_mute_mask(opl, (mm & 0x1FFu) | (mm >> 9 & 1u ? 0x3E00u : 0u));
        n += 10u;
    }
    ay.mute = (uint8_t)(song.ay_clock ? mute >> n & 15u : 0u);
    mod.mute = song.is_mod ? mute & 0xFFu : 0u;
}
EXPORT("pv_mute") void pv_mute(uint32_t m) { mute = m; if (loaded) apply_mute(); }

/* ---- effets : comme romsounds.c (fx_apply_rate, fx_enc, fx_controls) */
static uint32_t voices(void)
{
    uint32_t n = 0;
    if (song.is_mod) return mod.nch;
    if (song.ym_clock) n += 7u;
    else if (song.opnb_clock) n += (song.opnb_type & 1u ? 6u : 4u) + 7u;
    else if (song.opm_clock) n += 8u;
    else if (song.opn_clock) n += 3u;
    if (song.psg_clock) n += 4u;
    if (song.gb_clock) n += 4u;
    if (song.nes_clock) n += 5u;
    if (song.oki_clock || song.kdsc_clock) n += 4u;
    if (song.opl_clock) n += 10u;
    if (song.ay_clock) n += 4u;
    return n;
}
#define PITCH_MAX (song.ym_clock || song.opnb_clock || song.opn_clock ? 5 : song.opm_clock ? 3 : song.opl_clock ? 1 : 12)
static void fx_apply_rate(void)
{
    uint32_t r = mul_q16(SEMI_Q16[pitch + 12], speed_q16 > 65536u ? 65536u : speed_q16);
    ym_step = mul_q16(ym_step0, r);
    psg.step_q16 = mul_q16(psg_step0, r);
    gb.step_q16 = mul_q16(gb_step0, r);
    nes.step_q16 = mul_q16(nes_step0, r);
    ay.step_q16 = mul_q16(ay_step0, r);
    oki.step_q16 = mul_q16(oki_step0, r);
    kdsc.step_q16 = mul_q16(kdsc_step0, r);
    mod.rate_q16 = r;
}
static void start_common(void)                  /* (pv_load) : comme la fin de song_start */
{
    wait_q8 = 0; played = 0; played_frac = 0; ended = 0; loaded = 1;
    brake = 0; braked = 0; speed_q16 = 65536u; paused = 0;
    if (pitch > PITCH_MAX) pitch = PITCH_MAX;
    fx_apply_rate();
    apply_mute();
    fx_attach(&fx, song_size <= VGM_MAX - FX_BYTES ? fx_mem : 0);   /* (le FM-1 : la mémoire de l'écho au-dessus du morceau) */
}
static uint32_t beat_frames(void) { return 264600000u / (song.bpm100 ? song.bpm100 : 12000u); }
static uint32_t tempo_eff(void) { uint32_t t = tempo_cur * (speed_q16 >> 8) >> 8; return t ? t : 1u; }

EXPORT("pv_voices") uint32_t pv_voices(void) { return loaded ? voices() : 0u; }
EXPORT("pv_enc") void pv_enc(uint32_t fn, int32_t e)
{
    int32_t v;
    switch (fn) {
    case 1: v = (int32_t)tempo_cur + 13 * e; tempo_cur = (uint32_t)(v < 64 ? 64 : v > 512 ? 512 : v); break;
    case 2: v = pitch + e; pitch = v < -12 ? -12 : v > PITCH_MAX ? PITCH_MAX : v; fx_apply_rate(); break;
    case 3:
        v = fx.filt + e; v = v < -20 ? -20 : v > 20 ? 20 : v;
        if (!fx.filt || (fx.filt < 0) != (v < 0)) { fx.lo[0] = fx.lo[1] = fx.bd[0] = fx.bd[1] = 0; }
        fx.filt = (int8_t)v; break;
    case 4: v = fx.echo + e; fx.echo = (uint8_t)(v < 0 ? 0 : v > 10 ? 10 : v); break;
    case 5: v = fx.time + e; fx.time = (uint8_t)(v < 1 ? 1 : v > 11 ? 11 : v); break;
    case 6: v = fx.grain + e; fx.grain = (uint8_t)(v < 0 ? 0 : v > 12 ? 12 : v); break;
    case 7: v = fx.mod + e; fx.mod = (int8_t)(v < -10 ? -10 : v > 10 ? 10 : v); break;
    default: break;
    }
}
EXPORT("pv_btn") void pv_btn(uint32_t fn, uint32_t down)
{
    switch (fn) {
    case 1: case 2: if (down) pv_enc(2, fn == 2u ? 1 : -1); break;
    case 3: if (down && loaded) fx_loop(&fx, fx_len(&fx), 0, 0); if (!down) fx.loop = 0; break;
    case 4: case 5:
        if (down && loaded) {
            uint32_t L = beat_frames() >> (fn == 4u ? 1 : 0), ph = song.beat0 % L, t = played, back, te = tempo_eff();
            back = t >= ph ? (t - ph) % L : 0u;
            fx_loop(&fx, L * 256u / te, back * 256u / te, 1);
        }
        if (!down) fx.loop = 0;
        break;
    case 6:
        if (down && loaded) {
            if (braked) { braked = 0; brake = 0; speed_q16 = 65536u; fx_apply_rate(); }
            else if (!paused) brake = 1;
        }
        break;
    case 7: if (down && loaded) fx_backspin(&fx); break;
    case 8: if (down) { tempo_cur = 256; pitch = 0; mute = 0; fx_reset(&fx); braked = 0; brake = 0; speed_q16 = 65536u; fx_apply_rate(); if (loaded) apply_mute(); } break;
    default: break;
    }
}
/* l'état : 0 tempo (Q8), 1 hauteur, 2 filtre, 3 écho, 4 temps, 5 grain, 6 modulation, 7 voix coupées, 8 freinage (1 : ralentit,
 * 2 : arrêtée), 9 boucle en cours, 10 backspin en cours, 11 écho / boucle possibles (morceau assez petit), 12 BPM x 100, 13 pause */
EXPORT("pv_get") int32_t pv_get(uint32_t i)
{
    switch (i) {
    case 0: return (int32_t)tempo_cur; case 1: return pitch; case 2: return fx.filt; case 3: return fx.echo; case 4: return fx.time;
    case 5: return fx.grain; case 6: return fx.mod; case 7: return (int32_t)mute; case 8: return braked ? 2 : brake ? 1 : 0;
    case 9: return fx.loop; case 10: return fx.spin != 0u; case 11: return fx.buf != 0; case 12: return (int32_t)song.bpm100;
    case 13: return (int32_t)paused; default: return 0;
    }
}
EXPORT("pv_set") void pv_set(uint32_t i, int32_t v)
{
    switch (i) {
    case 0: tempo_cur = (uint32_t)(v < 64 ? 64 : v > 512 ? 512 : v); break;
    case 1: pitch = v < -12 ? -12 : v > PITCH_MAX ? PITCH_MAX : v; fx_apply_rate(); break;
    case 2: if ((fx.filt < 0) != (v < 0) || !fx.filt) { fx.lo[0] = fx.lo[1] = fx.bd[0] = fx.bd[1] = 0; } fx.filt = (int8_t)(v < -20 ? -20 : v > 20 ? 20 : v); break;
    case 3: fx.echo = (uint8_t)(v < 0 ? 0 : v > 10 ? 10 : v); break;
    case 4: fx.time = (uint8_t)(v < 1 ? 1 : v > 11 ? 11 : v); break;
    case 5: fx.grain = (uint8_t)(v < 0 ? 0 : v > 12 ? 12 : v); break;
    case 6: fx.mod = (int8_t)(v < -10 ? -10 : v > 10 ? 10 : v); break;
    case 7: mute = (uint32_t)v; if (loaded) apply_mute(); break;
    case 13: paused = v != 0; if (!paused && braked) { braked = 0; brake = 0; speed_q16 = 65536u; fx_apply_rate(); } break;   /* (PLAY relance aussi la platine) */
    default: break;
    }
}

EXPORT("pv_load") int pv_load(uint32_t size)
{
    const uint8_t *m = vgm_buf;
    uint32_t pcm, a, b, hl, yc, rate;
    loaded = 0;
    song.is_mod = 0;
    if (size >= 64u && size <= PV_MAX && m[0] == 'R' && m[1] == 'S' && m[2] == 'M' && m[3] == 'D') {   /* module Amiga */
        if ((rd32(m + 4) != 1u && rd32(m + 4) != 2u) || rd32(m + 8) + 64u != size
            || mod_load(&mod, m + 64, size - 64u, rd32(m + 4) == 2u ? rd32(m + 56) : 0u, rd32(m + 4) == 2u ? rd32(m + 60) : 0u)) return 4;
        song.is_mod = 1; song.total = rd32(m + 12); song.loop_samples = rd32(m + 16);
        song.bpm100 = rd32(m + 48); song.beat0 = rd32(m + 52);
        song_size = size;
        start_common();
        return 0;
    }
    if (size < 40u || size > PV_MAX || m[0] != 'R' || m[1] != 'S' || m[2] != 'M' || (m[3] < '1' || m[3] > '7')) return 4;
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
    song.kdsc_clock = hl > 88u && !song.oki_clock ? rd32(m + 100) : 0u;
    song.kdsc_sh = song.kdsc_clock ? rd32(m + 84) : 0u;   /* (échantillons réduits par la page : 1 point sur 2^sh) */
    song.bpm100 = hl > 48u ? rd32(m + 48) : 0u;
    song.beat0 = hl > 48u ? rd32(m + 52) : 0u;
    if (song.bpm100 && (song.bpm100 < 3000u || song.bpm100 > 40000u)) song.bpm100 = 0;
    song_size = size;
    if (song.opnb_clock && !song.ay_clock) { song.ay_clock = song.opnb_clock / 4u; song.ay_type = 0x10u; }
    if (song.opn_clock && !song.ay_clock) { song.ay_clock = song.opn_clock / 2u; song.ay_type = 0x10u; }
    song.pcm = m + hl; song.pcm_len = pcm;
    part_a = m + hl + pcm; len_a = a;
    part_b = part_a + a; len_b = b;
    if (!song.ym_clock && !song.psg_clock && !song.gb_clock && !song.nes_clock && !song.ay_clock && !song.opl_clock && !song.opnb_clock && !song.opm_clock && !song.opn_clock && !song.oki_clock && !song.kdsc_clock) return 3;
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
    kdsc_init(&kdsc, song.kdsc_clock, song.kdsc_clock ? song.pcm : 0, song.pcm_len, song.kdsc_sh);
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
    ym_step0 = (rate << 16) / 44100u;
    ym_pos = 0; mbuf_n = 0; mbuf_l[0] = mbuf_r[0] = 0;
    song.pcm_pos = 0;
    part_open(0);
    psg_step0 = psg.step_q16; gb_step0 = gb.step_q16; nes_step0 = nes.step_q16; ay_step0 = ay.step_q16;
    oki_step0 = oki.step_q16; kdsc_step0 = kdsc.step_q16;
    start_common();
    return 0;
}

/* comme render_block (firmware/src/romsounds.c) : 256 trames à la fois, tempo, hauteur, vinyl brake, effets */
#define BLOCK 256u
static void render_block(int32_t *mix)
{
    static int32_t tmp[BLOCK * 2u];
    uint32_t left = BLOCK, o = 0, i, tempo = tempo_cur * (speed_q16 >> 8) >> 8, playing = loaded && !ended && !paused && !braked;
    if (!tempo) tempo = 1;
    for (i = 0; i < BLOCK * 2u; i++) mix[i] = 0;
    if (brake && playing) {
        speed_q16 = speed_q16 > 317u + 1500u ? speed_q16 - 317u : 0u;
        if (!speed_q16) { playing = 0; brake = 0; braked = 1; speed_q16 = 1500u; }
        fx_apply_rate();
    }
    if (song.is_mod && playing) {
        mod.tempo_q8 = tempo;
        mod_render(&mod, mix, BLOCK);
        played_frac += BLOCK * tempo; played += played_frac >> 8; played_frac &= 255u;
        left = 0;
    }
    while (left && playing) {
        uint32_t n, k = 0, j, p = ym_pos, kk = 0;
        DEV_SMPL *bb[2] = {mbuf_l + 1, mbuf_r + 1};
        if (wait_q8 <= 0) {
            uint32_t wt = vgm_next(&song);
            if (!wt) { playing = 0; ended = 1; break; }
            wait_q8 += (int32_t)(wt << 8);
            continue;
        }
        part_waited = 1;
        n = ((uint32_t)wait_q8 + tempo - 1u) / tempo;
        if (n > left) n = left;
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
        if (song.kdsc_clock) kdsc_render(&kdsc, mix + 2u * o, n);
        wait_q8 -= (int32_t)(n * tempo);
        played_frac += n * tempo; played += played_frac >> 8; played_frac &= 255u;
        left -= n; o += n;
    }
    if (loaded) fx_process(&fx, mix, BLOCK);
}
/* n trames (multiple de 256, n <= PV_BLOCK) dans pv_out() ; rend n, ou 0 quand le morceau est fini (sans écho qui traîne) */
EXPORT("pv_render") uint32_t pv_render(uint32_t frames)
{
    static int32_t mix[BLOCK * 2u];
    uint32_t n = frames > PV_BLOCK ? PV_BLOCK : frames, o, i;
    if (!loaded || ended) return 0;
    for (o = 0; o + BLOCK <= n; o += BLOCK) {
        render_block(mix);
        for (i = 0; i < BLOCK * 2u; i++) { int32_t v = mix[i]; out[2u * o + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }
    }
    return o;
}
