/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : la chaîne JieLi n'a pas de <stdlib.h> ; uzlib l'inclut sans s'en servir (NULL vient de <stddef.h>). */
#ifndef ROMSOUNDS_SHIM_STDLIB_H
#define ROMSOUNDS_SHIM_STDLIB_H
#include <stddef.h>
#endif
