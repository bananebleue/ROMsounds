/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : fabrique un petit fichier VGM de test (composé ici, aucune musique de jeu) qui exerce le lecteur :
 * YM2612 (6 voies FM, deux ports), PSG (voies carrées + bruit), DAC (bloc de données PCM + commandes 0x8n / 0xE0),
 * attentes 0x61 / 0x62 / 0x7n, et une boucle.
 *   cc -O2 -o build/make_test_vgm tests/make_test_vgm.c && build/make_test_vgm build/test.vgm [long]
 *   long : 60 s de DAC en plus dans la boucle (un fichier de plus de 300 Ko, comme les vraies pistes à batterie)
 *   stream : 2 s de flux DAC (VGM 1.60 : 0x90-0x95) en plus dans la boucle
 *   gg : un morceau Game Gear (SN76489 seul, stéréo), 4 s, sans boucle
 *   gb : un morceau Game Boy (4 voies, enveloppe, balayage, longueur, panoramique), 6 s, boucle sur les 2 dernières
 *   ay : un morceau YM2149 (Atari ST, 2 MHz) : voie A, voie B en enveloppe, bruit sur C, puis les trois (boucle), 5 s
 *   opl : un morceau OPL2 / AdLib (YM3812, 3,58 MHz) : sinus 440 Hz, FM 880 Hz, percussions, puis un accord (boucle), 5 s
 *   opl3 : le même pour un OPL3 (YMF262, 14,3 MHz, banque 0, stéréo) : la page le joue en OPL2, même son
 *   neogeo : un morceau YM2610 (Neo-Geo, 8 MHz) : FM 440 Hz, SSG 1 kHz, ADPCM-A (500 Hz, à 1 Mo dans une ROM de 2 Mo
 *            pleine d'octets inutiles), ADPCM-B (926 Hz, en boucle), puis le tout (boucle), 6 s
 *   arcade : un morceau CPS1 (YM2151 à 3,58 MHz, OKI MSM6295 à 1 MHz, ROM de 256 Ko avec sa table des phrases) : FM
 *            440 Hz, phrase 1 (sinus 500 Hz), phrase 5 (bruit, au loin dans la ROM), puis FM + phrase 1 (boucle), 4 s ;
 *            arcadeb : le même, la ROM en deux banques de 256 Ko (registre 0x0F), la phrase 5 dans la seconde
 *   opn : un morceau YM2203 (4 MHz) : FM 440 Hz, SSG 1 kHz, puis les deux (boucle), 4 s
 *   nes : un morceau NES (2 carrées dont une qui balaye, triangle, bruit, DPCM), 6 s, boucle sur les 2 dernières
 * Déroulé (secondes) : 0-2 accord FM, 2-3 PSG seul (3 voies), 3-4 bruit, 4-5 DAC (une rampe), puis boucle sur le PSG. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

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

static void gbw(uint32_t reg, uint32_t val) { b(0xB3); b(reg - 0xFF10u); b(val); }
static void nesw(uint32_t reg, uint32_t val) { b(0xB4); b(reg - 0x4000u); b(val); }

static int finish(const char *path, const char *what, uint32_t loop_at, uint32_t loop_len)
{
    FILE *f;
    b(0x66);
    w32(0x04, n - 4); w32(0x18, total);
    if (loop_at) { w32(0x1C, loop_at - 0x1C); w32(0x20, loop_len); }
    f = fopen(path, "wb");
    if (!f) return 1;
    fwrite(buf, 1, n, f); fclose(f);
    printf("test VGM (%s) : %u octets, %u trames\n", what, n, total);
    return 0;
}

static int game_boy(const char *path)
{
    uint32_t i, loop_at;
    n = 0xC0; w32(0x08, 0x161); w32(0x34, 0xC0 - 0x34); w32(0x80, 4194304);
    gbw(0xFF26, 0x80); gbw(0xFF24, 0x77); gbw(0xFF25, 0xFF);
    gbw(0xFF11, 0x80); gbw(0xFF12, 0xF0); gbw(0xFF13, 1750 & 255); gbw(0xFF14, 0x80 | 1750 >> 8);   /* voie 1 : 440 Hz */
    wait(44100);
    gbw(0xFF12, 0x00); gbw(0xFF14, 0x80);                                       /* (coupée par le DAC) */
    gbw(0xFF16, 0x40); gbw(0xFF17, 0xF3); gbw(0xFF18, 1899 & 255); gbw(0xFF19, 0x80 | 1899 >> 8);  /* voie 2 : enveloppe qui décroît */
    wait(44100);
    gbw(0xFF1A, 0x00);
    for (i = 0; i < 16; i++) gbw(0xFF30 + i, i < 8 ? i * 0x22 + 0x01 : 0xFE - (i - 8) * 0x22);         /* onde : un triangle */
    gbw(0xFF1A, 0x80); gbw(0xFF1C, 0x20); gbw(0xFF1D, 1798 & 255); gbw(0xFF1E, 0x80 | 1798 >> 8);   /* voie 3 */
    wait(44100);
    gbw(0xFF1A, 0x00);
    gbw(0xFF21, 0xF1); gbw(0xFF22, 0x45); gbw(0xFF23, 0x80);                    /* voie 4 : bruit qui décroît */
    wait(44100);
    loop_at = n;
    gbw(0xFF25, 0x12);                                                          /* voie 1 à droite, voie 2 à gauche */
    gbw(0xFF10, 0x16); gbw(0xFF11, 0xC0); gbw(0xFF12, 0xA0); gbw(0xFF13, 1500 & 255); gbw(0xFF14, 0x80 | 1500 >> 8);   /* balayage */
    gbw(0xFF16, 0x00); gbw(0xFF17, 0xC0); gbw(0xFF18, 1650 & 255); gbw(0xFF19, 0xC0 | 1650 >> 8);   /* longueur : 1/4 s */
    for (i = 0; i < 120; i++) wait(735);
    gbw(0xFF25, 0xFF); gbw(0xFF12, 0x00); gbw(0xFF14, 0x80); gbw(0xFF17, 0x00); gbw(0xFF19, 0x80);
    return finish(path, "Game Boy", loop_at, 88200);
}

static void ayw(uint32_t reg, uint32_t val) { b(0xA0); b(reg); b(val); }

static int ym2149(const char *path)
{
    uint32_t i, loop_at;
    n = 0xC0; w32(0x08, 0x161); w32(0x34, 0xC0 - 0x34); w32(0x74, 2000000); buf[0x78] = 0x10;   /* YM2149 */
    ayw(0, 284 & 255); ayw(1, 284 >> 8); ayw(8, 15); ayw(7, 0x3E);          /* voie A : 440 Hz */
    wait(44100);
    ayw(8, 0); ayw(2, 142); ayw(3, 0); ayw(9, 0x10); ayw(11, 0x00); ayw(12, 0x10); ayw(13, 0x0C); ayw(7, 0x3D);   /* B : 880 Hz, dents de scie */
    wait(44100);
    ayw(9, 0); ayw(6, 8); ayw(10, 12); ayw(7, 0x1F);                         /* C : bruit */
    wait(44100);
    loop_at = n;
    ayw(0, 0x00); ayw(1, 2); ayw(2, 0x7F); ayw(3, 1); ayw(4, 0xAA); ayw(5, 1);   /* les trois, enveloppe triangle */
    ayw(8, 0x10); ayw(9, 13); ayw(10, 0x10); ayw(11, 0x80); ayw(12, 0x08); ayw(13, 0x0E); ayw(7, 0x38);
    for (i = 0; i < 100; i++) wait(882);
    ayw(7, 0x3F);
    return finish(path, "YM2149", loop_at, 88200);
}

static int opl3_mode;                                     /* opl3 : les mêmes écritures, pour un YMF262 (banque 0) */
static void oplw(uint32_t reg, uint32_t val)
{
    if (opl3_mode && reg >= 0xC0 && reg <= 0xC8) val |= 0x30;   /* (OPL3 : gauche + droite) */
    b(opl3_mode ? 0x5E : 0x5A); b(reg); b(val);
}
static void opl_voice(uint32_t ch, uint32_t mod_tl, uint32_t fnum, uint32_t blk)
{
    static const uint8_t OFS[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
    uint32_t m = OFS[ch], c = OFS[ch] + 3u;
    oplw(0x20 + m, 0x01); oplw(0x20 + c, 0x01);           /* multiplicateur 1 */
    oplw(0x40 + m, mod_tl); oplw(0x40 + c, 0x00);
    oplw(0x60 + m, 0xF2); oplw(0x60 + c, 0xF2);           /* attaque rapide */
    oplw(0x80 + m, 0x05); oplw(0x80 + c, 0x05);
    oplw(0xC0 + ch, 0x00);                                /* FM, sans réinjection */
    oplw(0xA0 + ch, fnum & 255); oplw(0xB0 + ch, 0x20 | blk << 2 | fnum >> 8);
}
static int opl2(const char *path)
{
    uint32_t loop_at;
    n = 0x80; w32(0x08, 0x151); w32(0x34, 0x80 - 0x34);
    if (opl3_mode) { w32(0x5C, 14318180); b(0x5F); b(0x05); b(0x01); }   /* OPL3 : mode « NEW » */
    else w32(0x50, 3579545);
    oplw(0x01, 0x20);                                     /* formes d'onde permises */
    opl_voice(0, 0x3F, 580, 4); wait(44100);              /* sinus : 440 Hz (modulateur muet) */
    oplw(0xB0, 0x00);
    opl_voice(1, 0x18, 580, 5); wait(44100);              /* FM : 880 Hz */
    oplw(0xB1, 0x00);
    opl_voice(6, 0x10, 400, 2); oplw(0xB6, 2 << 2 | 400 >> 8);   /* percussions : voies 6-8 réglées, sans « key on » */
    opl_voice(7, 0x00, 700, 3); oplw(0xB7, 3 << 2 | 700 >> 8);
    opl_voice(8, 0x00, 900, 3); oplw(0xB8, 3 << 2 | 900 >> 8);
    oplw(0xBD, 0x20); oplw(0xBD, 0x39); wait(22050); oplw(0xBD, 0x20); oplw(0xBD, 0x39); wait(22050);   /* grosse caisse, caisse claire, charleston */
    oplw(0xBD, 0x00);
    loop_at = n;
    opl_voice(2, 0x3F, 580, 4); opl_voice(3, 0x3F, 731, 4); opl_voice(4, 0x3F, 869, 4);   /* la, do#, mi */
    wait(88200);
    oplw(0xB2, 0); oplw(0xB3, 0); oplw(0xB4, 0);
    return finish(path, "OPL2", loop_at, 88200);
}

/* YM2610 : FM (comme le YM2612, voie 1), SSG (registres 0-15 du port 0), ADPCM-A (port 1), ADPCM-B (port 0, 0x10-0x1B) */
static void nb(uint32_t port, uint32_t reg, uint32_t val) { b(0x58 + port); b(reg); b(val); }
static int jedi[49 * 16];
static uint32_t adpcm_a(uint8_t *out, uint32_t ns, double hz, double amp, int noise)   /* codeur ADPCM-A (glouton) */
{
    static const int steps[49] = {16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
        1411, 1552};
    static const int inc[8] = {-16, -16, -16, -16, 32, 80, 112, 144};
    int acc = 0, st = 0, k, nib, best, bv;
    uint32_t i, seed = 1;
    for (k = 0; k < 49 * 16; k++) { int v = (2 * (k & 7) + 1) * steps[k >> 4] / 8; jedi[k] = k & 8 ? -v : v; }
    for (i = 0; i < ns; i++) {
        int x;
        if (noise) { seed = seed * 1103515245u + 12345u; x = (int)((seed >> 16 & 2047) - 1024) * (int)(ns - i) / (int)ns; }
        else x = (int)(amp * sin(2 * M_PI * hz * i / 18518.5));
        best = 0; bv = 1 << 30;
        for (nib = 0; nib < 16; nib++) { int a = acc + jedi[st + nib], d = a > x ? a - x : x - a; if (a > 2047 || a < -2048) continue; if (d < bv) { bv = d; best = nib; } }
        acc += jedi[st + best]; st += inc[best & 7]; if (st < 0) st = 0; if (st > 48 * 16) st = 48 * 16;
        if (i & 1) out[i >> 1] |= (uint8_t)best; else out[i >> 1] = (uint8_t)(best << 4);
    }
    return (ns + 1) / 2;
}
static uint32_t adpcm_b(uint8_t *out, uint32_t ns, double hz, double rate, double amp)   /* codeur ADPCM-B (delta-T, glouton) */
{
    static const int B1[16] = {1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15};
    static const int B2[16] = {57, 57, 57, 57, 77, 102, 128, 153, 57, 57, 57, 57, 77, 102, 128, 153};
    int acc = 0, d = 127, nib, best, bv;
    uint32_t i;
    for (i = 0; i < ns; i++) {
        int x = (int)(amp * sin(2 * M_PI * hz * i / rate));
        best = 0; bv = 1 << 30;
        for (nib = 0; nib < 16; nib++) { int a = acc + B1[nib] * d / 8, e; if (a > 32767) a = 32767; if (a < -32768) a = -32768; e = a > x ? a - x : x - a; if (e < bv) { bv = e; best = nib; } }
        acc += B1[best] * d / 8; if (acc > 32767) acc = 32767; if (acc < -32768) acc = -32768;
        d = d * B2[best] / 64; if (d < 127) d = 127; if (d > 24576) d = 24576;
        if (i & 1) out[i >> 1] |= (uint8_t)best; else out[i >> 1] = (uint8_t)(best << 4);
    }
    return (ns + 1) / 2;
}
static void rom_block(uint32_t type, uint32_t rom_size, uint32_t at, const uint8_t *d, uint32_t len)
{
    uint32_t i;
    b(0x67); b(0x66); b(type); b((len + 8) & 255); b((len + 8) >> 8 & 255); b((len + 8) >> 16 & 255); b((len + 8) >> 24);
    b(rom_size & 255); b(rom_size >> 8 & 255); b(rom_size >> 16 & 255); b(rom_size >> 24);
    b(at & 255); b(at >> 8 & 255); b(at >> 16 & 255); b(at >> 24);
    for (i = 0; i < len; i++) b(d[i]);
}
static void nb_a(uint32_t ch, uint32_t start, uint32_t end)   /* ADPCM-A : voie ch, de start à end (octets), puis départ */
{
    nb(1, 0x08 + ch, 0xDF); nb(1, 0x10 + ch, start >> 8 & 255); nb(1, 0x18 + ch, start >> 16);
    nb(1, 0x20 + ch, (end - 1) >> 8 & 255); nb(1, 0x28 + ch, (end - 1) >> 16); nb(1, 0x00, 1u << ch);
}
static int neogeo(const char *path)
{
    static uint8_t smp[65536], junk[65536];
    uint32_t i, loop_at, la, lb, ln;
    n = 0x100; w32(0x08, 0x171); w32(0x34, 0x100 - 0x34); w32(0x4C, 8000000);
    for (i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37 + (i >> 8));
    la = adpcm_a(smp, 9259, 18518.5 / 37, 1400, 0); la = (la + 255) & ~255u;
    rom_block(0x82, 0x200000, 0x180000, junk, sizeof junk);                       /* des octets que rien ne joue */
    rom_block(0x82, 0x200000, 0x100000, smp, la);                                 /* le sinus, à 1 Mo */
    ln = adpcm_a(smp, 3704, 0, 0, 1); ln = (ln + 255) & ~255u;
    rom_block(0x82, 0x200000, 0x000200, smp, ln);                                 /* un bruit qui décroît (0,2 s) */
    lb = adpcm_b(smp, 11111, 18518.5 / 20, 18518.5, 12000); lb = (lb + 255) & ~255u;
    rom_block(0x83, 0x100000, 0x040000, smp, lb);                                 /* ADPCM-B : sinus 926 Hz */
    rom_block(0x83, 0x100000, 0x080000, junk, 4096);
    /* FM : voie 1 (registres de la voie 1 du port 0), un sinus à 440 Hz */
    nb(0, 0x22, 0); nb(0, 0x27, 0); nb(0, 0xB1, 7); nb(0, 0xB5, 0xC0);
    for (i = 0; i < 4; i++) {
        uint32_t s2 = 1 + (i == 1 ? 8 : i == 2 ? 4 : i == 3 ? 12 : 0);
        nb(0, 0x30 + s2, 1); nb(0, 0x40 + s2, i == 3 ? 0x08 : 0x7F); nb(0, 0x50 + s2, 0x1F);
        nb(0, 0x60 + s2, 0); nb(0, 0x70 + s2, 0); nb(0, 0x80 + s2, 0x0F); nb(0, 0x90 + s2, 0);
    }
    nb(0, 0xA5, 4 << 3 | 1038 >> 8); nb(0, 0xA1, 1038 & 255); nb(0, 0x28, 0xF1);
    wait(44100);
    nb(0, 0x28, 0x01);
    nb(0, 0x00, 125); nb(0, 0x01, 0); nb(0, 0x08, 15); nb(0, 0x07, 0x3E);          /* SSG A : 1 kHz */
    wait(44100);
    nb(0, 0x08, 0); nb(0, 0x07, 0x3F);
    nb(1, 0x01, 0x3F);                                                            /* ADPCM-A : volume général au plus */
    nb_a(0, 0x100000, 0x100000 + la); wait(22050);
    nb_a(0, 0x100000, 0x100000 + la); nb_a(1, 0x200, 0x200 + ln); wait(22050);
    nb(0, 0x11, 0xC0);
    nb(0, 0x12, 0x40000 >> 8 & 255); nb(0, 0x13, 0x40000 >> 16); nb(0, 0x14, (0x40000 + lb - 1) >> 8 & 255); nb(0, 0x15, (0x40000 + lb - 1) >> 16);
    nb(0, 0x19, 0x55); nb(0, 0x1A, 0x55); nb(0, 0x1B, 0xFF); nb(0, 0x10, 0x90);    /* ADPCM-B : départ, en boucle */
    wait(44100);
    nb(0, 0x10, 0x01); nb(0, 0x10, 0x00);
    loop_at = n;
    nb(0, 0x28, 0xF1); nb(0, 0x08, 12); nb(0, 0x07, 0x3E);
    for (i = 0; i < 4; i++) { nb_a(0, 0x100000, 0x100000 + la); wait(22050); }
    nb(0, 0x28, 0x01); nb(0, 0x08, 0); nb(0, 0x07, 0x3F);
    return finish(path, "Neo-Geo", loop_at, 88200);
}

/* arcade : YM2151 (commande 0x54), OKI MSM6295 (0xB8 ; bloc de données 0x8B) */
static void opm(uint32_t reg, uint32_t val) { b(0x54); b(reg); b(val); }
static void okw(uint32_t reg, uint32_t val) { b(0xB8); b(reg); b(val); }
static uint32_t oki_enc(uint8_t *out, uint32_t ns, double hz, double amp, int noise)   /* codeur ADPCM OKI (glouton) */
{
    static const int st[49] = {16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
        1411, 1552};
    static const int sh[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
    int sig = -2, k = 0, nib, best, bv;
    uint32_t i, seed = 7;
    for (i = 0; i < ns; i++) {
        int x;
        if (noise) { seed = seed * 1103515245u + 12345u; x = (int)((seed >> 16 & 2047) - 1024) * (int)(ns - i) / (int)ns; }
        else x = (int)(amp * sin(2 * M_PI * hz * i / 7575.76));
        best = 0; bv = 1 << 30;
        for (nib = 0; nib < 16; nib++) {
            int sv = st[k], d = sv / 8 + (nib & 4 ? sv : 0) + (nib & 2 ? sv / 2 : 0) + (nib & 1 ? sv / 4 : 0), a = sig + (nib & 8 ? -d : d), e;
            if (a > 2047) a = 2047;
            if (a < -2048) a = -2048;
            e = a > x ? a - x : x - a; if (e < bv) { bv = e; best = nib; }
        }
        { int sv = st[k], d = sv / 8 + (best & 4 ? sv : 0) + (best & 2 ? sv / 2 : 0) + (best & 1 ? sv / 4 : 0); sig += best & 8 ? -d : d; }
        if (sig > 2047) sig = 2047;
        if (sig < -2048) sig = -2048;
        k += sh[best & 7]; if (k < 0) k = 0; if (k > 48) k = 48;
        if (i & 1) out[i >> 1] |= (uint8_t)best; else out[i >> 1] = (uint8_t)(best << 4);
    }
    return (ns + 1) / 2;
}
static void oki_phrase(uint8_t *rom, uint32_t ph, uint32_t start, uint32_t len)
{
    uint32_t e = start + len - 1, t = ph * 8;
    rom[t] = start >> 16 & 3; rom[t + 1] = start >> 8 & 255; rom[t + 2] = start & 255;
    rom[t + 3] = e >> 16 & 3; rom[t + 4] = e >> 8 & 255; rom[t + 5] = e & 255;
}
static int arcade(const char *path, int banked)
{
    static uint8_t rom[0x80000];
    uint32_t i, loop_at, l1, l5;
    n = 0x100; w32(0x08, 0x171); w32(0x34, 0x100 - 0x34); w32(0x30, 3579545); w32(0x98, 1000000u | 0x80000000u);
    for (i = 0x400; i < sizeof rom; i++) rom[i] = (uint8_t)(i * 73 + (i >> 7));      /* des octets que rien ne joue */
    for (i = 0; i < 0x400; i++) rom[i] = 0;
    l1 = oki_enc(rom + 0x400, 3788, 7575.76 / 15, 1500, 0); oki_phrase(rom, 1, 0x400, l1);
    oki_phrase(rom, 2, 0x1000, 0xF000);                                              /* une phrase jamais jouée */
    if (banked) { memcpy(rom + 0x40000, rom, 0x400); for (i = 0; i < 8; i++) rom[0x40000 + 8 + i] = 0; }
    l5 = oki_enc(rom + (banked ? 0x40000 : 0) + 0x20000, 1515, 0, 0, 1); oki_phrase(rom + (banked ? 0x40000 : 0), 5, 0x20000, l5);
    b(0x67); b(0x66); b(0x8B); w32(n, (banked ? 0x80000 : 0x40000) + 8); n += 4;
    w32(n, banked ? 0x80000 : 0x40000); n += 4; w32(n, 0); n += 4;
    for (i = 0; i < (banked ? 0x80000u : 0x40000u); i++) b(rom[i]);
    /* YM2151 : voie 0, un seul opérateur (C2) qui sonne, 440 Hz */
    opm(0x20, 0xC7); opm(0x28, 0x4A); opm(0x30, 0);
    for (i = 0; i < 4; i++) {
        uint32_t sl = i * 8;
        opm(0x40 + sl, 1); opm(0x60 + sl, i == 3 ? 0x08 : 0x7F); opm(0x80 + sl, 31); opm(0xA0 + sl, 0); opm(0xC0 + sl, 0); opm(0xE0 + sl, 0x0F);
    }
    opm(0x08, 0x78); wait(44100);
    opm(0x08, 0x00);
    okw(0, 0x81); okw(0, 0x10); wait(22050);                                         /* phrase 1, voix 1 */
    if (banked) okw(0x0F, 1);
    okw(0, 0x85); okw(0, 0x20); wait(22050);                                         /* phrase 5, voix 2 */
    if (banked) okw(0x0F, 0);
    loop_at = n;
    opm(0x08, 0x78);
    for (i = 0; i < 4; i++) { okw(0, 0x08); okw(0, 0x81); okw(0, 0x10); wait(22050); }
    opm(0x08, 0x00);
    return finish(path, banked ? "arcade, banques" : "arcade", loop_at, 88200);
}
static int ym2203(const char *path)
{
    uint32_t i, loop_at;
    n = 0x100; w32(0x08, 0x171); w32(0x34, 0x100 - 0x34); w32(0x44, 4000000);
    b(0x55); b(0x27); b(0);
    b(0x55); b(0xB0); b(7);
    for (i = 0; i < 4; i++) {
        uint32_t s2 = (i == 1 ? 8 : i == 2 ? 4 : i == 3 ? 12 : 0);
        b(0x55); b(0x30 + s2); b(1); b(0x55); b(0x40 + s2); b(i == 3 ? 0x08 : 0x7F); b(0x55); b(0x50 + s2); b(0x1F);
        b(0x55); b(0x60 + s2); b(0); b(0x55); b(0x70 + s2); b(0); b(0x55); b(0x80 + s2); b(0x0F); b(0x55); b(0x90 + s2); b(0);
    }
    b(0x55); b(0xA4); b(4 << 3 | 1038 >> 8); b(0x55); b(0xA0); b(1038 & 255); b(0x55); b(0x28); b(0xF0);
    wait(44100);
    b(0x55); b(0x28); b(0x00);
    b(0x55); b(0x00); b(125); b(0x55); b(0x01); b(0); b(0x55); b(0x08); b(15); b(0x55); b(0x07); b(0x3E);   /* SSG A : 1 kHz */
    wait(44100);
    loop_at = n;
    b(0x55); b(0x28); b(0xF0); b(0x55); b(0x08); b(12);
    wait(88200);
    b(0x55); b(0x28); b(0x00); b(0x55); b(0x08); b(0);
    return finish(path, "YM2203", loop_at, 88200);
}

static int nes(const char *path)
{
    uint32_t i, at, loop_at;
    n = 0xC0; w32(0x08, 0x161); w32(0x34, 0xC0 - 0x34); w32(0x84, 1789772);
    b(0x67); b(0x66); b(0xC2); at = n; b(0); b(0); b(0); b(0);                   /* DPCM : 1 Ko à $C000 */
    b(0x00); b(0xC0);
    for (i = 0; i < 1024; i++) b(i % 32 < 16 ? 0xFF : 0x00);
    w32(at, 1026);
    nesw(0x4017, 0x40);                          /* (pas de $4015 : beaucoup de vrais VGM NES n'en écrivent jamais) */
    nesw(0x4000, 0xBF); nesw(0x4001, 0x08); nesw(0x4002, 253); nesw(0x4003, 0x08);      /* carrée 1 : 440 Hz, 50 % */
    wait(44100);
    nesw(0x4000, 0xB0);
    nesw(0x4004, 0x4F); nesw(0x4005, 0x08); nesw(0x4006, 201); nesw(0x4007, 0x08);      /* carrée 2 : 25 %, enveloppe */
    wait(44100);
    nesw(0x4004, 0x70);
    nesw(0x4008, 0xFF); nesw(0x400A, 126); nesw(0x400B, 0x08);                          /* triangle : 440 Hz */
    wait(44100);
    nesw(0x4008, 0x80); nesw(0x400B, 0x08);
    nesw(0x400C, 0x38); nesw(0x400E, 0x08); nesw(0x400F, 0x08);                          /* bruit, volume 8 */
    wait(44100);
    loop_at = n;
    nesw(0x400C, 0x30);
    nesw(0x4010, 0x4F); nesw(0x4011, 0x40); nesw(0x4012, 0x00); nesw(0x4013, 0x3F);      /* DPCM en boucle */
    nesw(0x4015, 0x1F);
    nesw(0x4000, 0x9C); nesw(0x4001, 0xA3); nesw(0x4002, 120); nesw(0x4003, 0x01);       /* carrée 1 : balayage */
    for (i = 0; i < 120; i++) wait(735);
    nesw(0x4015, 0x00);
    return finish(path, "NES", loop_at, 88200);
}

int main(int argc, char **argv)
{
    static const uint8_t OP[4] = {0, 8, 4, 12}, TL[4] = {0x22, 0x0A, 0x22, 0x0A}, MUL[4] = {1, 1, 2, 1};
    static const uint16_t F[6] = {617, 777, 924, 617, 777, 924};
    static const uint8_t BL[6] = {3, 3, 3, 4, 4, 4};
    uint32_t v, o, i, pcm_at, loop_at;
    FILE *f;
    memcpy(buf, "Vgm ", 4);
    if (argc > 2 && !strcmp(argv[2], "gb")) return game_boy(argv[1]);
    if (argc > 2 && !strcmp(argv[2], "nes")) return nes(argv[1]);
    if (argc > 2 && !strcmp(argv[2], "ay")) return ym2149(argv[1]);
    if (argc > 2 && !strcmp(argv[2], "opl")) return opl2(argv[1]);
    if (argc > 2 && !strcmp(argv[2], "neogeo")) return neogeo(argv[1]);
    if (argc > 2 && !strcmp(argv[2], "arcade")) return arcade(argv[1], 0);
    if (argc > 2 && !strcmp(argv[2], "arcadeb")) return arcade(argv[1], 1);
    if (argc > 2 && !strcmp(argv[2], "opn")) return ym2203(argv[1]);
    if (argc > 2 && !strcmp(argv[2], "opl3")) { opl3_mode = 1; return opl2(argv[1]); }
    if (argc > 2 && !strcmp(argv[2], "gg")) {              /* gg : Game Gear (SN76489 seul, stéréo 0x4F), 4 s */
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
