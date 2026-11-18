/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : lecteur de musiques Mega Drive (fichiers VGM) sur le M-VAVE FM-1.
 *
 *   YM2612   fmopn.c (MAME / Genesis Plus GX 1.5.1 via libvgm, GPL-2.0-or-later, firmware/ext/fmopn), compilé à part en
 *            -O2 ; ~60 % du temps disponible sur le FM-1 (mesuré le 17/11/2026, 6 voies)
 *   SN76489  psg.h (ROMsounds) ; Game Boy : gb_apu.h ; NES : nes_apu.h (ROMsounds, d'après les documentations publiques)
 *   VGM      vgm.h (ROMsounds) : YM2612 + DAC (banque PCM, commandes 0x8n / 0xE0) + PSG + Game Boy + NES, boucle
 *   inflate  uzlib (zlib, firmware/ext/uzlib) : les commandes restent compressées en mémoire, décompressées en jouant
 *
 *   PLAY     lecture / pause (à la fin : recommence)
 *   REC      revenir au début
 *   écran    titre, jeu, temps, charge du calcul, coupures
 *
 * Le morceau arrive de la page (web/index.html, préparé par web/romsounds.js) par SysEx, dans la RAM (ARENA) :
 *   F0 7D 52 53 01 <taille : 5 x 7 bits> <titre ASCII> 00 <jeu ASCII> F7      début
 *   F0 7D 52 53 02 <n° : 2 x 7 bits> <512 octets, empaquetés 8 -> 7 bits> F7  données (à n° x 512)
 *   F0 7D 52 53 03 <CRC-32 : 5 x 7 bits> F7                                    fin : vérifier, jouer
 * Le contenu (« RSM2 ») : en-tête de 48 octets (« RSM1 » : 40, sans les horloges Game Boy et NES), banque PCM brute
 * (pour la NES : l'image de sa mémoire $8000-$FFFF, que lit le DMC), commandes avant la boucle (A) et depuis la boucle (B),
 * en deflate brut. À la fin de B, on reprend B : une boucle sans retour en arrière.
 * Le FM-1 répond à chacun par un contrôleur sur le canal 16 : CC 117 = 4 / 3 (début / fin acceptés ; au début, 4 dit
 * « format RSM2 », les versions d'avant répondaient 2 (RSM1) ou 1), 0x40 + code
 * (refus) ; CC 118 = n° & 127 (données reçues). La page attend la réponse avant d'envoyer la suite.
 *
 * Le calcul se fait dans la boucle principale (pas dans l'interruption audio) : un tampon circulaire entre les deux.
 * Règles de la puce (pas de FPU, pas de division 64 bits) : nombres entiers seulement. */
#include <stdint.h>
static void app_idle(void);
static void app_sysex(const uint8_t *p, uint32_t n);
#define LCD_IDLE app_idle
#define APP_SYSEX app_sysex
#define APP_NAME "ROMSOUNDS"                    /* ROMsounds (base Dinghy 0.5) */
#include "platform.c"
#include "../ext/fmopn/fmopn.h"                 /* compilé à part, en -O2 (tools/build.py, tests/run_host.sh) */
#include "../ext/fmopl/fmopl.h"                 /* (de même) OPL2 / OPL : AdLib, Sound Blaster */
#include "psg.h"
#include "gb_apu.h"
#include "nes_apu.h"
#include "ay_apu.h"
#include "mod.h"
#include "fx.h"
#include "vgm.h"
#include "rx.h"
#include "../ext/uzlib/uzlib.h"                 /* compilé à part, en -O2 */
#ifdef DINGHY_HOST                              /* sur PC (tests/host.c) : le compteur 24 MHz vient de l'horloge du PC */
#include <time.h>
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 24000000u + (uint64_t)t.tv_nsec * 24u / 1000u);
}
#endif

/* ---- tampon circulaire : la boucle principale écrit, l'interruption audio lit */
#define RING 4096u                              /* trames (puissance de 2) : ~93 ms */
#define BLOCK 256u
static int16_t ring[RING * 2u];
static volatile uint32_t ring_w, ring_r;        /* compteurs de trames, un seul écrivain chacun */
static volatile uint32_t underruns;             /* blocs audio servis incomplets pendant la lecture */
static volatile uint32_t playing, primed;     /* primed : le tampon a été rempli une fois depuis le départ */

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t r = ring_r, avail = ring_w - r, i, m = avail < n ? avail : n;
    for (i = 0; i < m; i++, r++) {
        out[2u * i] += ring[(r & (RING - 1u)) * 2u];
        out[2u * i + 1u] += ring[(r & (RING - 1u)) * 2u + 1u];
    }
    ring_r = r;
    if (m < n && playing && primed) underruns++;
}

/* ---- le morceau : RSM1 en mémoire, compressé */
#define VGM_MAX (268u * 1024u)                  /* = VGM_MAX de web/romsounds.js ; + le dictionnaire, + l'écran : le pool */
static uint8_t vgm_buf[VGM_MAX] __attribute__((section(".pool"), aligned(4)));   /* (le haut sert aussi à l'écho) */
static uint8_t dict[32768] __attribute__((section(".pool")));   /* fenêtre de la décompression */
static const uint8_t *part_a, *part_b;
static uint32_t len_a, len_b, cur_part;
static TINF_DATA inf;
static uint8_t obuf[256];
static uint32_t obuf_r, obuf_n, inf_done, part_waited;   /* part_waited : la partie en cours a déjà fait attendre */
static vgm_t song;
static char title[32], game[32], sys[24];
static uint32_t loaded, ended, played;          /* played : trames du morceau jouées (44,1 kHz) */
static int32_t wait_q8;                         /* temps du morceau avant la commande suivante (trames, Q8) */
static uint32_t played_frac;
static uint32_t song_size;                      /* taille du morceau dans vgm_buf (au-dessus : la mémoire de l'écho) */

/* ---- effets en direct : tempo (sans changer la hauteur), hauteur (sans changer le tempo), voix coupées, fx.h */
static fx_t fx;
static uint32_t tempo_cur = 256;                /* Q8 : 256 = 100 % (64 .. 512) */
static int32_t pitch;                           /* demi-tons, -12 .. +12 (Mega Drive : +5 au plus, le YM2612 coûte) */
static uint32_t mute;                           /* bit = voix coupée (ordre : FM 1-6, DAC, PSG 1-3, bruit ; GB ; NES) */
static uint32_t ym_step0, psg_step0, gb_step0, nes_step0, ay_step0;
static uint32_t speed_q16 = 65536u;             /* vinyl brake : la platine ralentit (tempo et hauteur ensemble) */
static uint32_t brake, braked;                  /* brake : ralentit ; braked : arrêtée (PLAY ou le bouton : relancer) */
static int32_t track_turn;                      /* morceau suivant / précédent demandé (molette SELECT, boutons) */
static uint32_t track_turn_t;
static const uint32_t SEMI_Q16[25] = {32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858,
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072};
static uint32_t mul_q16(uint32_t a, uint32_t r)  /* a x r / 65536, r <= 2 x 65536, sans débordement */
{
    return (a >> 16) * r + ((a & 0xFFFFu) * (r >> 2) >> 14);
}
static void *ym;
static psg_t psg;
static gb_t gb;
static nes_t nes;
static ay_t ay;
static mod_t mod;
static void *opl;

static void vgm_ym(uint32_t port, uint32_t reg, uint32_t val)
{
    ym2612_write(ym, (UINT8)(port * 2u), (UINT8)reg);
    ym2612_write(ym, (UINT8)(port * 2u + 1u), (UINT8)val);
}
static void vgm_psg(uint32_t val) { psg_write(&psg, val); }
static void vgm_psg_stereo(uint32_t val) { psg_stereo(&psg, val); }
static void vgm_gb(uint32_t reg, uint32_t val) { gb_write(&gb, reg, val); }
static void vgm_nes(uint32_t reg, uint32_t val) { nes_write(&nes, reg, val); }
static void vgm_ay(uint32_t reg, uint32_t val) { ay_write(&ay, reg, val); }
static void vgm_opl(uint32_t reg, uint32_t val)
{
    if (!opl) return;
    if (song.opl_type == 2u) { ym3526_write(opl, 0, (UINT8)reg); ym3526_write(opl, 1, (UINT8)val); }
    else { ym3812_write(opl, 0, (UINT8)reg); ym3812_write(opl, 1, (UINT8)val); }
}

static void part_open(uint32_t which)           /* 0 : A (avant la boucle), 1 : B (la boucle) */
{
    cur_part = which;
    inf.source = which ? part_b : part_a;
    inf.source_limit = inf.source + (which ? len_b : len_a);
    inf.source_read_cb = 0;
    uzlib_uncompress_init(&inf, dict, sizeof dict);
    obuf_r = obuf_n = 0;
    inf_done = 0;
    part_waited = 0;
}
static int vgm_get(void)
{
    if (obuf_r < obuf_n) return obuf[obuf_r++];
    if (inf_done) return -1;
    {
        int res;
        inf.dest_start = inf.dest = obuf;
        inf.dest_limit = obuf + sizeof obuf;
        res = uzlib_uncompress(&inf);
        obuf_n = (uint32_t)(inf.dest - obuf);
        obuf_r = 0;
        if (res != TINF_OK) inf_done = 1;   /* fin de la partie (ou données abîmées) */
        if (!obuf_n) return -1;
    }
    return obuf[obuf_r++];
}
static int vgm_restart(void)
{
    if (!len_b || !song.loop_samples) return 0;  /* pas de boucle : fin */
    if (cur_part == 1u && !part_waited) return 0;   /* une boucle qui ne dure rien (ou abîmée) : on s'arrête */
    part_open(1);
    return 1;
}

/* uzlib : les sommes de contrôle de gzip / zlib ne servent pas (deflate brut, CRC-32 du tout vérifié à la réception) */
uint32_t uzlib_adler32(const void *data, unsigned int length, uint32_t prev_sum) { (void)data; (void)length; return prev_sum; }
uint32_t uzlib_crc32(const void *data, unsigned int length, uint32_t crc) { (void)data; (void)length; return crc; }

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int song_load(uint32_t size)             /* RSM1 reçu : vérifier, préparer ; 0 ou un code d'erreur */
{
    const uint8_t *m = vgm_buf;
    uint32_t pcm, a, b;
    uint32_t hl;
    song.is_mod = 0;
    if (size >= 64u && m[0] == 'R' && m[1] == 'S' && m[2] == 'M' && m[3] == 'D') {   /* RSMD : un module Amiga */
        uint32_t i;
        for (i = 0; i < sizeof song; i++) ((uint8_t *)&song)[i] = 0;
        if (rd32(m + 4) != 1u || rd32(m + 8) + 64u != size || mod_load(&mod, m + 64, size - 64u)) return 4;
        song.is_mod = 1; song.total = rd32(m + 12); song.loop_samples = rd32(m + 16);
        song.bpm100 = rd32(m + 48); song.beat0 = rd32(m + 52);
        song_size = size;
        return 0;
    }
    if (size < 40u || m[0] != 'R' || m[1] != 'S' || m[2] != 'M' || (m[3] < '1' || m[3] > '5')) return 4;
    hl = m[3] == '1' ? 40u : m[3] == '2' ? 48u : m[3] == '3' ? 56u : m[3] == '4' ? 64u : 72u;   /* RSM1 : 40 ; RSM2 (+ Game Boy,
                                                 * NES) : 48 ; RSM3 (+ tempo) : 56 ; RSM4 (+ AY / YM2149) : 64 ; RSM5 (+ OPL) : 72 */
    if (size < hl) return 4;
    pcm = rd32(m + 20); a = rd32(m + 24); b = rd32(m + 28);
    if (hl + pcm + a + b != size || !a) return 4;
    song.ym_clock = rd32(m + 4); song.psg_clock = rd32(m + 8);
    song.total = rd32(m + 12); song.loop_samples = rd32(m + 16);
    song.warn = rd32(m + 32);
    song.psg_cfg = rd32(m + 36);
    song.gb_clock = hl > 40u ? rd32(m + 40) : 0u;
    song.nes_clock = hl > 40u ? rd32(m + 44) : 0u;
    song.bpm100 = hl > 48u ? rd32(m + 48) : 0u;
    song.beat0 = hl > 48u ? rd32(m + 52) : 0u;
    song.ay_clock = hl > 56u ? rd32(m + 56) : 0u;
    song.ay_type = hl > 56u ? rd32(m + 60) : 0u;
    song.opl_clock = hl > 64u ? rd32(m + 64) : 0u;
    song.opl_type = hl > 64u ? rd32(m + 68) : 0u;
    if (song.bpm100 && (song.bpm100 < 3000u || song.bpm100 > 40000u)) song.bpm100 = 0;   /* (30 .. 400 BPM) */
    song.pcm = m + hl; song.pcm_len = pcm;
    part_a = m + hl + pcm; len_a = a;
    part_b = part_a + a; len_b = b;
    if (!song.ym_clock && !song.psg_clock && !song.gb_clock && !song.nes_clock && !song.ay_clock && !song.opl_clock) return 3;
    song_size = size;
    return 0;
}

/* le YM2612 calcule à sa propre fréquence (horloge / 144 : 53 267 Hz en NTSC), ramenée à 44,1 kHz */
static uint32_t ym_step, ym_pos;                /* Q16 : échantillons de la puce par trame */
static DEV_SMPL mbuf_l[640], mbuf_r[640];       /* [0] : le dernier échantillon du lot précédent (hauteur +5 : ~420) */
static uint32_t mbuf_n;
#define PITCH_MAX (song.ym_clock ? 5 : song.opl_clock ? 1 : 12)   /* demi-tons vers le haut : le YM2612 et l'OPL coûtent
                                                 * (OPL : ~1,3 fois le YM2612, mesuré sur PC) */
#define OPL_GAIN_OR_2 (song.ym_clock ? 2 : OPL_GAIN)

static uint32_t voices(uint32_t *ym_at, uint32_t *psg_at, uint32_t *gb_at, uint32_t *nes_at)
{
    uint32_t n = 0;
    if (song.is_mod) { *ym_at = *psg_at = *gb_at = *nes_at = 0; return mod.nch; }   /* module : ses voies */
    *ym_at = n; if (song.ym_clock) n += 7u;     /* FM 1-6, DAC */
    *psg_at = n; if (song.psg_clock) n += 4u;
    *gb_at = n; if (song.gb_clock) n += 4u;
    *nes_at = n; if (song.nes_clock) n += 5u;
    if (song.opl_clock) n += 10u;               /* OPL : 9 voies + les percussions (avant l'AY) */
    if (song.ay_clock) n += 4u;                 /* AY / YM2149 : voies A, B, C, bruit (les 4 dernières) */
    return n;
}
static void fx_apply_mute(void)
{
    uint32_t y, p, g, a, tot = voices(&y, &p, &g, &a);
    ay.mute = (uint8_t)(song.ay_clock ? mute >> (tot - 4u) & 15u : 0u);
    mod.mute = song.is_mod ? mute & 0xFFu : 0u;
    if (opl && song.opl_clock) {                /* 9 voies, puis les 5 percussions ensemble */
        uint32_t at = tot - (song.ay_clock ? 4u : 0u) - 10u, mm = mute >> at;
        opl_set_mute_mask(opl, (mm & 0x1FFu) | (mm >> 9 & 1u ? 0x3E00u : 0u));
    }
    if (ym) ym2612_set_mute_mask(ym, song.ym_clock ? mute >> y & 0x7Fu : 0u);
    psg.mute = (uint8_t)(song.psg_clock ? mute >> p & 15u : 0u);
    gb.mute = (uint8_t)(song.gb_clock ? mute >> g & 15u : 0u);
    nes.mute = (uint8_t)(song.nes_clock ? mute >> a & 31u : 0u);
}
static void fx_apply_rate(void)                  /* la hauteur : les puces vont plus ou moins vite que leur horloge */
{
    uint32_t r = mul_q16(SEMI_Q16[pitch + 12], speed_q16 > 65536u ? 65536u : speed_q16);
    ym_step = mul_q16(ym_step0, r);
    psg.step_q16 = mul_q16(psg_step0, r);
    gb.step_q16 = mul_q16(gb_step0, r);
    nes.step_q16 = mul_q16(nes_step0, r);
    ay.step_q16 = mul_q16(ay_step0, r);
    mod.rate_q16 = r;
}

static void song_start(void)
{
    uint32_t yc = song.ym_clock ? song.ym_clock : 7670453u, rate;
    if (yc > 9000000u) yc = 9000000u;
    rate = yc / 144u;
    ym = ym2612_init(NULL, yc, rate, NULL, NULL);
    ym2612_reset_chip(ym);
    opl = 0;
    if (song.opl_clock && !song.ym_clock) {     /* OPL2 / OPL : à sa fréquence native (horloge / 72), comme le YM2612 */
        uint32_t oc = song.opl_clock > 4000000u ? 4000000u : song.opl_clock;
        rate = oc / 72u;
        if (song.opl_type == 2u) { opl = ym3526_init(oc, rate); if (opl) ym3526_reset_chip(opl); }
        else { opl = ym3812_init(oc, rate); if (opl) ym3812_reset_chip(opl); }
    }
    psg_init(&psg, song.psg_clock ? song.psg_clock : 3579545u, song.psg_cfg & 0xFFFFu, song.psg_cfg >> 16 & 0xFFu);
    gb_init(&gb, song.gb_clock);
    nes_init(&nes, song.nes_clock, !song.ym_clock && song.pcm_len >= 0x8000u ? song.pcm : 0);   /* (NES : sa mémoire) */
    ay_init(&ay, song.ay_clock, (song.ay_type & 0xF0u) != 0u);
    if (song.is_mod) mod_load(&mod, vgm_buf + 64, song_size - 64u);   /* (depuis le début) */
    ym_step0 = (rate << 16) / 44100u;           /* (rate < 65536 : pas de débordement) */
    psg_step0 = psg.step_q16; gb_step0 = gb.step_q16; nes_step0 = nes.step_q16; ay_step0 = ay.step_q16;
    if (pitch > PITCH_MAX) pitch = PITCH_MAX;
    fx_apply_rate();
    fx_apply_mute();
    fx_attach(&fx, song_size <= VGM_MAX - FX_BYTES ? (int16_t *)(void *)(vgm_buf + VGM_MAX - FX_BYTES) : 0);
    ym_pos = 0; mbuf_n = 0; mbuf_l[0] = mbuf_r[0] = 0;
    song.pcm_pos = 0;
    part_open(0);
    wait_q8 = 0; played = 0; played_frac = 0; ended = 0;
    primed = 0;
    playing = 1;
}

static void render_block(void)                  /* 256 trames à 44,1 kHz dans le tampon */
{
    static int32_t mix[BLOCK * 2u];
    uint32_t left = BLOCK, o = 0, i, w = ring_w, tempo = tempo_cur * (speed_q16 >> 8) >> 8;
    if (!tempo) tempo = 1;
    for (i = 0; i < BLOCK * 2u; i++) mix[i] = 0;
    if (brake && playing) {                     /* vinyl brake : ~1,2 s jusqu'à l'arrêt */
        speed_q16 = speed_q16 > 317u + 1500u ? speed_q16 - 317u : 0u;
        if (!speed_q16) { playing = 0; brake = 0; braked = 1; speed_q16 = 1500u; }
        fx_apply_rate();
    }
    if (song.is_mod && playing) {               /* module Amiga : le lecteur de mod.h, au tempo choisi */
        mod.tempo_q8 = tempo;
        mod_render(&mod, mix, BLOCK);
        played_frac += BLOCK * tempo; played += played_frac >> 8; played_frac &= 255u;
        left = 0;
    }
    while (left && playing) {
        uint32_t n, k = 0, j, p = ym_pos, kk = 0;
        DEV_SMPL *b[2] = {mbuf_l + 1, mbuf_r + 1};
        if (wait_q8 <= 0) {                      /* les commandes de cet instant, puis l'attente suivante */
            uint32_t wt = vgm_next(&song);
            if (!wt) { playing = 0; ended = 1; break; }
            wait_q8 += (int32_t)(wt << 8);
            continue;
        }
        part_waited = 1;
        n = ((uint32_t)wait_q8 + tempo - 1u) / tempo;   /* trames de sortie jusqu'à la commande suivante, au tempo choisi */
        if (n > left) n = left;
        for (j = 0; j < n; j++) { p += ym_step; k += p >> 16; p &= 0xFFFFu; }
        if (song.ym_clock || opl) {              /* la puce FM (YM2612, sinon OPL) à sa fréquence, puis vers 44,1 kHz */
            mbuf_l[0] = mbuf_l[mbuf_n]; mbuf_r[0] = mbuf_r[mbuf_n];
            if (k) {
                if (song.ym_clock) ym2612_update_one(ym, k, b);
                else if (song.opl_type == 2u) ym3526_update_one(opl, k, b);
                else ym3812_update_one(opl, k, b);
            }
            mbuf_n = k;
            for (j = 0; j < n; j++) {
                ym_pos += ym_step; kk += ym_pos >> 16; ym_pos &= 0xFFFFu;
                mix[2u * (o + j)] += mbuf_l[kk] * OPL_GAIN_OR_2;   /* niveaux de libvgm (vgm2wav) : FM x 2, PSG : psg.h */
                mix[2u * (o + j) + 1u] += mbuf_r[kk] * OPL_GAIN_OR_2;
            }
        }
        if (song.psg_clock) {
            if (song.ym_clock) psg_render(&psg, mix + 2u * o, n);
            else {                               /* Master System, Game Gear : PSG seul, x 2 comme libvgm (mesuré) */
                static int32_t tmp[BLOCK * 2u];
                for (j = 0; j < 2u * n; j++) tmp[j] = 0;
                psg_render(&psg, tmp, n);
                for (j = 0; j < 2u * n; j++) mix[2u * o + j] += tmp[j] << 1;
            }
        }
        if (song.gb_clock) gb_render(&gb, mix + 2u * o, n);
        if (song.nes_clock) nes_render(&nes, mix + 2u * o, n);
        if (song.ay_clock) ay_render(&ay, mix + 2u * o, n);
        wait_q8 -= (int32_t)(n * tempo);
        played_frac += n * tempo; played += played_frac >> 8; played_frac &= 255u;
        left -= n; o += n;
    }
    if (loaded) fx_process(&fx, mix, BLOCK);
    for (i = 0; i < BLOCK; i++, w++) {
        int32_t l = mix[2u * i], r = mix[2u * i + 1u];
        ring[(w & (RING - 1u)) * 2u] = (int16_t)(l > 32767 ? 32767 : l < -32767 ? -32767 : l);
        ring[(w & (RING - 1u)) * 2u + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32767 ? -32767 : r);
    }
    ring_w = w;
}

/* ---- attribution des molettes et boutons (page : SysEx 07 / 08 ; blob 1) */
enum { FN_NONE, FN_TEMPO, FN_PITCH, FN_FILT, FN_ECHO, FN_TIME, FN_GRAIN, FN_MOD, FN_ENC_N };
enum { FB_NONE, FB_PITCH_DN, FB_PITCH_UP, FB_LOOP, FB_ROLL_HALF, FB_ROLL_BEAT, FB_BRAKE, FB_SPIN, FB_RESET, FB_SAVE, FB_LOAD,
       FB_SOLO, FB_PREV, FB_NEXT, FB_BTN_N };
#define MAP_ENC 6u                              /* KNOB1-4, ALGORITHM, PRESETS (SELECT : les morceaux) */
#define MAP_BTN 12u                             /* OCT-, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ (PLAY, REC : fixes) */
#define MAP_MAGIC 0x3150414Du                   /* "MAP1" */
static const uint8_t MAP_ENC_ID[MAP_ENC] = {ENC_KNOB1, ENC_KNOB2, ENC_KNOB3, ENC_KNOB4, ENC_ALGORITHM, ENC_PRESETS};
static const uint8_t MAP_BTN_ID[MAP_BTN] = {BTN_OCT_DOWN, BTN_OCT_UP, BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO,
                                            BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ};
static const uint8_t MAP_ENC_DEF[MAP_ENC] = {FN_TEMPO, FN_MOD, FN_GRAIN, FN_FILT, FN_TIME, FN_ECHO};
static const uint8_t MAP_BTN_DEF[MAP_BTN] = {FB_PITCH_DN, FB_PITCH_UP, FB_LOOP, FB_SOLO, FB_SPIN, FB_BRAKE, FB_LOAD, FB_NONE,
                                             FB_RESET, FB_SAVE, FB_ROLL_HALF, FB_ROLL_BEAT};
static struct { uint32_t magic; uint8_t enc[MAP_ENC], btn[MAP_BTN]; uint8_t pad[2]; } cmap;
static void map_load(void)
{
    uint32_t i, ok = !blob_load(1, &cmap, sizeof cmap) && cmap.magic == MAP_MAGIC;
    for (i = 0; ok && i < MAP_ENC; i++) if (cmap.enc[i] >= FN_ENC_N) ok = 0;
    for (i = 0; ok && i < MAP_BTN; i++) if (cmap.btn[i] >= FB_BTN_N) ok = 0;
    if (ok) return;
    cmap.magic = MAP_MAGIC;
    for (i = 0; i < MAP_ENC; i++) cmap.enc[i] = MAP_ENC_DEF[i];
    for (i = 0; i < MAP_BTN; i++) cmap.btn[i] = MAP_BTN_DEF[i];
}


/* ---- réception depuis la page (appelé par platform_poll) */
static uint32_t rx_size, rx_got, rx_active, load_req;
static volatile int32_t ack_cmd = -1, ack_seq = -1;
static volatile uint32_t stat_req;              /* la page demande la place de la bibliothèque (SysEx 04) */
static volatile uint32_t list_req, del_req, del_crc;   /* SysEx 05 : la liste ; 06 : retirer un morceau (par son CRC) */
static volatile uint32_t map_req, map_get;      /* SysEx 07 : changer l'attribution ; 08 : la lire */
static uint8_t map_new[18];

static void app_sysex(const uint8_t *p, uint32_t n)
{
    if (n < 4u || p[1] != 0x52u || p[2] != 0x53u) return;
    if (p[3] == 4u) { stat_req = 1; return; }   /* place : CC 119 morceaux, CC 120 secteurs pris, CC 121 secteurs en tout */
    if (p[3] == 5u) { list_req = 1; return; }   /* liste : un SysEx 0x15 par morceau, puis 0x16 (la place) */
    if (p[3] == 6u && n >= 9u) { del_crc = rd35(p + 4); del_req = 1; return; }   /* retirer : CC 117 = 6, ou 0x46 */
    if (p[3] == 7u && n >= 4u + MAP_ENC + MAP_BTN) {   /* attribution des molettes et boutons : CC 117 = 7, ou 0x47 */
        uint32_t i, ok = 1;
        for (i = 0; i < MAP_ENC; i++) if (p[4 + i] >= FN_ENC_N) ok = 0;
        for (i = 0; i < MAP_BTN; i++) if (p[4 + MAP_ENC + i] >= FB_BTN_N) ok = 0;
        if (!ok) { ack_cmd = 0x47; return; }
        for (i = 0; i < MAP_ENC + MAP_BTN; i++) map_new[i] = p[4 + i];
        map_req = 1;
        return;
    }
    if (p[3] == 8u) { map_get = 1; return; }     /* l'attribution : SysEx 0x18 <6 molettes> <12 boutons> */
    if (p[3] == 1u && n >= 9u) {                /* début */
        uint32_t i = 9, t = 0, size = rd35(p + 4);
        if (!size || size > VGM_MAX) { ack_cmd = 0x41; return; }
        playing = 0; loaded = 0; ended = 0;
        rx_size = size; rx_got = 0; rx_active = 1;
        while (i < n && p[i] && t < sizeof title - 1u) title[t++] = (char)p[i++];
        title[t] = 0;
        while (i < n && p[i]) i++;
        i++; t = 0;
        while (i < n && p[i] && t < sizeof game - 1u) game[t++] = (char)p[i++];
        game[t] = 0;
        while (i < n && p[i]) i++;
        i++; t = 0;
        while (i < n && p[i] && t < sizeof sys - 1u) sys[t++] = (char)p[i++];
        sys[t] = 0;
        ack_cmd = 8;                            /* 8 : RSM5 (+ OPL) ; 7 : RSM4 et modules (RSMD) ; 6 : RSM4 ; 5 : RSM3 ; 4 : RSM2 ; 2 : RSM1 ; 1 : VGM brut */
    } else if (p[3] == 2u && n >= 6u) {         /* données */
        uint32_t seq = (uint32_t)p[4] | (uint32_t)p[5] << 7, off = seq * 512u, want, got;
        if (!rx_active || off >= rx_size) { ack_cmd = 0x42; return; }
        want = rx_size - off < 512u ? rx_size - off : 512u;
        got = unpack7(p + 6, n - 6u, vgm_buf + off, want);
        if (got < want) { ack_cmd = 0x43; return; }
        if (off + want > rx_got) rx_got = off + want;
        ack_seq = (int32_t)(seq & 127u);
    } else if (p[3] == 3u && n >= 9u) {         /* fin */
        if (!rx_active || rx_got < rx_size) { ack_cmd = 0x44; return; }
        rx_active = 0;
        if (rx_crc32(vgm_buf, rx_size) != rd35(p + 4)) { ack_cmd = 0x45; return; }
        load_req = 1;                           /* la boucle principale ouvre le morceau et répond */
    }
}

/* ---- bibliothèque : les morceaux reçus, gardés en mémoire flash (après extinction aussi)
 * Zone : FM-1 0x97000-0xE0000 (292 Ko, le magasin de la famille Felucca / Dinghy, store.h) ; SMK-37 : les slots.
 * Chaque morceau : des secteurs de 4 Ko qui se suivent ; la liste dans le blob 0 (CRC, platform.c). Quand la place
 * manque, les morceaux les plus anciens s'en vont. Un morceau déjà présent (même taille, même CRC) n'est pas recopié. */
#if BOARD_SMK37
#define LIB_LO SLOTS_AT
#define LIB_HI (SLOTS_AT + NSLOT * SLOT_SECT * 0x1000u)
#else
#define LIB_LO 0x00097000u
#define LIB_HI 0x000E0000u
#endif
#define LIB_NS ((LIB_HI - LIB_LO) / 0x1000u)  /* secteurs */
#define LIB_MAX 40u
_Static_assert(LIB_NS < 128u && LIB_MAX < 128u, "la place se dit en CC : 0..127");
#define LIB_MAGIC 0x3142494Cu                  /* "LIB1" */
typedef struct {
    uint16_t sect, nsect;
    uint32_t size, crc, seq;
    char title[28], game[28], sys[16];
} lib_ent_t;
static struct {
    uint32_t magic;
    uint16_t count, cur;                        /* cur : le dernier morceau joué */
    uint32_t seq;
    lib_ent_t e[LIB_MAX];
} lib;
_Static_assert(sizeof lib <= SECTOR_MAX, "la liste doit tenir dans un blob");
#if !BOARD_SMK37
_Static_assert(LIB_LO >= FL_DATA_LO && LIB_HI <= FL_DATA_HI && LIB_HI <= FL_OTA_LO, "bibliothèque hors du magasin FL_DATA");
#endif

#ifdef DINGHY_HOST                              /* sur PC : une mémoire flash simulée */
static uint8_t host_flash[0x100000];
static int lib_ok(void) { return 1; }
static int lib_erase(uint32_t at) { memset(host_flash + at, 0xFF, 0x1000); return 0; }
static int lib_write(uint32_t at, const uint8_t *p, uint32_t n) { memcpy(host_flash + at, p, n); return 0; }
static int lib_read(uint32_t at, uint8_t *p, uint32_t n) { memcpy(p, host_flash + at, n); return 0; }
#else
static int lib_ok(void) { return flash_ok; }
static int lib_erase(uint32_t at) { uint32_t took; fm1_wdt_feed(); return fl_erase4k(at, &took); }
static int lib_write(uint32_t at, const uint8_t *p, uint32_t n) { fm1_wdt_feed(); return fl_write(at, p, n); }
static int lib_read(uint32_t at, uint8_t *p, uint32_t n) { fm1_wdt_feed(); return st_read(at, p, n); }
#endif

static void str_copy(char *d, const char *s, uint32_t n) { uint32_t i = 0; while (s[i] && i < n - 1u) { d[i] = s[i]; i++; } d[i] = 0; }
static uint32_t crc_more(uint32_t c, const uint8_t *p, uint32_t n)   /* CRC-32 par morceaux : c = ~0 au départ */
{
    uint32_t k;
    while (n--) { c ^= *p++; for (k = 0; k < 8u; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}

static void lib_load(void)
{
    if (blob_load(0, &lib, sizeof lib) || lib.magic != LIB_MAGIC || lib.count > LIB_MAX) {
        memset(&lib, 0, sizeof lib);
        lib.magic = LIB_MAGIC;
    }
    if (lib.cur >= lib.count) lib.cur = 0;
}
static int lib_save(void) { return blob_save(0, &lib, sizeof lib); }

static int lib_free_run(uint32_t ns)           /* le premier trou de ns secteurs, ou -1 */
{
    uint32_t s = 0, i;
    while (s + ns <= LIB_NS) {
        uint32_t hit = 0;
        for (i = 0; i < lib.count; i++)
            if (lib.e[i].sect < s + ns && s < (uint32_t)lib.e[i].sect + lib.e[i].nsect) { hit = (uint32_t)lib.e[i].sect + lib.e[i].nsect; break; }
        if (!hit) return (int)s;
        s = hit;
    }
    return -1;
}
static void lib_remove(uint32_t i)
{
    if (i >= lib.count) return;
    if (i < lib.cur) lib.cur--;                 /* (le morceau courant reste le même) */
    for (; i + 1u < lib.count; i++) lib.e[i] = lib.e[i + 1u];
    lib.count--;
    if (lib.cur >= lib.count) lib.cur = lib.count ? lib.count - 1u : 0u;
}

static void show_busy(const char *what);
static uint32_t lib_free_sect(void)
{
    uint32_t i, used = 0;
    for (i = 0; i < lib.count; i++) used += lib.e[i].nsect;
    return used < LIB_NS ? LIB_NS - used : 0u;
}

/* tasser : chaque morceau descend contre le précédent, secteur par secteur (copie montante : la destination est
 * toujours sous la source, on n'écrase que du déjà copié ou du libre). La liste est enregistrée après chaque morceau
 * déplacé ; une coupure au milieu d'un déplacement ne perd que ce morceau-là (son CRC ne correspond plus). */
static int lib_compact(void)
{
    static uint8_t buf[512];
    uint32_t next = 0;
    for (;;) {
        uint32_t i, best = LIB_MAX, k, j;
        for (i = 0; i < lib.count; i++)            /* le premier morceau au-dessus de « next » */
            if (lib.e[i].sect >= next && (best == LIB_MAX || lib.e[i].sect < lib.e[best].sect)) best = i;
        if (best == LIB_MAX) return 0;
        if (lib.e[best].sect > next) {
            lib_ent_t *e = &lib.e[best];
            for (k = 0; k < e->nsect; k++) {
                uint32_t from = LIB_LO + ((uint32_t)e->sect + k) * 0x1000u, to = LIB_LO + (next + k) * 0x1000u;
                if (lib_erase(to)) return -1;
                for (j = 0; j < 0x1000u; j += sizeof buf)
                    if (lib_read(from + j, buf, sizeof buf) || lib_write(to + j, buf, sizeof buf)) return -1;
            }
            e->sect = (uint16_t)next;
            if (lib_save()) return -1;
        }
        next = (uint32_t)lib.e[best].sect + lib.e[best].nsect;
    }
}

/* le morceau en RAM (vgm_buf, size octets) dans la bibliothèque : son index, ou -1 (pas de flash, trop gros) */
static uint32_t lib_dropped;                    /* morceaux retirés pour faire de la place au dernier reçu */
static int lib_store(uint32_t size, uint32_t crc)
{
    uint32_t ns = (size + 0xFFFu) / 0x1000u, i, at, c;
    int s;
    lib_ent_t *e;
    if (!lib_ok() || ns > LIB_NS) return -1;
    for (i = 0; i < lib.count; i++)
        if (lib.e[i].size == size && lib.e[i].crc == crc) { lib.cur = (uint16_t)i; lib_save(); return (int)i; }
    lib_dropped = 0;
    while ((s = lib_free_run(ns)) < 0 || lib.count >= LIB_MAX) {   /* place : tasser, sinon retirer le plus ancien */
        uint32_t old = 0;
        if (lib.count < LIB_MAX && lib_free_sect() >= ns) {
            show_busy("Rangement...");
            if (lib_compact()) return -1;
            continue;
        }
        if (!lib.count) return -1;
        lib_dropped++;
        for (i = 1; i < lib.count; i++) if (lib.e[i].seq < lib.e[old].seq) old = i;
        lib_remove(old);
    }
    at = LIB_LO + (uint32_t)s * 0x1000u;
    for (i = 0; i < ns; i++) {
        uint32_t k = size - i * 0x1000u < 0x1000u ? size - i * 0x1000u : 0x1000u;
        if (lib_erase(at + i * 0x1000u) || lib_write(at + i * 0x1000u, vgm_buf + i * 0x1000u, k)) return -1;
    }
    for (i = 0, c = ~0u; i < size; i += 512u) {   /* relire : la copie doit être exacte */
        static uint8_t chk[512];
        uint32_t k = size - i < 512u ? size - i : 512u;
        if (lib_read(at + i, chk, k)) return -1;
        c = crc_more(c, chk, k);
    }
    if (~c != crc) return -1;
    e = &lib.e[lib.count];
    e->sect = (uint16_t)s; e->nsect = (uint16_t)ns; e->size = size; e->crc = crc; e->seq = ++lib.seq;
    str_copy(e->title, title, sizeof e->title); str_copy(e->game, game, sizeof e->game); str_copy(e->sys, sys, sizeof e->sys);
    lib.cur = lib.count++;
    if (lib_save()) return -1;
    return (int)lib.cur;
}

/* un morceau de la bibliothèque en RAM : 0, ou -1 (illisible) */
static int lib_fetch(uint32_t i)
{
    const lib_ent_t *e = &lib.e[i];
    if (i >= lib.count || e->size > VGM_MAX) return -1;
    if (lib_read(LIB_LO + (uint32_t)e->sect * 0x1000u, vgm_buf, e->size)) return -1;
    if (rx_crc32(vgm_buf, e->size) != e->crc) return -1;
    str_copy(title, e->title, sizeof title); str_copy(game, e->game, sizeof game); str_copy(sys, e->sys, sizeof sys);
    rx_size = e->size;
    return song_load(e->size) ? -1 : 0;
}

/* ---- écran */
#define C_ACC RGB(255, 165, 0)                  /* orange ROMsounds */
#define C_TXT RGB(220, 220, 220)
#define C_OFF RGB(110, 110, 110)
#define C_BAD RGB(255, 90, 70)
#define BUDGET_US 5805u                          /* 256 trames à 44,1 kHz */

static char *put_u(char *p, uint32_t v)
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *put_t(char *p, uint32_t frames)    /* m:ss */
{
    uint32_t s = frames / 44100u;
    p = put_u(p, s / 60u);
    *p++ = ':';
    *p++ = (char)('0' + s % 60u / 10u);
    *p++ = (char)('0' + s % 10u);
    return p;
}

static void app_idle(void) { }

static const char *msg;                         /* un message passager (Enregistre, Efface...) */
static uint32_t msg_until;
static void say(const char *m) { msg = m; msg_until = fm1_ms + 2500u; }

/* ---- effets : les touches coupent les voix ; molettes et boutons : des fonctions au choix (la page les attribue,
 * SysEx 07 ; enregistré dans le blob 1). Presets d'effets : blob 2. */
#define NPRESET 12u                             /* les 12 premières touches */
typedef struct { uint8_t used, echo, time, grain; int8_t filt, mod, pitch, pad; uint16_t tempo, pad2; uint32_t mute; } preset_t;
static struct { uint32_t magic; preset_t p[NPRESET]; } presets;
#define PRESET_MAGIC 0x31455250u                /* "PRE1" */

static char fx_msg[40];
static uint32_t fx_notes_prev, fx_leds, fx_glow, fx_blink;   /* (0 au départ : les LED sont éteintes) */
static const char *voice_name(uint32_t k)
{
    static const char *const YM[7] = {"FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM 6", "DAC"};
    static const char *const SN[4] = {"PSG 1", "PSG 2", "PSG 3", "Bruit PSG"};
    static const char *const GB[4] = {"Carre 1", "Carre 2", "Onde", "Bruit"};
    static const char *const NS[5] = {"Carre 1", "Carre 2", "Triangle", "Bruit", "DPCM"};
    static const char *const AY[4] = {"Voie A", "Voie B", "Voie C", "Bruit AY"};
    static const char *const OP[10] = {"FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM 6", "FM 7", "FM 8", "FM 9", "Percussions"};
    static const char *const MD[8] = {"Voie 1", "Voie 2", "Voie 3", "Voie 4", "Voie 5", "Voie 6", "Voie 7", "Voie 8"};
    if (song.is_mod) return k < 8u ? MD[k] : "?";
    uint32_t y, p, g, a, tot = voices(&y, &p, &g, &a);
    if (song.ay_clock && k >= tot - 4u && k < tot) return AY[k - (tot - 4u)];
    if (song.opl_clock && k >= a + (song.nes_clock ? 5u : 0u) && k < a + (song.nes_clock ? 5u : 0u) + 10u)
        return OP[k - a - (song.nes_clock ? 5u : 0u)];
    if (song.ym_clock && k < y + 7u) return YM[k - y];
    if (song.psg_clock && k >= p && k < p + 4u) return SN[k - p];
    if (song.gb_clock && k >= g && k < g + 4u) return GB[k - g];
    if (song.nes_clock && k >= a && k < a + 5u) return NS[k - a];
    return "?";
}
static char *put_i(char *p, int32_t v) { if (v > 0) *p++ = '+'; else if (v < 0) { *p++ = '-'; v = -v; } return put_u(p, (uint32_t)v); }
static void fx_show(char *p) { *p = 0; say(fx_msg); }
static uint32_t fx_any(void)
{
    return tempo_cur != 256u || pitch || mute || fx.filt || fx.mod || fx.echo || fx.grain || fx.loop || fx.spin || brake || braked;
}
static void fx_summary(char *s)                  /* la ligne du bas, quand un effet est en route */
{
    char *p = s;
    uint32_t m = mute, n = 0;
    if (braked) p = put_s(p, "arret (PLAY) ");
    if (tempo_cur != 256u) { p = put_u(p, tempo_cur * 100u / 256u); p = put_s(p, "% "); }
    if (pitch) { p = put_i(p, pitch); p = put_s(p, "dt "); }
    if (fx.filt) p = put_s(p, fx.filt < 0 ? "PB " : "PH ");
    if (fx.mod) p = put_s(p, fx.mod < 0 ? "phaser " : "flanger ");
    if (fx.echo) p = put_s(p, "echo ");
    if (fx.grain) p = put_s(p, "grain ");
    if (fx.loop) p = put_s(p, "boucle ");
    while (m) { n += m & 1u; m >>= 1; }
    if (n) { p = put_u(p, n); p = put_s(p, n > 1u ? " muettes" : " muette"); }
    *p = 0;
}
/* le tempo du morceau (RSM3) : un temps en trames du morceau ; 120 BPM si inconnu */
static uint32_t beat_frames(void) { return 264600000u / (song.bpm100 ? song.bpm100 : 12000u); }
static uint32_t tempo_eff(void) { uint32_t t = tempo_cur * (speed_q16 >> 8) >> 8; return t ? t : 1u; }   /* Q8 */

static void fx_enc(uint32_t fn, int32_t e)      /* une molette a tourné de e crans */
{
    int32_t v;
    char *p;
    switch (fn) {
    case FN_TEMPO:
        v = (int32_t)tempo_cur + 13 * e; tempo_cur = (uint32_t)(v < 64 ? 64 : v > 512 ? 512 : v);
        fx_show(put_s(put_u(put_s(fx_msg, "Tempo "), tempo_cur * 100u / 256u), " %")); break;
    case FN_PITCH:
        v = pitch + e; pitch = v < -12 ? -12 : v > PITCH_MAX ? PITCH_MAX : v; fx_apply_rate();
        fx_show(put_s(put_i(put_s(fx_msg, "Hauteur "), pitch), " demi-tons")); break;
    case FN_FILT:
        v = fx.filt + e; v = v < -20 ? -20 : v > 20 ? 20 : v;
        if (!fx.filt || (fx.filt < 0) != (v < 0)) { fx.lo[0] = fx.lo[1] = fx.bd[0] = fx.bd[1] = 0; }
        fx.filt = (int8_t)v;
        p = !v ? put_s(fx_msg, "Filtre eteint")
               : put_s(put_u(put_s(fx_msg, v < 0 ? "Passe-bas " : "Passe-haut "), v < 0 ? FX_LP_HZ[-v] : FX_HP_HZ[v]), " Hz");
        fx_show(p); break;
    case FN_ECHO:
        v = fx.echo + e; fx.echo = (uint8_t)(v < 0 ? 0 : v > 10 ? 10 : v);
        p = !fx.buf ? put_s(fx_msg, "Echo : morceau trop gros") : !fx.echo ? put_s(fx_msg, "Echo eteint")
            : put_s(put_u(put_s(fx_msg, "Echo : "), fx.echo), fx.echo > 1u ? " repetitions" : " repetition");
        fx_show(p); break;
    case FN_TIME:
        v = fx.time + e; fx.time = (uint8_t)(v < 1 ? 1 : v > 11 ? 11 : v);
        fx_show(put_s(put_u(put_s(fx_msg, "Temps echo, boucle "), (uint32_t)fx.time * 50u), " ms")); break;
    case FN_GRAIN:
        v = fx.grain + e; fx.grain = (uint8_t)(v < 0 ? 0 : v > 12 ? 12 : v);
        fx_show(fx.grain ? put_u(put_s(fx_msg, "Grain "), fx.grain) : put_s(fx_msg, "Grain eteint")); break;
    case FN_MOD:
        v = fx.mod + e; fx.mod = (int8_t)(v < -10 ? -10 : v > 10 ? 10 : v);
        fx_show(!fx.mod ? put_s(fx_msg, "Modulation eteinte")
                : put_u(put_s(fx_msg, fx.mod < 0 ? "Phaser " : "Flanger "), (uint32_t)(fx.mod < 0 ? -fx.mod : fx.mod)));
        break;
    default: break;
    }
}
static void preset_save(uint32_t k)
{
    preset_t *q = &presets.p[k];
    q->used = 1; q->echo = fx.echo; q->time = fx.time; q->grain = fx.grain; q->filt = fx.filt; q->mod = fx.mod;
    q->pitch = (int8_t)pitch; q->tempo = (uint16_t)tempo_cur; q->mute = mute;
    presets.magic = PRESET_MAGIC;
    if (blob_save(2, &presets, sizeof presets)) fx_show(put_s(fx_msg, "Preset pas enregistre (flash)"));
    else fx_show(put_u(put_s(fx_msg, "Preset enregistre : touche "), k + 1u));
}
static void preset_init(void)
{
    if (blob_load(2, &presets, sizeof presets) || presets.magic != PRESET_MAGIC) { memset(&presets, 0, sizeof presets); presets.magic = PRESET_MAGIC; }
}
static void preset_load(uint32_t k)
{
    const preset_t *q = &presets.p[k];
    if (!q->used) { fx_show(put_s(put_u(put_s(fx_msg, "Touche "), k + 1u), " : pas de preset")); return; }
    fx.echo = q->echo; fx.time = q->time; fx.grain = q->grain;
    if ((fx.filt < 0) != (q->filt < 0) || !fx.filt) { fx.lo[0] = fx.lo[1] = fx.bd[0] = fx.bd[1] = 0; }
    fx.filt = q->filt; fx.mod = q->mod; tempo_cur = q->tempo; pitch = q->pitch; mute = q->mute;
    if (pitch > PITCH_MAX) pitch = PITCH_MAX;
    fx_apply_rate(); fx_apply_mute();
    fx_show(put_u(put_s(fx_msg, "Preset "), k + 1u));
}
static void fx_resume(void) { braked = 0; brake = 0; speed_q16 = 65536u; fx_apply_rate(); }

static void fx_controls(uint32_t pressed, uint32_t released)
{
    uint32_t y, pa, g, a, nv = voices(&y, &pa, &g, &a), notes = fm1_in.notes, hit = notes & ~fx_notes_prev, k, i;
    uint32_t held = 0, leds = 0, glow = 0, mod_solo = 0, mod_save = 0, mod_load = 0;
    char *p;
    fx_notes_prev = notes;
    if (nv > 27u) nv = 27u;
    for (i = 0; i < MAP_BTN; i++) if (fm1_in.buttons >> MAP_BTN_ID[i] & 1u) {
        held |= 1u << i;
        if (cmap.btn[i] == FB_SOLO) mod_solo = 1;
        if (cmap.btn[i] == FB_SAVE) mod_save = 1;
        if (cmap.btn[i] == FB_LOAD) mod_load = 1;
    }
    /* touches : preset (SAVE / rappel tenus), sinon voix (SEL tenu : solo) */
    for (k = 0; k < 27u; k++) {
        if (!(hit >> k & 1u)) continue;
        if (mod_save || mod_load) {
            if (k >= NPRESET) fx_show(put_s(fx_msg, "Presets : les 12 premieres touches"));
            else if (mod_save) preset_save(k);
            else preset_load(k);
            continue;
        }
        if (!loaded || k >= nv) continue;
        {
            uint32_t all = (1u << nv) - 1u;
            if (mod_solo) {
                if ((mute & all) == (all & ~(1u << k))) { mute = 0; p = put_s(fx_msg, "Toutes les voix"); }
                else { mute = all & ~(1u << k); p = put_s(put_s(fx_msg, "Solo "), voice_name(k)); }
            } else {
                mute ^= 1u << k;
                p = put_s(put_s(fx_msg, voice_name(k)), mute >> k & 1u ? " coupee" : " remise");
            }
            fx_show(p);
            fx_apply_mute();
        }
    }
    for (i = 0; i < MAP_ENC; i++) { int32_t e = fm1_enc_take(MAP_ENC_ID[i]); if (e) fx_enc(cmap.enc[i], e); }
    for (i = 0; i < MAP_BTN; i++) {
        uint32_t fn = cmap.btn[i], down = pressed >> MAP_BTN_ID[i] & 1u, up = released >> MAP_BTN_ID[i] & 1u;
        if (!down && !up) continue;
        switch (fn) {
        case FB_PITCH_DN: case FB_PITCH_UP: if (down) fx_enc(FN_PITCH, fn == FB_PITCH_UP ? 1 : -1); break;
        case FB_LOOP:
            if (down && loaded) { if (!fx.buf) say("Boucle : morceau trop gros"); fx_loop(&fx, fx_len(&fx), 0, 0); }
            if (up) fx.loop = 0;
            break;
        case FB_ROLL_HALF: case FB_ROLL_BEAT:
            if (down && loaded) {               /* le bout commence sur le dernier demi-temps (ou temps) */
                uint32_t L = beat_frames() >> (fn == FB_ROLL_HALF ? 1 : 0), ph = song.beat0 % L, t = played, back, te = tempo_eff();
                back = t >= ph ? (t - ph) % L : 0u;
                if (!fx.buf) say("Beat roll : morceau trop gros");
                fx_loop(&fx, L * 256u / te, back * 256u / te, 1);   /* (L < 2^23 : pas de débordement) */
                fx_show(put_s(put_u(put_s(fx_msg, "Beat roll "), song.bpm100 ? song.bpm100 / 100u : 120u),
                              fn == FB_ROLL_HALF ? " BPM, 1/2 temps" : " BPM, 1 temps"));
            }
            if (up) fx.loop = 0;
            break;
        case FB_BRAKE:
            if (down && loaded) {
                if (braked) { fx_resume(); playing = 1; say("Platine relancee"); }
                else if (playing) { brake = 1; say("Vinyl brake"); }
            }
            break;
        case FB_SPIN: if (down && loaded) { if (!fx.buf) say("Backspin : morceau trop gros"); else { fx_backspin(&fx); say("Backspin"); } } break;
        case FB_RESET:
            if (down) { tempo_cur = 256; pitch = 0; mute = 0; fx_reset(&fx); fx_resume(); fx_apply_mute(); say("Effets remis a zero"); }
            break;
        case FB_SAVE: if (down && !notes) say("SAVE + touche 1-12 : enregistrer"); break;
        case FB_LOAD: if (down && !notes) say("Tenir + touche 1-12 : rappeler"); break;
        case FB_PREV: case FB_NEXT: if (down) track_turn += fn == FB_NEXT ? 1 : -1, track_turn_t = fm1_ms | 1u; break;
        default: break;
        }
    }
    /* touches allumées : une voix qui joue ; une voix coupée : faible lueur et un clin d'oeil par seconde */
    fx_blink = (fm1_ms % 1000u) < 120u;
    if (loaded) for (k = 0; k < nv; k++) { if (!(mute >> k & 1u)) leds |= 1u << k; else { glow |= 1u << k; if (fx_blink) leds |= 1u << k; } }
    if (leds != fx_leds) { for (k = 0; k < 27u; k++) if ((leds ^ fx_leds) >> k & 1u) fm1_led_key(KEY_LED(k), (int)(leds >> k & 1u)); fx_leds = leds; }
    if (glow != fx_glow) { for (k = 0; k < 27u; k++) if ((glow ^ fx_glow) >> k & 1u) fm1_led_key_glow(KEY_LED(k), (int)(glow >> k & 1u)); fx_glow = glow; }
    for (i = 0; i < MAP_BTN; i++) {             /* un bouton allumé : son effet est en route */
        uint32_t fn = cmap.btn[i], on = (fn == FB_LOOP || fn == FB_ROLL_HALF || fn == FB_ROLL_BEAT) ? fx.loop && (held >> i & 1u)
                    : fn == FB_BRAKE ? brake || braked : fn == FB_SPIN ? fx.spin != 0u : 0u;
        if (fn == FB_LOOP || fn == FB_ROLL_HALF || fn == FB_ROLL_BEAT || fn == FB_BRAKE || fn == FB_SPIN) fm1_led_key(MAP_BTN_ID[i], (int)on);
    }
}

static void draw_song(void)                     /* système, titre, jeu, place dans la bibliothèque */
{
    char s[40], *p;
    lcd_fill(0, 52, 240, 188, C_BLACK);
    draw_text_box(0, 56, 240, &FONT_S, loaded && sys[0] ? sys : "v" APP_VER, C_OFF, 1);
    if (!loaded) {
        draw_text_box(0, 96, 240, &FONT_S, rx_active ? "Reception..." : lib.count ? "Morceau illisible" : "Aucun morceau", C_TXT, 1);
        draw_text_box(0, 120, 240, &FONT_S, rx_active ? "" : "Envoie un VGM depuis la page", C_OFF, 1);
        return;
    }
    draw_text_box(0, 84, 240, &FONT_S, title[0] ? title : "(sans titre)", C_TXT, 1);
    draw_text_box(0, 106, 240, &FONT_S, game, C_OFF, 1);
    if (lib.count && lib_ok()) {
        p = put_u(s, (uint32_t)lib.cur + 1u); p = put_s(p, " / "); p = put_u(p, lib.count);
        p = put_s(p, "   "); p = put_u(p, lib_free_sect() * 4u); p = put_s(p, " Ko libres"); *p = 0;
        draw_text_box(0, 174, 240, &FONT_S, s, C_OFF, 1);
    } else draw_text_box(0, 174, 240, &FONT_S, "pas en memoire : joue seulement", C_OFF, 1);
}

static void draw_time(uint32_t pct, uint32_t cuts)
{
    char s[40], *p;
    if (!loaded) {
        if (rx_active && rx_size) {
            p = put_s(s, "Reception "); p = put_u(p, rx_got * 100u / rx_size); p = put_s(p, " %"); *p = 0;
            draw_text_box(0, 140, 240, &FONT_L, s, C_ACC, 1);
        }
        return;
    }
    {   /* après la fin, le temps reste dans la partie qui boucle */
        uint32_t t = played;
        if (song.total && t > song.total && song.loop_samples && song.loop_samples <= song.total)
            t = song.total - song.loop_samples + (t - song.total) % song.loop_samples;
        p = put_t(s, t);
    }
    if (song.total) { p = put_s(p, " / "); p = put_t(p, song.total); }
    *p = 0;
    draw_text_box(0, 134, 240, &FONT_L, s, playing ? C_ACC : C_OFF, 1);
    if (msg && (int32_t)(msg_until - fm1_ms) > 0) { draw_text_box(0, 198, 240, &FONT_S, msg, C_ACC, 1); }
    else {
        msg = 0;
        p = put_s(s, "calcul "); p = put_u(p, pct); p = put_s(p, " %   coupures "); p = put_u(p, cuts); *p = 0;
        draw_text_box(0, 198, 240, &FONT_S, s, pct < 85u ? C_OFF : C_BAD, 1);
    }
    if (fx_any() && !ended) { fx_summary(s); draw_text_box(0, 220, 240, &FONT_S, s, C_ACC, 1); }
    else draw_text_box(0, 220, 240, &FONT_S, ended ? "fin - PLAY : rejouer" : playing ? "PLAY pause  REC debut"
                                                                              : "PLAY lecture  REC debut", C_OFF, 1);
}

static void show_busy(const char *what)          /* un travail long (enregistrer, lire la flash) : le dire d'abord */
{
    lcd_fill(0, 130, 240, 110, C_BLACK);
    draw_text_box(0, 150, 240, &FONT_S, what, C_ACC, 1);
}

static void open_lib(uint32_t i, uint32_t play)  /* jouer le morceau i de la bibliothèque */
{
    if (i >= lib.count) return;
    playing = 0;
    show_busy("Chargement...");
    if (lib_fetch(i)) { loaded = 0; say("morceau illisible"); draw_song(); return; }
    if (lib.cur != i) { lib.cur = (uint16_t)i; lib_save(); }
    loaded = 1; song_start(); playing = play; underruns = 0;
    draw_song();
}

static void lib_delete(uint32_t i)              /* retirer le morceau i (le courant : on passe au suivant, à l'arrêt) */
{
    uint32_t was_cur = i == lib.cur;
    if (i >= lib.count) return;
    if (was_cur) playing = 0;
    lib_remove(i);
    show_busy("Effacement...");
    lib_save();
    if (!was_cur) draw_song();
    else if (lib.count) open_lib(lib.cur < lib.count ? lib.cur : 0u, 0);
    else { loaded = 0; draw_song(); }
    stat_req = 1;
}

/* ---- SysEx vers la page : F0 7D 52 53 ... F7, des octets de 7 bits */
#ifdef DINGHY_HOST
static int rs_sysex_send(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    printf("%6u ms  SYSEX out", fm1_ms);
    for (i = 0; i < n; i++) printf(" %02X", p[i]);
    printf("\n");
    return 0;
}
#else
static int rs_sysex_send(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }
#endif
static uint8_t *put7_35(uint8_t *q, uint32_t v) { uint32_t k; for (k = 0; k < 5u; k++) *q++ = (uint8_t)(v >> (7u * k) & 127u); return q; }
static uint8_t *put7_str(uint8_t *q, const char *t) { while (*t) *q++ = (uint8_t)(*t++ & 127); *q++ = 0; return q; }
static void lib_send_list(void)                 /* 0x15 : n°, secteurs, taille, CRC, titre, jeu, système ; 0x16 : la place */
{
    static uint8_t f[128];
    uint32_t i, ok = lib_ok() ? 1u : 0u;
    for (i = 0; ok && i < lib.count; i++) {
        const lib_ent_t *e = &lib.e[i];
        uint8_t *q = f;
        *q++ = 0xF0; *q++ = 0x7D; *q++ = 0x52; *q++ = 0x53; *q++ = 0x15;
        *q++ = (uint8_t)i; *q++ = (uint8_t)(e->nsect & 127u);
        q = put7_35(q, e->size); q = put7_35(q, e->crc);
        q = put7_str(q, e->title); q = put7_str(q, e->game); q = put7_str(q, e->sys);
        q[-1] = 0xF7;                           /* (le dernier 0 devient la fin) */
        if (rs_sysex_send(f, (uint32_t)(q - f))) return;
    }
    {
        uint8_t *q = f;
        *q++ = 0xF0; *q++ = 0x7D; *q++ = 0x52; *q++ = 0x53; *q++ = 0x16;
        *q++ = (uint8_t)(ok ? lib.count : 0u); *q++ = (uint8_t)(ok ? LIB_NS - lib_free_sect() : 0u);
        *q++ = (uint8_t)(ok ? LIB_NS : 0u); *q++ = (uint8_t)(ok && lib.count && loaded ? lib.cur : 127u);
        *q++ = 0xF7;
        rs_sysex_send(f, (uint32_t)(q - f));
    }
}

#ifdef DINGHY_HOST                              /* sur PC : ROMSOUNDS_RSM=a.rsm,b.rsm... : reçus comme depuis la page, un toutes
                                                 * les 3 s (ROMSOUNDS_EVERY ms), le premier au démarrage */
static void rs_host_load(void)
{
    static uint32_t k, due;
    const char *list = getenv("ROMSOUNDS_RSM"), *every = getenv("ROMSOUNDS_EVERY");
    char f[256];
    uint32_t i = 0, n = 0;
    FILE *fp;
    if (!list || fm1_ms < due || load_req || rx_active) return;
    while (list[i] && n < k) if (list[i++] == ',') n++;  /* le k-ième nom */
    if (!list[i] && n < k) return;
    for (n = 0; list[i] && list[i] != ',' && n < sizeof f - 1u; ) f[n++] = list[i++];
    f[n] = 0;
    if (!n) return;
    k++;
    due = fm1_ms + (every ? (uint32_t)atoi(every) : 3000u);
    fp = fopen(f, "rb");
    if (!fp) return;
    rx_size = (uint32_t)fread(vgm_buf, 1, VGM_MAX, fp);
    fclose(fp);
    rx_got = rx_size;
    load_req = 1;
    str_copy(title, f, sizeof title);
    strcpy(game, "(test)");
    strcpy(sys, "Test");
}
/* sur PC : ROMSOUNDS_SX="ms:7D 52 53 05;ms:7D 52 53 06 ..." : des SysEx de la page (sans F0 ni F7), à ces instants */
static void rs_host_sx(void)
{
    static uint32_t at;
    const char *l = getenv("ROMSOUNDS_SX");
    uint8_t b[64];
    uint32_t n = 0, t;
    if (!l) return;
    l += at;
    if (!*l || sscanf(l, "%u:", &t) != 1 || fm1_ms < t) return;
    while (*l && *l != ':') l++, at++;
    if (*l) l++, at++;
    while (*l && *l != ';' && n < sizeof b) {
        unsigned v;
        int k;
        if (sscanf(l, " %x%n", &v, &k) != 1) break;
        b[n++] = (uint8_t)v; l += k; at += (uint32_t)k;
    }
    while (*l && *l != ';') l++, at++;
    if (*l == ';') at++;
    app_sysex(b, n);
}
#endif

static void app_main(void)
{
    uint32_t b, rel, t_shown = 0, sum = 0, cnt = 0, pct = 0, shown_rx = ~0u, rec_t = 0, rec_done = 0;

    fx_reset(&fx);
    map_load();
    preset_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    lib_load();
    if (lib.count) open_lib(lib.cur, 0);        /* au démarrage : le dernier morceau, prêt (PLAY) */
    else draw_song();

    for (;;) {
        platform_poll();
#ifdef DINGHY_HOST
        rs_host_load();
        rs_host_sx();
#endif

        if (load_req) {                          /* morceau reçu : l'ouvrir, le garder, répondre à la page */
            int e;
            load_req = 0;
            e = song_load(rx_size);
            if (e) { loaded = 0; ack_cmd = 0x48 + e; }
            else {
                if (lib_ok()) {
                    show_busy("Enregistrement...");
                    if (lib_store(rx_size, rx_crc32(vgm_buf, rx_size)) < 0) say("pas enregistre (flash)");
                    else if (lib_dropped == 1u) say("plein : le plus ancien retire");
                    else if (lib_dropped) say("plein : des anciens retires");
                    else say("enregistre dans le FM-1");
                }
                loaded = 1; song_start(); underruns = 0; ack_cmd = 3; stat_req = 1;
            }
            draw_song();
            t_shown = 0;
        }
        if (ack_cmd >= 0) { midi_send(0xBFu, 117u, (uint32_t)ack_cmd); ack_cmd = -1; }
        if (ack_seq >= 0) { midi_send(0xBFu, 118u, (uint32_t)ack_seq); ack_seq = -1; }
        if (del_req) {                          /* la page retire un morceau (avant d'en envoyer un, faute de place) */
            uint32_t i, c = del_crc;
            del_req = 0;
            for (i = 0; i < lib.count && lib.e[i].crc != c; i++) {}
            if (i < lib.count && lib_ok()) { lib_delete(i); say("retire depuis la page"); ack_cmd = 6; }
            else ack_cmd = 0x46;
            t_shown = 0;
        }
        if (list_req && ack_cmd < 0) { list_req = 0; lib_send_list(); }
        if (map_req) {                          /* la page attribue les molettes et boutons */
            uint32_t i;
            map_req = 0;
            for (i = 0; i < MAP_ENC; i++) cmap.enc[i] = map_new[i];
            for (i = 0; i < MAP_BTN; i++) cmap.btn[i] = map_new[MAP_ENC + i];
            cmap.magic = MAP_MAGIC;
            ack_cmd = blob_save(1, &cmap, sizeof cmap) ? 0x48 : 7;
            say("Commandes changees depuis la page");
        }
        if (map_get && ack_cmd < 0) {
            static uint8_t f[6 + MAP_ENC + MAP_BTN];
            uint32_t i;
            map_get = 0;
            f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x52; f[3] = 0x53; f[4] = 0x18;
            for (i = 0; i < MAP_ENC; i++) f[5 + i] = cmap.enc[i];
            for (i = 0; i < MAP_BTN; i++) f[5 + MAP_ENC + i] = cmap.btn[i];
            f[5 + MAP_ENC + MAP_BTN] = 0xF7;
            rs_sysex_send(f, sizeof f);
        }
        if (stat_req && ack_cmd < 0) {          /* après l'accusé de fin : la page lit la place juste après */
            uint32_t ok = lib_ok() ? 1u : 0u;
            stat_req = 0;
            midi_send(0xBFu, 119u, ok ? lib.count : 0u);
            midi_send(0xBFu, 120u, ok ? LIB_NS - lib_free_sect() : 0u);
            midi_send(0xBFu, 121u, ok ? LIB_NS : 0u);   /* 0 : pas de bibliothèque (pas de flash) */
        }

        if (RING - (ring_w - ring_r) >= BLOCK) {  /* calcul : un bloc dès qu'il y a la place, mesuré */
            uint32_t t0 = fm1_ticks(), us, was = playing;
            render_block();
            us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
            if (playing && ring_w - ring_r >= RING - BLOCK) primed = 1;
            if (was) { sum += us; cnt++; }
        }

        b = fm1_input_edges(&rel);
        fx_controls(b, rel);
        if (b & (1u << BTN_PLAY) && loaded) {
            if (ended) song_start();
            else if (braked) { fx_resume(); playing = 1; }
            else playing = !playing;
            t_shown = 0;
        }
        /* REC : revenir au début ; maintenu 2 s : retirer le morceau de la bibliothèque */
        if (b & (1u << BTN_REC)) { rec_t = fm1_ms; rec_done = 0; }
        if (fm1_in.buttons >> BTN_REC & 1u && !rec_done && fm1_ms - rec_t >= 2000u && lib.count) {
            rec_done = 1;
            lib_delete(lib.cur);
            say("morceau retire");
            t_shown = 0;
        }
        if (rel & (1u << BTN_REC) && !rec_done && loaded) { uint32_t was = playing; song_start(); playing = was; t_shown = 0; }
        /* morceau suivant / précédent : OCT+ / OCT-, ou SELECT (on charge quand on a fini de tourner) */
        {
            int32_t e = fm1_enc_take(ENC_SELECT);
            if (e) { track_turn += e; track_turn_t = fm1_ms | 1u; }
        }
        if (track_turn && lib.count > 1u) {
            if (fm1_ms - track_turn_t >= 400u) {    /* 0,4 s après le dernier cran */
                int32_t n = (int32_t)lib.count, i = ((int32_t)lib.cur + track_turn % n + n) % n;
                track_turn = 0; track_turn_t = 0;
                open_lib((uint32_t)i, 1);
                t_shown = 0;
            }
        } else if (track_turn) track_turn = 0;
        fm1_led_key(BTN_PLAY, playing);

        if (rx_active) {                         /* réception : la progression, 4 fois par seconde */
            if (shown_rx == ~0u) { draw_song(); shown_rx = 0; }
            if (rx_got != shown_rx && fm1_ms - t_shown >= 250u) { shown_rx = rx_got; draw_time(0, 0); t_shown = fm1_ms; }
            continue;
        }
        if (shown_rx != ~0u) { shown_rx = ~0u; draw_song(); }
        if (fm1_ms - t_shown >= 500u) {          /* deux fois par seconde */
            if (cnt) pct = (sum / cnt * 100u + BUDGET_US / 2u) / BUDGET_US;
            draw_time(pct, underruns);
            t_shown = fm1_ms; sum = cnt = 0;
        }
    }
}
