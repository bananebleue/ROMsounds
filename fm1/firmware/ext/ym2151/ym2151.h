/* ROMsounds : le YM2151 (OPM) de MAME, tel que dans libvgm (emu/cores/ym2151.c), sans le reste de libvgm */
#ifndef __YM2151_H__
#define __YM2151_H__
#include "ym2151_shim.h"
void *ym2151_init(UINT32 clock, UINT32 rate);	/* rate : clock / 64 (fréquence native) */
void ym2151_shutdown(void *chip);
void ym2151_reset_chip(void *chip);
void ym2151_write_reg(void *chip, UINT8 r, UINT8 v);
void ym2151_update_one(void *chip, UINT32 length, DEV_SMPL **buffers);
void ym2151_set_mute_mask(void *chip, UINT32 MuteMask);
#endif
