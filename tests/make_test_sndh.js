// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : un petit fichier SNDH de test (Atari ST : du code 68000 assemblé à la main ici), pour fm1/sndh/sndh.c.
//   init : la voie A joue un son (période 240 : 2 000 000 / 16 / 240 = 521 Hz, volume 15), puis Xbtimer installe la
//          minuterie A (/200, 123 : 2 457 600 / 200 / 123 = 99,9 Hz) dont l'interruption inverse le volume de la voie B
//          (registre 9 : 0 <-> 15) et termine par bclr #5,$FFFA0F ;
//   play (TC50) : écrit un compteur dans le registre 2 à chaque appel. 100 trames (2 s).
//   node tests/make_test_sndh.js SORTIE.sndh
'use strict';
const fs = require('fs');
const b = [];
const w16 = (...v) => { for (const x of v) b.push(x >> 8 & 255, x & 255); };
const str = s => { for (const c of s) b.push(c.charCodeAt(0)); b.push(0); };
w16(0x6000, 0); w16(0x6000, 0); w16(0x6000, 0);           // bra.w init, exit, play (corrigés plus bas)
for (const c of 'SNDH') b.push(c.charCodeAt(0));
str('TITLtest'); str('COMMROMsounds'); str('TC50');
for (const c of 'FRMS') b.push(c.charCodeAt(0)); b.push(0, 0, 0, 100);
for (const c of 'HDNS') b.push(c.charCodeAt(0));
if (b.length & 1) b.push(0);
const bra = (at, to) => { const d = to - (at + 2); b[at + 2] = d >> 8 & 255; b[at + 3] = d & 255; };
const init = b.length;
w16(0x41F9, 0x00FF, 0x8800);                              // lea $FF8800,a0
const ym = (r, v) => w16(0x10BC, r, 0x117C, v, 0x0002);   // move.b #r,(a0) ; move.b #v,2(a0)
ym(7, 0x3E); ym(0, 0xF0); ym(1, 0); ym(8, 15); ym(9, 0);
const pea = b.length; w16(0x487A, 0);                     // pea handler(pc)
w16(0x3F3C, 123, 0x3F3C, 7, 0x3F3C, 0, 0x3F3C, 31);      // move.w #123/#7/#0/#31,-(sp)
w16(0x4E4E, 0x4FEF, 0x000C, 0x4E75);                      // trap #14 ; lea 12(sp),sp ; rts
const handler = b.length;
w16(0x13FC, 0x0009, 0x00FF, 0x8800);                      // move.b #9,$FF8800
w16(0x0A38, 0x000F, 0x0700);                              // eori.b #15,$700.w
w16(0x13F8, 0x0700, 0x00FF, 0x8802);                      // move.b $700.w,$FF8802
w16(0x08B9, 0x0005, 0x00FF, 0xFA0F);                      // bclr #5,$FFFA0F
w16(0x4E73);                                              // rte
const exit = b.length; w16(0x4E75);                       // rts
const play = b.length;
w16(0x5238, 0x0701);                                      // addq.b #1,$701.w
w16(0x13FC, 0x0002, 0x00FF, 0x8800);                      // move.b #2,$FF8800
w16(0x13F8, 0x0701, 0x00FF, 0x8802);                      // move.b $701.w,$FF8802
w16(0x4E75);                                              // rts
bra(0, init); bra(4, exit); bra(8, play);
{ const d = handler - (pea + 2); b[pea + 2] = d >> 8 & 255; b[pea + 3] = d & 255; }
fs.writeFileSync(process.argv[2], Uint8Array.from(b));
console.log('SNDH de test : ' + b.length + ' octets');
