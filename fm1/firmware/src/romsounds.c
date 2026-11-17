/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : lecteur de musiques Mega Drive (fichiers VGM) sur le M-VAVE FM-1.
 *
 *   YM2612   fmopn.c (MAME / Genesis Plus GX 1.5.1 via libvgm, GPL-2.0-or-later, firmware/ext/fmopn), compilé à part en
 *            -O2 ; ~60 % du temps disponible sur le FM-1 (mesuré le 17/11/2026, 6 voies)
 *   SN76489  psg.h (ROMsounds)
 *   VGM      vgm.h (ROMsounds) : YM2612 + DAC (banque PCM, commandes 0x8n / 0xE0) + PSG, boucle
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
 * Le contenu (« RSM1 ») : en-tête de 40 octets, banque PCM brute, commandes avant la boucle (A) et depuis la boucle (B),
 * en deflate brut. À la fin de B, on reprend B : une boucle sans retour en arrière.
 * Le FM-1 répond à chacun par un contrôleur sur le canal 16 : CC 117 = 2 / 3 (début / fin acceptés ; au début, 2 dit
 * « format RSM1 », l'ancienne version répondait 1), 0x40 + code
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
#include "psg.h"
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
static uint8_t vgm_buf[VGM_MAX] __attribute__((section(".pool")));
static uint8_t dict[32768] __attribute__((section(".pool")));   /* fenêtre de la décompression */
static const uint8_t *part_a, *part_b;
static uint32_t len_a, len_b, cur_part;
static TINF_DATA inf;
static uint8_t obuf[256];
static uint32_t obuf_r, obuf_n, inf_done, part_waited;   /* part_waited : la partie en cours a déjà fait attendre */
static vgm_t song;
static char title[32], game[32];
static uint32_t loaded, ended, wait, played;    /* played : trames jouées (44,1 kHz) */
static void *ym;
static psg_t psg;

static void vgm_ym(uint32_t port, uint32_t reg, uint32_t val)
{
    ym2612_write(ym, (UINT8)(port * 2u), (UINT8)reg);
    ym2612_write(ym, (UINT8)(port * 2u + 1u), (UINT8)val);
}
static void vgm_psg(uint32_t val) { psg_write(&psg, val); }

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
    if (size < 40u || m[0] != 'R' || m[1] != 'S' || m[2] != 'M' || m[3] != '1') return 4;
    pcm = rd32(m + 20); a = rd32(m + 24); b = rd32(m + 28);
    if (40u + pcm + a + b != size || !a) return 4;
    song.ym_clock = rd32(m + 4); song.psg_clock = rd32(m + 8);
    song.total = rd32(m + 12); song.loop_samples = rd32(m + 16);
    song.warn = rd32(m + 32);
    song.pcm = m + 40; song.pcm_len = pcm;
    part_a = m + 40 + pcm; len_a = a;
    part_b = part_a + a; len_b = b;
    if (!song.ym_clock && !song.psg_clock) return 3;
    return 0;
}

/* le YM2612 calcule à sa propre fréquence (horloge / 144 : 53 267 Hz en NTSC), ramenée à 44,1 kHz */
static uint32_t ym_step, ym_pos;                /* Q16 : échantillons de la puce par trame */
static DEV_SMPL mbuf_l[320], mbuf_r[320];       /* [0] : le dernier échantillon du lot précédent */
static uint32_t mbuf_n;

static void song_start(void)
{
    uint32_t yc = song.ym_clock ? song.ym_clock : 7670453u, rate;
    if (yc > 9000000u) yc = 9000000u;
    rate = yc / 144u;
    ym = ym2612_init(NULL, yc, rate, NULL, NULL);
    ym2612_reset_chip(ym);
    psg_init(&psg, song.psg_clock ? song.psg_clock : 3579545u);
    ym_step = (rate << 16) / 44100u;            /* (rate < 65536 : pas de débordement) */
    ym_pos = 0; mbuf_n = 0; mbuf_l[0] = mbuf_r[0] = 0;
    song.pcm_pos = 0;
    part_open(0);
    wait = 0; played = 0; ended = 0;
    primed = 0;
    playing = 1;
}

static void render_block(void)                  /* 256 trames à 44,1 kHz dans le tampon */
{
    static int32_t mix[BLOCK * 2u];
    uint32_t left = BLOCK, o = 0, i, w = ring_w;
    for (i = 0; i < BLOCK * 2u; i++) mix[i] = 0;
    while (left && playing) {
        uint32_t n, k = 0, j, p = ym_pos, kk = 0;
        DEV_SMPL *b[2] = {mbuf_l + 1, mbuf_r + 1};
        if (!wait && !(wait = vgm_next(&song))) { playing = 0; ended = 1; break; }
        part_waited = 1;
        n = wait < left ? wait : left;
        for (j = 0; j < n; j++) { p += ym_step; k += p >> 16; p &= 0xFFFFu; }
        mbuf_l[0] = mbuf_l[mbuf_n]; mbuf_r[0] = mbuf_r[mbuf_n];
        if (k) ym2612_update_one(ym, k, b);
        mbuf_n = k;
        for (j = 0; j < n; j++) {
            ym_pos += ym_step; kk += ym_pos >> 16; ym_pos &= 0xFFFFu;
            mix[2u * (o + j)] += mbuf_l[kk] << 1;   /* niveaux de libvgm (vgm2wav) : FM x 2, PSG : psg.h */
            mix[2u * (o + j) + 1u] += mbuf_r[kk] << 1;
        }
        psg_render(&psg, mix + 2u * o, n);
        wait -= n; left -= n; o += n; played += n;
    }
    for (i = 0; i < BLOCK; i++, w++) {
        int32_t l = mix[2u * i], r = mix[2u * i + 1u];
        ring[(w & (RING - 1u)) * 2u] = (int16_t)(l > 32767 ? 32767 : l < -32767 ? -32767 : l);
        ring[(w & (RING - 1u)) * 2u + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32767 ? -32767 : r);
    }
    ring_w = w;
}

