/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : ce dont fmopn.c (libvgm / MAME) a besoin, sans le reste de libvgm. YM2612 seulement. */
#ifndef FMOPN_SHIM_H
#define FMOPN_SHIM_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t UINT8;   typedef int8_t INT8;
typedef uint16_t UINT16; typedef int16_t INT16;
typedef uint32_t UINT32; typedef int32_t INT32;
#define INLINE static inline
typedef INT32 DEV_SMPL;
typedef struct { void *chipInf; } DEV_DATA;
typedef void (*DEVFUNC_WRITE_CLOCK)(void *info, UINT32 clock);
typedef void (*DEVFUNC_WRITE_A8D8)(void *info, UINT8 addr, UINT8 data);
typedef UINT8 (*DEVFUNC_READ_A8D8)(void *info, UINT8 addr);
typedef void (*DEVFUNC_CTRL)(void *info);
typedef void (*DEVCB_LOG)(void *userParam, void *source, UINT8 level, const char *message);
typedef void (*DEVCB_SRATE_CHG)(void *info, UINT32 newSRate);
typedef struct { DEVCB_LOG func; void *source; void *param; } DEV_LOGGER;
static inline void dev_logger_set(DEV_LOGGER *l, void *s, DEVCB_LOG f, void *p) { l->func = f; l->source = s; l->param = p; }
#ifdef FMOPN_IMPL
static inline int fmopn_abs(int v) { return v < 0 ? -v : v; }
#define abs fmopn_abs
#endif
#define emu_logf(...) ((void)0)
#define logerror(...) ((void)0)
#define SNDDEV_SELECT
#define SNDDEV_YM2612
#endif
