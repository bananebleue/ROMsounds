// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : la page découpe un VGM en SysEx (web/romsounds.js + pack7 de fm1ota.js), et fm1/tests/rx_test.c les
// relit avec le code du firmware (fm1/firmware/src/rx.h) : le fichier doit revenir à l'identique.
//   node tests/rx_test.js FICHIER.vgm SORTIE.syx
'use strict';
const fs = require('fs');
const O = require('../web/fm1ota.js'), R = require('../web/romsounds.js');
const u = new Uint8Array(fs.readFileSync(process.argv[2]));
const v = R.parseVgm(u);
const fr = R.vgmFrames(v.body, 'Titre e accent ' + 'é', 'Jeu', O.pack7);
const all = [fr.begin, ...fr.data, fr.end];
for (const f of all) if (f.length - 2 > 640 || f.slice(1, -1).some(b => b > 127)) { console.error('trame invalide'); process.exit(1); }
fs.writeFileSync(process.argv[3], Buffer.from(all.flat()));
console.log('VGM ' + v.body.length + ' octets -> ' + all.length + ' SysEx (le plus long : ' + Math.max(...all.map(f => f.length)) + ' octets), CRC ' + R.crc32(v.body).toString(16));
