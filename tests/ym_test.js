// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : les fichiers .ym de l'Atari ST lus par la page (web/romsounds.js : unlha, ymToVgm, packVgm). Les exemples
// de StSound (Arnaud Carré, licence MIT : github.com/arnaud-carre/StSound, YmSampleFiles) : chacun doit sortir avec son
// titre, sa durée et un morceau pour le FM-1 ; les formats de remix / tracker (MIX1, YMT1) : refusés proprement.
//   node tests/ym_test.js DOSSIER [SORTIE.rsm pour Scout.ym]
'use strict';
const fs = require('fs'), path = require('path');
const R = require('../web/romsounds.js');
const want = {
  'ANCOOL1.YM': ['', 92.0], 'ND-Loader.ym': ['Nostalgic-O-Demo Loader Song', 10.3], 'Scout.ym': ['Scout', 245.7],
  'VirtualEscape1.YM': ['Virtual Escape (Equinox) Int', 53.0], 'enchant1.ym': ['Enchanted Lands: Intro', 233.0],
  'jess1.ym': ['Camerto for acid band', 476.2], 'Union Tcb 2.ym': null, 'YMKNUCK.YM': null
};
let bad = 0;
(async () => {
  for (const [f, w] of Object.entries(want)) {
    const u = new Uint8Array(fs.readFileSync(path.join(process.argv[2], f)));
    let ok, what;
    try {
      const v = await R.packVgm(R.ymToVgm(u));
      what = (v.title || '(sans titre)') + ', ' + (v.total / 44100).toFixed(1) + ' s, ' + v.system + ', ' + v.blob.length + ' octets';
      ok = w && v.title === w[0] && Math.abs(v.total / 44100 - w[1]) < 0.1 && v.ay === 2000000 && v.blob.length < R.VGM_MAX;
      if (f === 'Scout.ym' && process.argv[3]) fs.writeFileSync(process.argv[3], v.blob);
    } catch (e) { what = 'refusé : ' + e.message; ok = !w; }
    console.log((f + ' : ' + what).padEnd(90) + (ok ? 'ok' : 'FAIL'));
    if (!ok) bad++;
  }
  process.exit(bad ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
