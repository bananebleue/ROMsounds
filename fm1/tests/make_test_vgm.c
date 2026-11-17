/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : fabrique un petit fichier VGM de test (composé ici, aucune musique de jeu) qui exerce le lecteur :
 * YM2612 (6 voies FM, deux ports), PSG (voies carrées + bruit), DAC (bloc de données PCM + commandes 0x8n / 0xE0),
 * attentes 0x61 / 0x62 / 0x7n, et une boucle.
 *   cc -O2 -o build/make_test_vgm tests/make_test_vgm.c && build/make_test_vgm build/test.vgm [long]
 *   long : 60 s de DAC en plus dans la boucle (un fichier de plus de 300 Ko, comme les vraies pistes à batterie)
 *   stream : 2 s de flux DAC (VGM 1.60 : 0x90-0x95) en plus dans la boucle
 *   gg : un morceau Game Gear (SN76489 seul, stéréo), 4 s, sans boucle
 * Déroulé (secondes) : 0-2 accord FM, 2-3 PSG seul (3 voies), 3-4 bruit, 4-5 DAC (une rampe), puis boucle sur le PSG. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint8_t buf[1 << 21];
static uint32_t n = 0x40;

static void b(uint32_t v) { buf[n++] = (uint8_t)v; }
static void w32(uint32_t at, uint32_t v) { buf[at] = (uint8_t)v; buf[at + 1] = (uint8_t)(v >> 8); buf[at + 2] = (uint8_t)(v >> 16); buf[at + 3] = (uint8_t)(v >> 24); }
static void ym(uint32_t port, uint32_t reg, uint32_t val) { b(0x52 + port); b(reg); b(val); }
static void psg(uint32_t v) { b(0x50); b(v); }
static uint32_t total;
static void wait(uint32_t s)
{
    total += s;
    while (s) {
        if (s == 735) { b(0x62); return; }
        if (s <= 16) { b(0x70 + s - 1); return; }
        { uint32_t k = s > 65535 ? 65535 : s; b(0x61); b(k & 255); b(k >> 8); s -= k; }
    }
}

