/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : ce dont ym2151.c (libvgm / MAME) a besoin, sans le reste de libvgm. */
#ifndef YM2151_SHIM_H
#define YM2151_SHIM_H
#include <stdint.h>
#include <stddef.h>
#if !defined(FMOPN_SHIM_H) && !defined(FMOPL_SHIM_H)	/* (les types communs : déjà là avec fmopn_shim.h ou fmopl_shim.h) */
typedef uint8_t UINT8;   typedef int8_t INT8;
typedef uint16_t UINT16; typedef int16_t INT16;
typedef uint32_t UINT32; typedef int32_t INT32;
typedef INT32 DEV_SMPL;
#endif
#ifndef INLINE
#define INLINE static inline
#endif
#endif
