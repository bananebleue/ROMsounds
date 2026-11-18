// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : le lecteur de prévisualisation de la page (web/preview.wasm, fm1/preview/preview.c) dans Node : un
// morceau préparé (RSM) rendu en WAV, à comparer au lecteur du firmware sur PC (tests/run_host.sh) ou à libvgm.
//   node tests/preview_test.js MORCEAU.rsm SECONDES SORTIE.wav
'use strict';
const fs = require('fs'), path = require('path');
(async () => {
  const [rsm, sec, outPath] = process.argv.slice(2);
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(path.join(__dirname, '../web/preview.wasm')), {});
  const x = instance.exports, mem = () => new Uint8Array(x.memory.buffer);
  const blob = fs.readFileSync(rsm);
  mem().set(blob, x.pv_buf());
  const e = x.pv_load(blob.length);
  if (e) throw new Error('pv_load : ' + e);
  const want = Math.round(+sec * 44100), pcm = Buffer.alloc(want * 4);
  let got = 0;
  while (got < want) {
    const n = x.pv_render(Math.min(4096, want - got));
    if (!n) break;
    Buffer.from(x.memory.buffer, x.pv_out(), n * 4).copy(pcm, got * 4);
    got += n;
  }
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + want * 4, 4); h.write('WAVEfmt ', 8); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20);
  h.writeUInt16LE(2, 22); h.writeUInt32LE(44100, 24); h.writeUInt32LE(176400, 28); h.writeUInt16LE(4, 32); h.writeUInt16LE(16, 34);
  h.write('data', 36); h.writeUInt32LE(want * 4, 40);
  fs.writeFileSync(outPath, Buffer.concat([h, pcm]));
  console.log(rsm + ' : ' + got + ' trames rendues (' + (got / 44100).toFixed(1) + ' s), joué ' + x.pv_played());
})().catch(e => { console.error('FAIL', e.message || e); process.exit(1); });
