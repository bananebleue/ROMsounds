// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : la bibliothèque vue par la page. Le lecteur sur PC (fm1/build/host/romsounds, tests/run_host.sh) reçoit
// A, B, C (fm1/build/*.rsm, tests/big_rsm.js), puis les SysEx de la page (ROMSOUNDS_SX) : la liste (05), un retrait
// (06) ; la page (web/romsounds.js) relit la liste et calcule la place pour D.
//   (dans fm1/) node ../tests/lib_test.js
'use strict';
const fs = require('fs'), { execFileSync } = require('child_process');
const R = require('../web/romsounds.js');
let bad = 0;
const check = (what, ok) => { console.log(what.padEnd(64) + (ok ? 'ok' : 'FAIL')); if (!ok) bad++; };
function run(sx) {
  fs.writeFileSync('build/lib_test.txt', '9000 end\n');
  const out = execFileSync('build/host/romsounds', ['build/host/lib_test', 'build/lib_test.txt'], {
    env: { ...process.env, ROMSOUNDS_RSM: 'build/A.rsm,build/B.rsm,build/C.rsm', ROMSOUNDS_EVERY: '2000', ROMSOUNDS_SX: sx } }).toString();
  const at = t => out.split('\n').filter(l => / SYSEX out /.test(l) && +l.trim().split(/\s+/)[0] >= t)
    .map(l => l.split('SYSEX out ')[1].trim().split(' ').map(h => parseInt(h, 16)));
  return { out, at };
}
const blob = n => new Uint8Array(fs.readFileSync('build/' + n + '.rsm'));
const list = '7D 52 53 05';
let r = run('7000:' + list);
let st = R.libFromFrames(r.at(7000));
check('liste : 3 morceaux, A B C', st && st.count === 3 && st.entries.map(e => e.title).join() === 'build/A.rsm,build/B.rsm,build/C.rsm');
check('liste : 25 + 10 + 26 secteurs sur 73, C en cours', st && st.used === 61 && st.total === 73 && st.cur === 2);
check('liste : CRC et taille de B', st && st.entries[1].crc === R.crc32(blob('B')) && st.entries[1].size === blob('B').length);
let pl = R.libPlan(st, blob('D'), []);
check('D (80 Ko) : il manque 8 secteurs, rien de coché : refusé', pl.deficit === 8 && !pl.ok);
pl = R.libPlan(st, blob('D'), [st.entries[1]]);
check('D : B coché (40 Ko) : accepté', pl.ok);
check('A renvoyé : déjà dans le FM-1', R.libPlan(st, blob('A'), []).dup);
const b = R.b35(R.crc32(blob('B'))).map(v => v.toString(16)).join(' ');
r = run('7000:7D 52 53 06 ' + b + ';7500:' + list);
st = R.libFromFrames(r.at(7500));
check('B retiré par la page : accusé 6', / MIDI out BF 117 6\b/.test(r.out));
check('liste : A, C ; 51 secteurs ; C toujours en cours', st && st.entries.map(e => e.title).join() === 'build/A.rsm,build/C.rsm' && st.used === 51 && st.cur === 1);
check('D entre maintenant sans rien retirer', R.libPlan(st, blob('D'), []).ok && !R.libPlan(st, blob('D'), []).deficit);
process.exit(bad ? 1 : 0);
