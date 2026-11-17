/* SPDX-License-Identifier: GPL-3.0-only
 * ROMsounds : lire un fichier VGM (en-tête, étiquettes GD3), le préparer pour le FM-1 et le découper en SysEx.
 * Ce que reçoit le FM-1 (« RSM1 », firmware/src/romsounds.c) :
 *   en-tête de 40 octets : "RSM1", horloges YM2612 et SN76489, durée et boucle (trames à 44,1 kHz), tailles des trois
 *   parties, avertissements ; puis la banque PCM (brute, toutes les données de type 0 bout à bout), puis les commandes
 *   avant la boucle (A) et à partir de la boucle (B), chacune compressée (deflate brut). Le FM-1 garde le tout compressé
 *   et décompresse en jouant ; à la fin de B, il reprend B : la boucle ne demande aucun retour en arrière.
 * Utilisé par index.html (navigateur) et tests/rx_test.js (Node). */
(function (root) {
'use strict';
const VGM_MAX = 268 * 1024;              // place du morceau dans le FM-1 (firmware/src/romsounds.c : ARENA)
const VGM_ERR = { 0x41: 'fichier trop gros pour le FM-1', 0x42: 'données hors du fichier', 0x43: 'données incomplètes',
  0x44: 'fichier incomplet', 0x45: 'contrôle CRC faux', 0x49: 'ce n’est pas un fichier VGM', 0x4A: 'fichier VGM trop court',
  0x4B: 'ni YM2612 ni SN76489 dans ce fichier', 0x4C: 'préparation du morceau illisible' };
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

// longueur d'une commande VGM à la position p (spécification 1.71) ; 0 : fin
function cmdLen(u, p) {
  const c = u[p];
  if (c === 0x66) return 0;
  if (c === 0x67) return 7 + (rd32(u, p + 3) & 0x7FFFFFFF);
  if (c === 0x68) return 12;
  if (c === 0x4F || c === 0x50 || (c >= 0x30 && c <= 0x3F)) return 2;
  if ((c >= 0x40 && c <= 0x4E) || (c >= 0x51 && c <= 0x5F) || c === 0x61 || (c >= 0xA0 && c <= 0xBF)) return 3;
  if (c === 0x62 || c === 0x63 || (c >= 0x70 && c <= 0x8F)) return 1;
  if (c >= 0x90 && c <= 0x95) return [5, 5, 6, 11, 2, 5][c - 0x90];
  if (c >= 0xC0 && c <= 0xDF) return 4;
  if (c >= 0xE0) return 5;
  return 1;
}
const KEEP = c => c === 0x50 || c === 0x52 || c === 0x53 || c === 0x61 || c === 0x62 || c === 0x63 || (c >= 0x70 && c <= 0x8F) || c === 0xE0;

async function deflateRaw(u) {
  const s = new Blob([u]).stream().pipeThrough(new CompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(s).arrayBuffer());
}

/** Prépare le morceau pour le FM-1 : { blob, pcm, a, b, dacStream } (blob : ce qui est envoyé) */
async function packVgm(u) {
  const v = parseVgm(u);
  const ver = rd32(u, 8), start = ver >= 0x150 && rd32(u, 0x34) ? 0x34 + rd32(u, 0x34) : 0x40;
  const loop = rd32(u, 0x1C) ? 0x1C + rd32(u, 0x1C) : 0;
  const pcm = [], a = [], b = [];
  let p = start, dacStream = false, others = false;
  while (p < u.length) {
    const n = cmdLen(u, p), c = u[p];
    if (!n) break;
    if (p + n > u.length) break;
    if (c === 0x67) { if (u[p + 2] === 0x00) pcm.push(u.subarray(p + 7, p + n)); }
    else if (KEEP(c)) (loop && p >= loop ? b : a).push(u.subarray(p, p + n));
    else if (c >= 0x90 && c <= 0x95) dacStream = true;
    else if (c !== 0x4F) others = true;
    p += n;
  }
  const cat = parts => { const t = new Uint8Array(parts.reduce((x, y) => x + y.length, 0)); let o = 0; for (const q of parts) { t.set(q, o); o += q.length; } return t; };
  const end = Uint8Array.of(0x66);
  const P = cat(pcm), A = await deflateRaw(cat([...a, end])), B = loop ? await deflateRaw(cat([...b, end])) : new Uint8Array(0);
  const head = new Uint8Array(40), dv = new DataView(head.buffer);
  head.set([0x52, 0x53, 0x4D, 0x31]);                 // "RSM1"
  dv.setUint32(4, v.ym, true); dv.setUint32(8, v.sn, true);
  dv.setUint32(12, v.total, true); dv.setUint32(16, loop ? v.loop : 0, true);
  dv.setUint32(20, P.length, true); dv.setUint32(24, A.length, true); dv.setUint32(28, B.length, true);
  dv.setUint32(32, (v.others || others ? 1 : 0) | (dacStream ? 2 : 0), true);
  const blob = cat([head, P, A, B]);
  return Object.assign(v, { blob, pcmLen: P.length, aLen: A.length, bLen: B.length, dacStream, raw: u.length });
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

const API = { VGM_MAX, VGM_ERR, parseVgm, packVgm, vgmFrames, crc32, ascii };
if (typeof module !== 'undefined' && module.exports) module.exports = API; else root.RSVGM = API;
})(typeof self !== 'undefined' ? self : this);
