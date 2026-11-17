// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : niveaux d'un WAV 16 bits stéréo par tranches (le lecteur sur PC, tests/run_host.sh).
//   node tests/wav_check.js FICHIER.wav début-fin:min ...   ex. 0.2-1.8:3000 (crête d'au moins 3000 entre 0,2 et 1,8 s)
//   (min 0 : silence attendu, crête 0)
'use strict';
const b = require('fs').readFileSync(process.argv[2]);
const rate = b.readUInt32LE(24), data = b.subarray(44), n = data.length >> 2;
let bad = 0;
for (const spec of process.argv.slice(3)) {
  const [t, min] = spec.split(':'), [a, z] = t.split('-').map(Number);
  let peak = 0;
  for (let i = Math.floor(a * rate); i < Math.min(n, Math.floor(z * rate)); i++) peak = Math.max(peak, Math.abs(data.readInt16LE(i * 4)));
  const ok = +min ? peak >= +min : peak === 0;
  console.log((spec + ' : crête ' + peak).padEnd(40) + (ok ? 'ok' : 'FAIL'));
  if (!ok) bad++;
}
process.exit(bad ? 1 : 0);