int main(int argc, char **argv)
{
    static const uint8_t OP[4] = {0, 8, 4, 12}, TL[4] = {0x22, 0x0A, 0x22, 0x0A}, MUL[4] = {1, 1, 2, 1};
    static const uint16_t F[6] = {617, 777, 924, 617, 777, 924};
    static const uint8_t BL[6] = {3, 3, 3, 4, 4, 4};
    uint32_t v, o, i, pcm_at, loop_at;
    FILE *f;
    memcpy(buf, "Vgm ", 4);
    if (argc > 2 && argv[2][0] == 'g') {              /* gg : Game Gear (SN76489 seul, stéréo 0x4F), 4 s */
        w32(0x08, 0x151); w32(0x0C, 3579545); w32(0x34, 0x40 - 0x34);
        buf[0x28] = 0x09; buf[0x29] = 0x00; buf[0x2A] = 16;   /* bruit Sega */
        psg(0x80 | 0x0D); psg(0x0D); psg(0x90 | 1);
        psg(0xA0 | 0x09); psg(0x0A); psg(0xB0 | 3);
        b(0x4F); b(0xF0); wait(44100);                  /* tout à gauche */
        b(0x4F); b(0x0F); wait(44100);                  /* tout à droite */
        b(0x4F); b(0xFF); psg(0xE4); psg(0xF2); wait(44100);   /* partout, + bruit */
        psg(0x9F); psg(0xBF); psg(0xFF); wait(44100);
        b(0x66);
        w32(0x04, n - 4); w32(0x18, total);
        f = fopen(argv[1], "wb");
        if (!f) return 1;
        fwrite(buf, 1, n, f); fclose(f);
        printf("test VGM (Game Gear) : %u octets\n", n);
        return 0;
    }
    w32(0x08, argc > 2 && argv[2][0] == 's' ? 0x160 : 0x150);
    w32(0x0C, 3579545);                 /* SN76489 */
    w32(0x2C, 7670453);                 /* YM2612 */
    w32(0x34, 0x40 - 0x34);             /* données à 0x40 */
    /* PCM : une rampe de 2000 octets, dans un bloc de données type 0 */
    b(0x67); b(0x66); b(0x00); pcm_at = n; b(0); b(0); b(0); b(0);
    for (i = 0; i < 2000; i++) b(128 + (int)((i % 100) * 2) - 100);
    w32(pcm_at, 2000);
    /* FM : un orgue sur les 6 voies */
    ym(0, 0x22, 0); ym(0, 0x27, 0); ym(0, 0x2B, 0);
    for (v = 0; v < 6; v++) {
        uint32_t p = v / 3, c = v % 3;
        ym(p, 0xB0 + c, 3 << 3 | 4);
        ym(p, 0xB4 + c, 0xC0);
        for (o = 0; o < 4; o++) {
            uint32_t s = c + OP[o];
            ym(p, 0x30 + s, MUL[o]); ym(p, 0x40 + s, TL[o]); ym(p, 0x50 + s, 0x1F);
            ym(p, 0x60 + s, 0x05); ym(p, 0x70 + s, 0x02); ym(p, 0x80 + s, 0x26); ym(p, 0x90 + s, 0);
        }
        ym(p, 0xA4 + c, BL[v] << 3 | F[v] >> 8); ym(p, 0xA0 + c, F[v] & 255);
        ym(0, 0x28, 0xF0 | (v < 3 ? v : v + 1));
    }
    wait(88200);
    for (v = 0; v < 6; v++) ym(0, 0x28, v < 3 ? v : v + 1);
    /* PSG : trois voies (do, mi, sol) */
    loop_at = n;
    psg(0x80 | 0x0D); psg(0x0D); psg(0x90 | 2);       /* voie 1 : période 0x0D5 */
    psg(0xA0 | 0x09); psg(0x0A); psg(0xB0 | 4);
    psg(0xC0 | 0x0E); psg(0x08); psg(0xD0 | 6);
    for (i = 0; i < 60; i++) wait(735);               /* 1 s en trames de 1/60 */
    psg(0x9F); psg(0xBF); psg(0xDF);
    psg(0xE4); psg(0xF3);                              /* bruit blanc, période 0x10 */
    wait(44100);
    psg(0xFF);
    /* DAC : activer, puis un octet tous les 4 échantillons */
    ym(0, 0x2B, 0x80); ym(1, 0xB6, 0xC0);
    for (i = 0; i < 11025; i++) {
        if (i % 2000 == 0) { b(0xE0); b(0); b(0); b(0); b(0); }   /* la rampe, en boucle */
        b(0x84); total += 4;
    }
    ym(0, 0x2B, 0x00);
    if (argc > 2 && argv[2][0] == 's') {              /* stream : 2 s de flux DAC (VGM 1.60), comme libvgm les lit */
        ym(0, 0x2B, 0x80);
        b(0x90); b(0); b(0x02); b(0x00); b(0x2A);        /* flux 0 -> YM2612, port 0, registre 0x2A */
        b(0x91); b(0); b(0x00); b(1); b(0);              /* banque 0 (PCM du YM2612), pas 1 */
        b(0x92); b(0); b(0x11); b(0x2B); b(0); b(0);     /* 11025 Hz */
        b(0x95); b(0); b(0); b(0); b(0x01);              /* bloc 0, en boucle */
        wait(44100);
        b(0x93); b(0); b(0x00); b(0x02); b(0); b(0); b(0x02); b(0xF4); b(0x01); b(0); b(0);   /* depuis 512, 500 ms */
        wait(44100);
        b(0x94); b(0);
        ym(0, 0x2B, 0x00);
    } else if (argc > 2) {                             /* long : 60 s de DAC à ~11 kHz, des motifs qui varient */
        ym(0, 0x2B, 0x80);
        for (i = 0; i < 60u * 11025u; i++) {
            if (i % 2000 == 0) { b(0xE0); b((i / 2000) % 7 * 200); b(0); b(0); b(0); }
            b(0x84); total += 4;
            if (n > sizeof buf - 64) break;
        }
        ym(0, 0x2B, 0x00);
    }
    wait(4410);
    b(0x66);
    w32(0x04, n - 4);
    w32(0x18, total);
    w32(0x1C, loop_at - 0x1C);
    w32(0x20, total - 88200);
    f = fopen(argc > 1 ? argv[1] : "test.vgm", "wb");
    if (!f) return 1;
    fwrite(buf, 1, n, f);
    fclose(f);
    printf("test VGM : %u octets, %u trames\n", n, total);
    return 0;
}
