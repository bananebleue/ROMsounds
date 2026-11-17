// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : un pack .zip (comme ceux de VGMRips) lu par la page (web/romsounds.js : zipList) : chaque .vgm / .vgz
// sort à l'identique et se prépare pour le FM-1.
//   node tests/zip_test.js PACK.zip FICHIER1 FICHIER2 ...   (les fichiers attendus, dans l'ordre du pack)
'use strict';
const fs = require('fs'), path = require('path');
const R = require('../web/romsounds.js');
(async () => {
  const [zip, ...want] = process.argv.slice(2);
  const list = R.zipList(new Uint8Array(fs.readFileSync(zip)));
  if (list.length !== want.length) throw new Error(list.length + ' morceaux dans le pack, ' + want.length + ' attendus');
  for (let i = 0; i < list.length; i++) {
    const got = await list[i].open(), ref = fs.readFileSync(want[i]);
    if (list[i].name !== path.basename(want[i]) || Buffer.compare(Buffer.from(got), ref)) throw new Error(list[i].name + ' : différent');
    const v = await R.packVgm(await R.gunzipIf(got));
    console.log((list[i].name + ' : ' + got.length + ' octets -> ' + v.blob.length + ' pour le FM-1').padEnd(60) + 'ok');
  }
})().catch(e => { console.error('FAIL', e.message || e); process.exit(1); });
