/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : ce dont fmopl.c (libvgm / MAME) a besoin, sans le reste de libvgm. YM3812 (OPL2) et YM3526 (OPL). */
#ifndef FMOPL_SHIM_H
#define FMOPL_SHIM_H
#include <stdint.h>
#include <stddef.h>
#ifndef FMOPN_SHIM_H                            /* (les types communs : déjà là si fmopn_shim.h est inclus) */
typedef uint8_t UINT8;   typedef int8_t INT8;
typedef uint16_t UINT16; typedef int16_t INT16;
typedef uint32_t UINT32; typedef int32_t INT32;
#ifndef INLINE
#define INLINE static inline
#endif
typedef INT32 DEV_SMPL;
typedef struct { void *chipInf; } DEV_DATA;
typedef void (*DEVCB_LOG)(void *userParam, void *source, UINT8 level, const char *message);
typedef struct { DEVCB_LOG func; void *source; void *param; } DEV_LOGGER;
static inline void dev_logger_set(DEV_LOGGER *l, void *s, DEVCB_LOG f, void *p) { l->func = f; l->source = s; l->param = p; }
#define emu_logf(...) ((void)0)
#define logerror(...) ((void)0)
#define SNDDEV_SELECT
#endif
#define SNDDEV_YM3812
#define SNDDEV_YM3526
#endif
