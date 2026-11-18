// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : niveaux d'un WAV 16 bits stéréo par tranches (le lecteur sur PC, tests/run_host.sh).
//   node tests/wav_check.js FICHIER.wav début-fin:min ...   ex. 0.2-1.8:3000 (crête d'au moins 3000 entre 0,2 et 1,8 s)
//   (min 0 : silence attendu, crête 0 ; début-fin:min/max : crête entre les deux ; début-fin:~Hz : la fréquence, à 3 %
//   près, comptée aux passages par zéro autour de la moyenne)
'use strict';
const b = require('fs').readFileSync(process.argv[2]);
const rate = b.readUInt32LE(24), data = b.subarray(44), n = data.length >> 2;
let bad = 0;
for (const spec of process.argv.slice(3)) {
  const [t, lim] = spec.split(':'), [a, z] = t.split('-').map(Number), i0 = Math.floor(a * rate), i1 = Math.min(n, Math.floor(z * rate));
  let ok, what;
  if (lim[0] === '~') {
    let m = 0, c = 0, prev = 0;
    for (let i = i0; i < i1; i++) m += data.readInt16LE(i * 4);
    m /= Math.max(1, i1 - i0);
    for (let i = i0; i < i1; i++) { const s = data.readInt16LE(i * 4) > m ? 1 : -1; if (prev && s !== prev) c++; prev = s; }
    const hz = c / 2 / ((i1 - i0) / rate);
    ok = Math.abs(hz / +lim.slice(1) - 1) < 0.03; what = 'fréquence ' + hz.toFixed(1) + ' Hz';
  } else {
    const [min, max] = lim.split('/').map(Number);
    let peak = 0;
    for (let i = i0; i < i1; i++) peak = Math.max(peak, Math.abs(data.readInt16LE(i * 4)));
    ok = (min ? peak >= min : peak === 0) && (max === undefined || isNaN(max) || peak <= max); what = 'crête ' + peak;
  }
  console.log((spec + ' : ' + what).padEnd(40) + (ok ? 'ok' : 'FAIL'));
  if (!ok) bad++;
}
process.exit(bad ? 1 : 0);
