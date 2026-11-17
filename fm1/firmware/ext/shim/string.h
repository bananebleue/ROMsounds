/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : la chaîne JieLi n'a pas de <string.h>. Le peu dont les bibliothèques reprises (firmware/ext/) ont
 * besoin, fourni par firmware/src/libc.c. Seulement pour la compilation de l'app (tools/build.py : -Ifirmware/ext/shim). */
#ifndef ROMSOUNDS_SHIM_STRING_H
#define ROMSOUNDS_SHIM_STRING_H
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
#endif
