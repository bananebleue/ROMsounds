/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : calcule sur PC les tables de fmopl.c (sinus, niveaux), qui demandent des calculs à virgule que
 * le FM-1 ne sait pas faire, et les écrit en C. Le firmware les lit telles quelles (firmware/ext/fmopl/fmopl_tables.h).
 *   cc -O2 -o build/gen_fmopl_tables tools/gen_fmopl_tables.c -lm && build/gen_fmopl_tables > firmware/ext/fmopl/fmopl_tables.h
 * Le CI vérifie que le fichier livré est bien celui que ce programme produit. */
#define FMOPL_GEN_TABLES
#include <math.h>
#include <stdio.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "../firmware/ext/fmopl/fmopl.c"

static void dump(const char *type, const char *name, const char *len, const long *v, int n)
{
    int i;
    printf("static const %s %s[%s] = {", type, name, len);
    for (i = 0; i < n; i++) printf("%s%ld,", i % 16 ? " " : "\n    ", v[i]);
    printf("\n};\n");
}

int main(void)
{
    static long v[4096 * 4];
    long lo, hi;
    int i;
    init_tables();
    printf("/* Généré par tools/gen_fmopl_tables.c à partir de init_tables() de fmopl.c (MAME / libvgm, GPL-2.0-or-later).\n"
           " * Ne pas modifier à la main. */\n");
    for (lo = hi = 0, i = 0; i < TL_TAB_LEN; i++) { v[i] = tl_tab[i]; if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
    printf("/* tl_tab : %ld .. %ld */\n", lo, hi);
    dump(lo >= -32768 && hi <= 32767 ? "INT16" : "INT32", "tl_tab", "TL_TAB_LEN", v, TL_TAB_LEN);
    for (lo = hi = 0, i = 0; i < SIN_LEN * 4; i++) { v[i] = sin_tab[i]; if (v[i] > hi) hi = v[i]; }
    printf("/* sin_tab : 0 .. %ld */\n", hi);
    dump(hi <= 65535 ? "UINT16" : "UINT32", "sin_tab", "SIN_LEN * 4", v, SIN_LEN * 4);
    return 0;
}
