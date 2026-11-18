// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : la page prépare un VGM pour le FM-1 (web/romsounds.js : RSM7) et le découpe en SysEx (+ pack7 de
// fm1ota.js) ; fm1/tests/rx_test.c relit les SysEx avec le code du firmware (fm1/firmware/src/rx.h) : le RSM7 doit
// revenir à l'identique. Le RSM7 sert aussi au lecteur sur PC (ROMSOUNDS_RSM).
//   node tests/rx_test.js FICHIER.vgm SORTIE.syx SORTIE.rsm
'use strict';
const fs = require('fs');
const O = require('../web/fm1ota.js'), R = require('../web/romsounds.js');
(async () => {
  const u = new Uint8Array(fs.readFileSync(process.argv[2]));
  const v = await R.packVgm(u);
  const fr = R.vgmFrames(v.blob, 'Titre accentué', 'Jeu', O.pack7);
  const all = [fr.begin, ...fr.data, fr.end];
  for (const f of all) if (f.length - 2 > 640 || f.slice(1, -1).some(b => b > 127)) { console.error('trame invalide'); process.exit(1); }
  fs.writeFileSync(process.argv[3], Buffer.from(all.flat()));
  fs.writeFileSync(process.argv[4], v.blob);
  console.log('VGM ' + v.raw + ' octets -> RSM7 ' + v.blob.length + ' (PCM ' + v.pcmLen + ', A ' + v.aLen + ', B ' + v.bLen + ') -> '
    + all.length + ' SysEx (le plus long : ' + Math.max(...all.map(f => f.length)) + ' octets)');
  if (v.blob.length > R.VGM_MAX) { console.error('trop gros pour le FM-1'); process.exit(1); }
})().catch(e => { console.error(e); process.exit(1); });
