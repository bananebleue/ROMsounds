/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : le cœur YM2612 porté (firmware/ext/fmopn) rend exactement ce que rend celui de libvgm (« GPGX »), dont
 * il vient. 5 s de son : 6 voies, LFO, SSG-EG sur un opérateur, rétroactions et algorithmes variés. La sortie brute
 * (gauche/droite, int32) est comparée par son SHA-256 à celle de libvgm c5c03a2 (2026-11-04), relevée à la mise en place.
 *   cc -O2 -Ifirmware/ext/fmopn -o build/fmopn_test tests/fmopn_test.c firmware/ext/fmopn/fmopn.c
 *   build/fmopn_test | sha256sum                  (attendu : tests/fmopn_test.sha256) */
#include <stdio.h>
#include <string.h>
#include "fmopn.h"
int main(void)
{
    void *chip = ym2612_init(NULL, 7670453, 7670453 / 144, NULL, NULL);
    static DEV_SMPL l[1024], r[1024];
    DEV_SMPL *b[2] = {l, r};
    int n;
    ym2612_reset_chip(chip);
#define R(p, rg, v) do { ym2612_write(chip, (p) * 2, (rg)); ym2612_write(chip, (p) * 2 + 1, (v)); } while (0)
  {
  static const unsigned char OP[4]={0,8,4,12},TL[4]={0x20,8,0x20,8},MUL[4]={1,1,2,1};
  static const unsigned short F[6]={617,924,617,777,924,617}; static const unsigned char B[6]={3,3,4,4,4,5};
  R(0,0x22,0x0B); /* LFO on, to exercise lfo_pm_table */
  for(int v=0;v<6;v++){int p=v/3,c=v%3; R(p,0xB0+c,((v%7)<<3)|(v%8)); R(p,0xB4+c,0xC0|0x37);
    for(int o=0;o<4;o++){int s=c+OP[o]; R(p,0x30+s,MUL[o]|((o&3)<<4));R(p,0x40+s,TL[o]);R(p,0x50+s,0x1F);R(p,0x60+s,0x85);R(p,0x70+s,2);R(p,0x80+s,0x26);R(p,0x90+s,o==1?0x08:0);}
    R(p,0xA4+c,(B[v]<<3)|(F[v]>>8)); R(p,0xA0+c,F[v]&0xFF); R(0,0x28,0xF0|(v<3?v:v+1)); }
  }
    for (n = 0; n < 53267 * 5 / 1024; n++) {
        memset(l, 0, sizeof l); memset(r, 0, sizeof r);
        ym2612_update_one(chip, 1024, b);
        fwrite(l, 4, 1024, stdout); fwrite(r, 4, 1024, stdout);
    }
    return 0;
}
