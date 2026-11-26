// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : un petit VGM de test pour le Konami 053260 (composé ici) : un carré PCM 8 bits de 64 octets en boucle sur la
// voix 1 (hauteur 0xFC0 : 1 octet par échantillon de la puce, 3 579 545 / 64 / 64 = 874 Hz), centré, volume 32, pendant
// 1 s, puis coupé 0,5 s.   node tests/make_test_kdsc.js SORTIE.vgm
'use strict';
const fs = require('fs');
const h = new Uint8Array(0x100), dv = new DataView(h.buffer);
h.set(Buffer.from('Vgm ')); dv.setUint32(8, 0x161, true); dv.setUint32(0x34, 0x100 - 0x34, true);
dv.setUint32(0xAC, 3579545, true); dv.setUint32(0x18, 66150, true);
const rom = new Uint8Array(64); for (let i = 0; i < 64; i++) rom[i] = i < 32 ? 100 : 156;
const blk = [0x67, 0x66, 0x8E, ...le(8 + rom.length), ...le(0x200000), ...le(0), ...rom];
const w = (r, d) => [0xBA, r, d];
const cmds = [...blk, ...w(0x2F, 2), ...w(0x08, 0xC0), ...w(0x09, 0x0F), ...w(0x0A, 64), ...w(0x0B, 0), ...w(0x0C, 0), ...w(0x0D, 0), ...w(0x0E, 0),
  ...w(0x0F, 32), ...w(0x2C, 4), ...w(0x2A, 1), ...w(0x28, 1), 0x61, 0x44, 0xAC, ...w(0x28, 0), 0x61, 0x22, 0x56, 0x66];
function le(x) { return [x & 255, x >> 8 & 255, x >> 16 & 255, x >>> 24]; }
const out = new Uint8Array(0x100 + cmds.length); out.set(h); out.set(cmds, 0x100);
new DataView(out.buffer).setUint32(4, out.length - 4, true);
fs.writeFileSync(process.argv[2], out);
