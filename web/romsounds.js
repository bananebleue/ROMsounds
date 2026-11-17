/* SPDX-License-Identifier: GPL-3.0-only
 * ROMsounds : lire un fichier VGM (en-tête, étiquettes GD3) et le découper en SysEx pour le FM-1.
 * Protocole : firmware/src/romsounds.c. Utilisé par index.html (navigateur) et tests/rx_test.js (Node). */
(function (root) {
'use strict';
const VGM_MAX = 300 * 1024;
const VGM_ERR = { 0x41: 'fichier trop gros pour le FM-1', 0x42: 'données hors du fichier', 0x43: 'données incomplètes',
  0x44: 'fichier incomplet', 0x45: 'contrôle CRC faux', 0x49: 'ce n’est pas un fichier VGM', 0x4A: 'fichier VGM trop court',
  0x4B: 'ni YM2612 ni SN76489 dans ce fichier' };
// horloges des autres puces dans l'en-tête (spécification VGM 1.71)
const OTHER_CHIPS = [0x30, 0x38, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x80,
  0x84, 0x88, 0x8C, 0x90, 0x98, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8, 0xDC, 0xE0];
const rd32 = (u, i) => (u[i] | u[i + 1] << 8 | u[i + 2] << 16 | u[i + 3] << 24) >>> 0;
// l'écran du FM-1 n'a que l'ASCII : accents retirés, le reste remplacé par « ? »
const ascii = t => (t || '').normalize('NFD').replace(/[̀-ͯ]/g, '').replace(/[^\x20-\x7E]/g, '?').trim().slice(0, 28);

function crc32(u) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < u.length; i++) { c ^= u[i]; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xEDB88320 & -(c & 1)); }
  return (~c) >>> 0;
}
const b35 = v => [v & 127, (v >>> 7) & 127, (v >>> 14) & 127, (v >>> 21) & 127, (v >>> 28) & 127];

/** u : le fichier VGM décompressé -> { body (sans les étiquettes), ym, sn, others, total, loop, title, game, ... } */
function parseVgm(u) {
  if (u.length < 0x40 || String.fromCharCode(u[0], u[1], u[2], u[3]) !== 'Vgm ') throw new Error('ce n’est pas un fichier VGM');
  const ver = rd32(u, 8), sn = rd32(u, 0x0C) & 0x3FFFFFFF, ym = (ver >= 0x110 ? rd32(u, 0x2C) : rd32(u, 0x10)) & 0x3FFFFFFF;
  const start = ver >= 0x150 && rd32(u, 0x34) ? 0x34 + rd32(u, 0x34) : 0x40;
  const gd3 = rd32(u, 0x14) ? 0x14 + rd32(u, 0x14) : 0;
  const tags = [];
  if (gd3 && gd3 + 12 <= u.length && String.fromCharCode(u[gd3], u[gd3 + 1], u[gd3 + 2], u[gd3 + 3]) === 'Gd3 ') {
    let i = gd3 + 12, t = '';
    const end = Math.min(u.length, i + rd32(u, gd3 + 8));
    for (; i + 1 < end; i += 2) { const c = u[i] | u[i + 1] << 8; if (c) t += String.fromCharCode(c); else { tags.push(t); t = ''; } }
  }
  let others = false;
  for (const o of OTHER_CHIPS) if (o + 4 <= Math.min(start, 0x100) && (rd32(u, o) & 0x3FFFFFFF)) others = true;
  if (ver >= 0x110 && rd32(u, 0x10)) others = true;
  const body = u.subarray(0, gd3 > start ? gd3 : u.length);
  if (!ym && !sn) throw new Error('ni YM2612 ni SN76489 : ce n’est pas une musique Mega Drive / Master System');
  return { body, ym, sn, others, total: rd32(u, 0x18), loop: rd32(u, 0x1C) ? rd32(u, 0x20) : 0,
           title: ascii(tags[0] || tags[1]), game: ascii(tags[2] || tags[3]), system: tags[4] || '', author: tags[6] || '' };
}

/** Les SysEx à envoyer : { begin, data: [...], end } (pack7 : celui de fm1ota.js) */
function vgmFrames(body, title, game, pack7) {
  const name = [...(title || '')].map(c => c.charCodeAt(0) & 127), g = [...(game || '')].map(c => c.charCodeAt(0) & 127);
  const data = [];
  for (let seq = 0; seq * 512 < body.length; seq++)
    data.push([0xF0, 0x7D, 0x52, 0x53, 0x02, seq & 127, seq >> 7, ...pack7(body.subarray(seq * 512, seq * 512 + 512)), 0xF7]);
  return { begin: [0xF0, 0x7D, 0x52, 0x53, 0x01, ...b35(body.length), ...name, 0, ...g, 0xF7], data,
           end: [0xF0, 0x7D, 0x52, 0x53, 0x03, ...b35(crc32(body)), 0xF7] };
}

const API = { VGM_MAX, VGM_ERR, parseVgm, vgmFrames, crc32, ascii };
if (typeof module !== 'undefined' && module.exports) module.exports = API; else root.RSVGM = API;
})(typeof self !== 'undefined' ? self : this);