/* ---- réception depuis la page (appelé par platform_poll) */
static uint32_t rx_size, rx_got, rx_active, load_req;
static volatile int32_t ack_cmd = -1, ack_seq = -1;

static void app_sysex(const uint8_t *p, uint32_t n)
{
    if (n < 4u || p[1] != 0x52u || p[2] != 0x53u) return;
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
        ack_cmd = 2;                            /* 2 : ce firmware attend un RSM1 (1 : l'ancien, un VGM brut) */
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

static void draw_song(void)
{
    lcd_fill(0, 80, 240, 160, C_BLACK);
    if (!loaded) {
        draw_text_box(0, 96, 240, &FONT_S, rx_active ? "Reception..." : "Aucun morceau", C_TXT, 1);
        draw_text_box(0, 120, 240, &FONT_S, rx_active ? "" : "Envoie un VGM depuis la page", C_OFF, 1);
        return;
    }
    draw_text_box(0, 84, 240, &FONT_S, title[0] ? title : "(sans titre)", C_TXT, 1);
    draw_text_box(0, 106, 240, &FONT_S, game, C_OFF, 1);
    if (song.warn)
        draw_text_box(0, 178, 240, &FONT_S, song.warn & VGM_W_DACSTREAM ? "flux DAC non pris en charge" : "autres puces ignorees",
                      C_BAD, 1);
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
    draw_text_box(0, 136, 240, &FONT_L, s, playing ? C_ACC : C_OFF, 1);
    p = put_s(s, "calcul "); p = put_u(p, pct); p = put_s(p, " %   coupures "); p = put_u(p, cuts); *p = 0;
    draw_text_box(0, 200, 240, &FONT_S, s, pct < 85u ? C_OFF : C_BAD, 1);
    draw_text_box(0, 220, 240, &FONT_S, ended ? "fin - PLAY : rejouer" : playing ? "PLAY pause  REC debut"
                                                                              : "PLAY lecture  REC debut", C_OFF, 1);
}

#ifdef DINGHY_HOST                              /* sur PC : ROMSOUNDS_RSM=fichier.rsm joue ce morceau au démarrage */
static void rs_host_load(void)
{
    const char *f = getenv("ROMSOUNDS_RSM");    /* un morceau préparé par la page (tests/rx_test.js) */
    FILE *fp = f ? fopen(f, "rb") : NULL;
    if (!fp) return;
    rx_size = (uint32_t)fread(vgm_buf, 1, VGM_MAX, fp);
    fclose(fp);
    rx_got = rx_size;
    load_req = 1;
    strcpy(title, "Test ROMsounds");
    strcpy(game, "(synthese)");
}
#endif

static void app_main(void)
{
    uint32_t b, rel, t_shown = 0, sum = 0, cnt = 0, pct = 0, shown_rx = ~0u;

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    draw_text_box(0, 56, 240, &FONT_S, "Mega Drive  v" APP_VER, C_OFF, 1);
    draw_song();
#ifdef DINGHY_HOST
    rs_host_load();
#endif

    for (;;) {
        platform_poll();

        if (load_req) {                          /* morceau reçu : l'ouvrir et répondre à la page */
            int e;
            load_req = 0;
            e = song_load(rx_size);
            if (e) { loaded = 0; ack_cmd = 0x48 + e; }
            else { loaded = 1; song_start(); underruns = 0; ack_cmd = 3; }
            draw_song();
            t_shown = 0;
        }
        if (ack_cmd >= 0) { midi_send(0xBFu, 117u, (uint32_t)ack_cmd); ack_cmd = -1; }
        if (ack_seq >= 0) { midi_send(0xBFu, 118u, (uint32_t)ack_seq); ack_seq = -1; }

        if (RING - (ring_w - ring_r) >= BLOCK) {  /* calcul : un bloc dès qu'il y a la place, mesuré */
            uint32_t t0 = fm1_ticks(), us, was = playing;
            render_block();
            us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
            if (playing && ring_w - ring_r >= RING - BLOCK) primed = 1;
            if (was) { sum += us; cnt++; }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_PLAY) && loaded) {
            if (ended) song_start();
            else playing = !playing;
            t_shown = 0;
        }
        if (b & (1u << BTN_REC) && loaded) { uint32_t was = playing; song_start(); playing = was; t_shown = 0; }
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
