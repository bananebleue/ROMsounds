/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : fige les tables du YM2151 (ym2151.c de MAME via libvgm) calculées en virgule flottante, à la fréquence
 * native de la puce (horloge / 64 : elles ne dépendent alors plus de l'horloge), pour le FM-1 qui n'a pas de FPU.
 *   cc -O2 -o build/gen_ym2151_tables tools/gen_ym2151_tables.c -lm && build/gen_ym2151_tables > firmware/ext/ym2151/ym2151_tables.h */
#define YM2151_GEN_TABLES
#include <stdio.h>
#include "../firmware/ext/ym2151/ym2151.c"

static void dump_i(const char *decl, const int *v, int n)
{
    int i;
    printf("%s = {", decl);
    for (i = 0; i < n; i++) printf("%s%d", i % 16 ? ", " : (i ? ",\n\t" : "\n\t"), v[i]);
    printf("\n};\n");
}
static void dump_u(const char *decl, const unsigned *v, int n)
{
    int i;
    printf("%s = {", decl);
    for (i = 0; i < n; i++) printf("%s%uu", i % 12 ? ", " : (i ? ",\n\t" : "\n\t"), v[i]);
    printf("\n};\n");
}
int main(void)
{
    static YM2151 c;
    c.clock = 3579545u; c.sampfreq = 3579545u / 64u;
    init_tables();
    init_chip_tables(&c);
    printf("/* Généré par tools/gen_ym2151_tables.c à partir de init_tables() et init_chip_tables() de ym2151.c\n"
           " * (MAME / libvgm, GPL-2.0-or-later), à la fréquence native. Ne pas modifier à la main. */\n");
    dump_i("static const signed int tl_tab[TL_TAB_LEN]", tl_tab, TL_TAB_LEN);
    dump_u("static const unsigned int sin_tab[SIN_LEN]", sin_tab, SIN_LEN);
    dump_u("static const UINT32 d1l_tab[16]", d1l_tab, 16);
    dump_u("static const UINT32 opm_freq[11*768]", c.freq, 11 * 768);
    dump_i("static const INT32 opm_dt1_freq[8*32]", c.dt1_freq, 8 * 32);
    dump_u("static const UINT32 opm_noise_tab[32]", c.noise_tab, 32);
    return 0;
}
