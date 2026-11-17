/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : banc de mesure. Une puce YM2612 (Mega Drive) simulée par Nuked-OPN2, dont on mesure le coût sur le FM-1.
 *
 *   touches    jouent des notes sur les 6 voies de la puce (une voie par note, la plus ancienne reprise)
 *   PLAY       accord tenu sur les 6 voies (le cas le plus chargé) / arrêt
 *   OCT- OCT+  l'octave
 *   MASTER     le volume (platform.c)
 *   écran      temps de calcul d'un bloc de 256 trames (5,8 ms de son), la charge en %, le maximum, les coupures
 *
 * Le calcul se fait dans la boucle principale, pas dans l'interruption audio : un tampon circulaire entre les deux.
 * Si le calcul est trop lent, le son se coupe (compteur « coupures ») mais l'écran et les touches restent vivants.
 * Nuked-OPN2 (LGPL-2.1-or-later, firmware/ext/nuked-opn2) : simulation au cycle près, 24 cycles par échantillon,
 * 53 267 échantillons par seconde (horloge 7,67 MHz / 144), ramenés à 44,1 kHz.
 * Règles de la puce (pas de FPU, pas de division 64 bits) : nombres entiers seulement. */
static void app_idle(void);
#define LCD_IDLE app_idle
#define APP_NAME "ROMSOUNDS"                    /* ROMsounds (base Dinghy 0.5) */
#include "platform.c"
#include "../ext/nuked-opn2/ym3438.c"
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
static volatile uint32_t underruns;             /* blocs audio servis incomplets */

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t r = ring_r, avail = ring_w - r, i, m = avail < n ? avail : n;
    for (i = 0; i < m; i++, r++) {
        out[2u * i] += ring[(r & (RING - 1u)) * 2u];
        out[2u * i + 1u] += ring[(r & (RING - 1u)) * 2u + 1u];
    }
    ring_r = r;
    if (m < n && ring_w) underruns++;
}

/* ---- la puce */
static ym3438_t opn;
static int16_t last_l, last_r;
static uint32_t pos;                            /* position fractionnaire, Q16 : 53267 / 44100 par trame */
#define STEP 79164u                             /* 53267,0 * 65536 / 44100 */

static void opn_sample(void)                    /* un échantillon de la puce : 24 cycles */
{
    int32_t l = 0, r = 0;
    uint32_t k;
    Bit16s b[2];
    for (k = 0; k < 24u; k++) {
        OPN2_Clock(&opn, b);
        l += b[0];
        r += b[1];
    }
    l *= 5; r *= 5;                             /* gain : voir tests (WAV de tests/run_host.sh) */
    last_l = (int16_t)(l > 32767 ? 32767 : l < -32767 ? -32767 : l);
    last_r = (int16_t)(r > 32767 ? 32767 : r < -32767 ? -32767 : r);
}

static void opn_reg(uint32_t part, uint32_t reg, uint32_t val)   /* écrire un registre (part 0 : voies 1-3, 1 : 4-6) */
{
    OPN2_Write(&opn, part * 2u, (Bit8u)reg);
    opn_sample();
    OPN2_Write(&opn, part * 2u + 1u, (Bit8u)val);
    opn_sample();
}

static void render_block(void)                  /* 256 trames à 44,1 kHz dans le tampon */
{
    uint32_t i, w = ring_w;
    for (i = 0; i < BLOCK; i++, w++) {
        pos += STEP;
        while (pos >= 65536u) { pos -= 65536u; opn_sample(); }
        ring[(w & (RING - 1u)) * 2u] = last_l;
        ring[(w & (RING - 1u)) * 2u + 1u] = last_r;
    }
    ring_w = w;
}

/* Un son d'orgue clair, algorithme 4 (deux paires modulateur -> porteuse), le même sur les 6 voies */
static void opn_setup(void)
{
    static const uint8_t OP[4] = {0, 8, 4, 12};  /* OP1, OP2, OP3, OP4 */
    static const uint8_t TL[4] = {0x20, 0x08, 0x20, 0x08}, MUL[4] = {0x01, 0x01, 0x02, 0x01};
    uint32_t v, o;
    OPN2_Reset(&opn);
    OPN2_SetChipType(ym3438_mode_ym2612);
    opn_reg(0, 0x22, 0x00);                     /* LFO coupé */
    opn_reg(0, 0x27, 0x00);                     /* voie 3 normale */
    opn_reg(0, 0x2B, 0x00);                     /* DAC coupé */
    for (v = 0; v < 6u; v++) {
        uint32_t part = v / 3u, c = v % 3u;
        opn_reg(part, 0xB0 + c, 3u << 3 | 4u);  /* retour 3, algorithme 4 */
        opn_reg(part, 0xB4 + c, 0xC0);          /* gauche + droite */
        for (o = 0; o < 4u; o++) {
            uint32_t s = c + OP[o];
            opn_reg(part, 0x30 + s, MUL[o]);
            opn_reg(part, 0x40 + s, TL[o]);
            opn_reg(part, 0x50 + s, 0x1F);      /* attaque rapide */
            opn_reg(part, 0x60 + s, 0x05);
            opn_reg(part, 0x70 + s, 0x02);
            opn_reg(part, 0x80 + s, 0x26);      /* niveau de maintien 2, relâche 6 */
            opn_reg(part, 0x90 + s, 0x00);
        }
    }
}

