// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : compare deux WAV 16 bits stéréo (le lecteur sur PC face à libvgm vgm2wav) par tranches, en niveau RMS.
//   node tests/wav_compare.js NOTRE.wav REFERENCE.wav [tranche_s] [durée_s] [écart_max_%]
//   (sans écart max : affiche seulement ; avec : échoue si une tranche assez forte s'écarte de plus que ça)
'use strict';
const fs = require('fs');
function load(p) {
  const b = fs.readFileSync(p);
  let o = 12, rate = 44100, data = null;
  while (o + 8 <= b.length) {
    const id = b.toString('ascii', o, o + 4), len = b.readUInt32LE(o + 4);
    if (id === 'fmt ') rate = b.readUInt32LE(o + 12);
    if (id === 'data') { data = b.subarray(o + 8, o + 8 + len); break; }
    o += 8 + len + (len & 1);
  }
  return { rate, data, n: data.length >> 2 };
}
const [A, B] = [load(process.argv[2]), load(process.argv[3])];
const step = +(process.argv[4] || 0.5), dur = +(process.argv[5] || Math.min(A.n / A.rate, B.n / B.rate)), tol = process.argv[6];
function rms(w, a, z, ch) {                   // sans la composante continue (libvgm n'a pas le filtre passe-haut de la NES)
  let s = 0, m = 0, k = 0;
  for (let i = Math.floor(a * w.rate); i < Math.min(w.n, Math.floor(z * w.rate)); i++, k++) {
    const x = w.data.readInt16LE(i * 4 + ch * 2);
    s += x * x; m += x;
  }
  return k ? Math.sqrt(Math.max(0, s / k - (m / k) ** 2)) : 0;
}
let bad = 0;
for (let t = 0; t + step <= dur + 1e-9; t += step) {
  const l = [rms(A, t, t + step, 0), rms(B, t, t + step, 0)], r = [rms(A, t, t + step, 1), rms(B, t, t + step, 1)];
  const e = [l, r].map(([x, y]) => y > 200 ? (x / y - 1) * 100 : null);
  const off = tol !== undefined && e.some(x => x !== null && Math.abs(x) > +tol);
  if (off) bad++;
  console.log(t.toFixed(2).padStart(6) + ' s  G ' + l.map(x => x.toFixed(0).padStart(6)).join(' / ') + (e[0] === null ? '' : ' (' + e[0].toFixed(1) + ' %)').padEnd(11)
    + '  D ' + r.map(x => x.toFixed(0).padStart(6)).join(' / ') + (e[1] === null ? '' : ' (' + e[1].toFixed(1) + ' %)') + (off ? '  FAIL' : ''));
}
process.exit(bad ? 1 : 0);
