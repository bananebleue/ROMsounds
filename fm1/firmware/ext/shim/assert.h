/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : la chaîne JieLi n'a pas de <assert.h> ; les assert des bibliothèques reprises ne servent qu'au débogage. */
#ifndef ROMSOUNDS_SHIM_ASSERT_H
#define ROMSOUNDS_SHIM_ASSERT_H
#define assert(x) ((void)0)
#endif