static const uint16_t FNUM[12] = {617, 653, 692, 733, 777, 823, 872, 924, 979, 1037, 1099, 1164};   /* do .. si */
static uint8_t vnote[6], vage[6];
static uint32_t ages;

static void opn_key(uint32_t v, uint32_t on)
{
    opn_reg(0, 0x28, (on ? 0xF0u : 0u) | (v < 3u ? v : v + 1u));
}
static void note_on(uint32_t note)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, blk = note / 12u, f = FNUM[note % 12u];
    if (blk) blk--;                             /* MIDI 60 (do 4) -> bloc 4 */
    if (blk > 7u) blk = 7u;
    for (v = 0; v < 6u; v++)                     /* une voie libre, sinon la plus ancienne */
        if (!vnote[v]) { pick = v; break; }
        else if (vage[v] < best) { best = vage[v]; pick = v; }
    if (vnote[pick]) opn_key(pick, 0);
    vnote[pick] = (uint8_t)note;
    vage[pick] = (uint8_t)++ages;
    opn_reg(pick / 3u, 0xA4 + pick % 3u, blk << 3 | f >> 8);
    opn_reg(pick / 3u, 0xA0 + pick % 3u, f & 0xFFu);
    opn_key(pick, 1);
}
static void note_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < 6u; v++)
        if (vnote[v] == note) { opn_key(v, 0); vnote[v] = 0; }
}

/* ---- écran */
#define C_ACC RGB(255, 165, 0)                  /* orange ROMsounds */
#define C_TXT RGB(200, 200, 200)
#define C_OFF RGB(90, 90, 90)
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

static void app_idle(void) { }

static void draw_stats(uint32_t avg_us, uint32_t max_us, uint32_t cuts, uint32_t chord)
{
    char s[40], *p;
    uint32_t pct = (avg_us * 100u + BUDGET_US / 2u) / BUDGET_US;
    p = put_s(s, ""); p = put_u(p, pct); p = put_s(p, " %"); *p = 0;
    draw_text_box(0, 92, 240, &FONT_L, s, pct < 70u ? C_ACC : C_BAD, 1);
    p = put_s(s, "calcul "); p = put_u(p, avg_us); p = put_s(p, " us / "); p = put_u(p, BUDGET_US); *p = 0;
    draw_text_box(0, 136, 240, &FONT_S, s, C_TXT, 1);
    p = put_s(s, "max "); p = put_u(p, max_us); p = put_s(p, " us   coupures "); p = put_u(p, cuts); *p = 0;
    draw_text_box(0, 160, 240, &FONT_S, s, C_TXT, 1);
    draw_text_box(0, 208, 240, &FONT_S, chord ? "PLAY : accord en cours" : "PLAY : accord 6 voix", chord ? C_ACC : C_OFF, 1);
}

static void app_main(void)
{
    static const uint8_t CHORD[6] = {48, 55, 60, 64, 67, 72};
    uint32_t prev = 0, k, b, rel, chord = 0, t_shown = 0, sum = 0, cnt = 0, mx = 0, max_shown = 0;
    int32_t oct = 0;
    uint8_t held[27] = {0};

    opn_setup();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    draw_text_box(0, 56, 240, &FONT_S, "YM2612 Nuked OPN2  v" APP_VER, C_OFF, 1);
    draw_stats(0, 0, 0, 0);

    for (;;) {
        uint32_t keys;
        platform_poll();

        /* calcul : un bloc dès qu'il y a la place, mesuré */
        if (RING - (ring_w - ring_r) >= BLOCK) {
            uint32_t t0 = fm1_ticks(), us;
            render_block();
            us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
            sum += us; cnt++;
            if (us > mx) mx = us;
        }

        keys = fm1_in.notes;                    /* bit k : touche k (0 = fa 3 .. 26 = sol 5) */
        for (k = 0; k < 27u; k++) {
            if ((keys & ~prev) >> k & 1u) { held[k] = (uint8_t)(53 + 12 * oct + (int32_t)k); note_on(held[k]); }
            if ((prev & ~keys) >> k & 1u) note_off(held[k]);
            fm1_led_key(KEY_LED(k), keys >> k & 1u);
        }
        prev = keys;

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && oct > -2) oct--;
        if (b & (1u << BTN_OCT_UP) && oct < 2) oct++;
        if (b & (1u << BTN_PLAY)) {
            chord = !chord;
            for (k = 0; k < 6u; k++) { if (chord) note_on(CHORD[k]); else note_off(CHORD[k]); }
        }
        fm1_led_key(BTN_PLAY, chord);
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);

        if (fm1_ms - t_shown >= 500u && cnt) {  /* deux fois par seconde : la moyenne, le maximum depuis le départ */
            if (mx > max_shown) max_shown = mx;
            draw_stats(sum / cnt, max_shown, underruns, chord);
            t_shown = fm1_ms; sum = cnt = 0; mx = 0;
        }
    }
}
