// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : un petit module Amiga de test (composé ici) : un échantillon carré de 32 octets en boucle, 4 voies.
//   ligne 0 : C-2 sur la voie 1 (période 428 : 3 546 895 / 428 / 32 = 259 Hz), vitesse 6, tempo 125 (8 lignes / s)
//   ligne 16 : C-3 sur la voie 2 (518 Hz), la voie 1 coupée (C00) ; ligne 32 : arpège 0x37 sur la voie 3 ;
//   ligne 48 : glissando de C-1 vers C-2 (259 Hz) sur la voie 4, coupée ligne 62 ; ligne 63 : B00, retour au début (7,68 s)
//   node tests/make_test_mod.js SORTIE.mod
'use strict';
const fs = require('fs');
const NCH = 4, u = new Uint8Array(1084 + 64 * 4 * NCH + 32);
u.set(Buffer.from('ROMsounds test'), 0);
const s1 = 20;                                    // échantillon 1 : 32 octets, volume 64, boucle 0..32
u.set(Buffer.from('carre'), s1);
u[s1 + 22] = 0; u[s1 + 23] = 16; u[s1 + 24] = 0; u[s1 + 25] = 64; u[s1 + 26] = 0; u[s1 + 27] = 0; u[s1 + 28] = 0; u[s1 + 29] = 16;
u[950] = 1; u[951] = 0; u[952] = 0;                // une position : le motif 0
u.set(Buffer.from('M.K.'), 1080);
const put = (row, ch, smp, per, eff, prm) => {
  const o = 1084 + (row * NCH + ch) * 4;
  u[o] = (smp & 0xF0) | (per >> 8); u[o + 1] = per & 255; u[o + 2] = (smp & 15) << 4 | eff; u[o + 3] = prm;
};
put(0, 0, 1, 428, 0xF, 6); put(0, 1, 0, 0, 0xF, 125);
put(16, 0, 0, 0, 0xC, 0); put(16, 1, 1, 214, 0, 0);
put(32, 1, 0, 0, 0xC, 0); put(32, 2, 1, 428, 0, 0x37);
put(48, 2, 0, 0, 0xC, 0); put(48, 3, 1, 856, 0, 0); put(49, 3, 0, 428, 3, 8);
for (let r = 50; r < 62; r++) put(r, 3, 0, 0, 3, 0);   // (le portamento continue : 3 00 reprend la vitesse)
put(62, 3, 0, 0, 0xC, 0); put(63, 0, 0, 0, 0xB, 0);
const smp = 1084 + 64 * 4 * NCH;
for (let i = 0; i < 32; i++) u[smp + i] = i < 16 ? 100 : 156;   // (156 = -100 en signé)
fs.writeFileSync(process.argv[2], u);
console.log('module de test : ' + u.length + ' octets');
