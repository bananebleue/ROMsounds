// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : le lecteur SNDH de la page (web/sndh.wasm) dans Node, sur le fichier de tests/make_test_sndh.js : les
// écritures du YM (le son de la voie A, la minuterie A à 99,9 Hz sur le registre 9, play à 50 Hz sur le registre 2),
// identiques à celles du lecteur sur PC (build/sndh/sndh_test), puis le VGM et le morceau pour le FM-1.
//   node tests/sndh_test.js FICHIER.sndh ECRITURES_PC.txt SORTIE.rsm
'use strict';
const fs = require('fs'), path = require('path'), R = require('../web/romsounds.js');
(async () => {
  const [, , file, ref, out] = process.argv;
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(path.join(__dirname, '../web/sndh.wasm')), {});
  const e = instance.exports, u = new Uint8Array(fs.readFileSync(file)), m = R.parseSndh(u, 1);
  if (m.hz !== 50 || m.frames !== 100 || m.title !== 'test') throw new Error('en-tête mal lu : ' + JSON.stringify(m));
  new Uint8Array(e.memory.buffer, e.sndh_buf(), u.length).set(u);
  const n = e.sndh_run(u.length, 1, m.hz, m.frames);
  if (n < 0) throw new Error('sndh_run : ' + n);
  const T = new Uint32Array(e.memory.buffer, e.sndh_ev_t(), n).slice(), V = new Uint16Array(e.memory.buffer, e.sndh_ev_rv(), n).slice();
  let txt = '';
  for (let i = 0; i < n; i++) txt += T[i] + ' ' + (V[i] >> 8) + ' ' + (V[i] & 255) + '\n';
  if (txt !== fs.readFileSync(ref, 'utf8')) throw new Error('écritures différentes du lecteur sur PC');
  const per = r => { let c = 0; for (let i = 0; i < n; i++) if (V[i] >> 8 === r && T[i] < 88200) c++; return c; };
  const ok = (what, got, lo, hi) => { const g = got >= lo && got <= hi; console.log(what.padEnd(48) + got + (g ? '\tok' : '\tFAIL')); if (!g) process.exitCode = 1; };
  ok('play (registre 2), écritures en 2 s', per(2), 99, 101);
  ok('minuterie A (registre 9), écritures en 2 s', per(9), 197, 201);
  ok('voie A : période (registre 0)', V[T.findIndex((t, i) => V[i] >> 8 === 0)] & 255, 240, 240);
  const vgm = R.sndhVgm(m, T, V, Math.round(m.frames * 44100 / m.hz)), v = await R.packVgm(vgm);
  ok('durée (s)', Math.round(v.total / 441) / 100, 2, 2);
  fs.writeFileSync(out, v.blob);
})().catch(err => { console.error(err.message); process.exit(1); });
