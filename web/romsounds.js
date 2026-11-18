/* SPDX-License-Identifier: GPL-3.0-only
 * ROMsounds : lire un fichier VGM (en-tête, étiquettes GD3), le préparer pour le FM-1 et le découper en SysEx.
 * Ce que reçoit le FM-1 (« RSM4 », firmware/src/romsounds.c) :
 *   en-tête de 64 octets : "RSM4", horloges YM2612 et SN76489, durée et boucle (trames à 44,1 kHz), tailles des trois
 *   parties, avertissements, bruit du SN76489 (rebouclage | largeur << 16), horloges Game Boy et NES, tempo (BPM x 100, deviné :
 *   beatsOf) et position du premier temps (trames), horloge et type de l'AY-3-8910 / YM2149 ; puis la banque
 *   d'échantillons (brute : pour le YM2612, toutes les données de type 0 bout à bout ; pour la NES, l'image de sa mémoire
 *   $8000-$FFFF, que lit le DMC), puis les commandes
 *   avant la boucle (A) et à partir de la boucle (B), chacune compressée (deflate brut). Le FM-1 garde le tout compressé
 *   et décompresse en jouant ; à la fin de B, il reprend B : la boucle ne demande aucun retour en arrière.
 * Utilisé par index.html (navigateur) et tests/rx_test.js (Node). */
(function (root) {
'use strict';
const VGM_MAX = 268 * 1024;              // place du morceau dans le FM-1 (firmware/src/romsounds.c : ARENA)
const VGM_ERR = { 0x41: 'fichier trop gros pour le FM-1', 0x42: 'données hors du fichier', 0x43: 'données incomplètes',
  0x44: 'fichier incomplet', 0x45: 'contrôle CRC faux', 0x49: 'ce n’est pas un fichier VGM', 0x4A: 'fichier VGM trop court',
  0x4B: 'ni YM2612 ni SN76489 dans ce fichier', 0x4C: 'préparation du morceau illisible',
  0x46: 'morceau introuvable dans le FM-1 (la liste a changé : recommence)',
  0x47: 'attribution refusée par le FM-1', 0x48: 'le FM-1 n’a pas pu l’enregistrer (mémoire flash)' };
const LIB_MAX = 40;                      // morceaux au plus dans la bibliothèque du FM-1 (firmware : LIB_MAX)
// horloges des autres puces dans l'en-tête (spécification VGM 1.71)
const OTHER_CHIPS = [0x30, 0x38, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x88, 0x8C, 0x90, 0x98, 0x9C, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8, 0xDC, 0xE0];
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
  const head = ver >= 0x150 && rd32(u, 0x34) ? 0x34 + rd32(u, 0x34) : 0x40;
  const gb = head >= 0x84 ? rd32(u, 0x80) & 0x3FFFFFFF : 0, nes = head >= 0x88 ? rd32(u, 0x84) & 0x3FFFFFFF : 0;
  const fds = head >= 0x88 && (rd32(u, 0x84) & 0x80000000) !== 0;
  const ay = head >= 0x78 ? rd32(u, 0x74) & 0x3FFFFFFF : 0, ayType = ay && head >= 0x79 ? u[0x78] : 0;
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
  if (!ym && !sn && !gb && !nes && !ay) throw new Error('aucune puce que ROMsounds sait jouer (Mega Drive, Master System, Game Gear, Game Boy, NES, AY / YM2149)');
  if (fds) others = true;                        // (le FDS de la NES : pas encore)
  return { body, ym, sn, gb, nes, ay, ayType, others, total: rd32(u, 0x18), loop: rd32(u, 0x1C) ? rd32(u, 0x20) : 0,
           title: ascii(tags[0] || tags[1]), game: ascii(tags[2] || tags[3]), system: tags[4] || '', author: tags[6] || '',
           sys: ascii(tags[4] || tags[5]).slice(0, 22) || (ym ? 'Mega Drive' : gb ? 'Game Boy' : nes ? 'NES' : ay ? (ayType & 0xF0 ? 'YM2149' : 'AY-3-8910') : 'SN76489') };
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
const KEEP = c => c === 0xA0 || c === 0xB3 || c === 0xB4 || c === 0x4F || c === 0x50 || c === 0x52 || c === 0x53 || c === 0x61 || c === 0x62 || c === 0x63 || (c >= 0x70 && c <= 0x8F) || c === 0xE0;

/** Le tempo du morceau, deviné d'après les débuts de notes (YM2612 : key-on ; DAC : départs d'échantillons ; PSG : attaques
 * de volume ; Game Boy, NES : déclenchements) sur les 90 premières secondes : { bpm, phase (trames à 44,1 kHz du premier
 * temps) }, bpm = 0 si le morceau n'a pas assez de notes. Autocorrélation des débuts par tranches de 10 ms, avec une
 * préférence douce autour de 120 BPM ; la phase : le décalage qui tombe sur le plus de débuts. */
function beatsOf(u, start) {
  const BIN = 441, MAX = 9000;                   // 10 ms, 90 s
  const e = new Float32Array(MAX + 1);
  const vol = [15, 15, 15, 15], ayv = [0, 0, 0];
  let t = 0, n = 0, latch = 0;
  const hit = w => { const i = Math.floor(t / BIN); if (i < MAX) { e[i] += w; n++; } };
  for (let p = start; p < u.length && t < MAX * BIN;) {
    const c = u[p], k = cmdLen(u, p);
    if (!k) break;
    if (c === 0x61) t += u[p + 1] | u[p + 2] << 8;
    else if (c === 0x62) t += 735;
    else if (c === 0x63) t += 882;
    else if (c >= 0x70 && c <= 0x7F) t += (c & 15) + 1;
    else if (c >= 0x80 && c <= 0x8F) t += c & 15;
    else if (c === 0x52 && u[p + 1] === 0x28 && (u[p + 2] & 0xF0)) hit(1);
    else if (c === 0xE0 || c === 0x95) hit(1.5);
    else if (c === 0x50) {
      const d = u[p + 1];
      if (d & 0x80) latch = d >> 4 & 7;
      if ((latch & 1) && (d & 0x80)) { const ch = latch >> 1, v = d & 15; if (v + 4 <= vol[ch]) hit(ch === 3 ? 1.5 : 1); vol[ch] = v; }
    } else if (c === 0xB3 && (u[p + 1] === 0x04 || u[p + 1] === 0x09 || u[p + 1] === 0x0E || u[p + 1] === 0x13) && (u[p + 2] & 0x80)) hit(u[p + 1] === 0x13 ? 1.5 : 1);
    else if (c === 0xB4 && (u[p + 1] === 0x03 || u[p + 1] === 0x07 || u[p + 1] === 0x0B || u[p + 1] === 0x0F)) hit(u[p + 1] === 0x0F ? 1.5 : 1);
    else if (c === 0xB4 && u[p + 1] === 0x15 && (u[p + 2] & 0x10)) hit(1.5);   // DPCM
    else if (c === 0xA0 && (u[p + 1] & 0x7F) === 13) hit(1);                     // AY : enveloppe relancée
    else if (c === 0xA0 && (u[p + 1] & 0x7F) >= 8 && (u[p + 1] & 0x7F) <= 10) {  // AY : volume qui monte
      const ch = (u[p + 1] & 0x7F) - 8, v = u[p + 2] & 31; if ((v & 16) || v >= (ayv[ch] & 15) + 4) hit(1); ayv[ch] = v;
    }
    p += k;
  }
  const len = Math.min(MAX, Math.floor(t / BIN));
  if (n < 16 || len < 400) return { bpm: 0, phase: 0 };
  const s = new Float32Array(len);                // un peu lissé : ±10 ms
  for (let i = 0; i < len; i++) s[i] = e[i] + 0.5 * ((i ? e[i - 1] : 0) + (i + 1 < len ? e[i + 1] : 0));
  const at = x => { const i = Math.floor(x), f = x - i; return i + 1 < len ? s[i] * (1 - f) + s[i + 1] * f : 0; };
  const ac = lag => { let a = 0; for (let i = 0; i + lag < len - 1; i++) a += s[i] * at(i + lag); return a / (len - lag); };
  let best = 0, bpm = 0;
  for (let b = 70; b <= 180; b += 0.5) {
    const lag = 6000 / b, prior = Math.exp(-0.5 * (Math.log2(b / 120) / 0.8) ** 2);
    const v = (ac(lag) + 0.5 * ac(2 * lag) + 0.25 * ac(4 * lag)) * prior;
    if (v > best) { best = v; bpm = b; }
  }
  const P = 6000 / bpm;
  let bp = 0, phase = 0;
  for (let ph = 0; ph < P; ph += 0.5) { let a = 0; for (let x = ph; x < len; x += P) a += at(x); if (a > bp) { bp = a; phase = ph; } }
  return { bpm, phase: Math.round(phase * BIN) };
}

async function deflateRaw(u) {
  const s = new Blob([u]).stream().pipeThrough(new CompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(s).arrayBuffer());
}

/* Flux DAC (VGM 1.60, commandes 0x90-0x95) : ROMsounds ne les lit pas ; la page les rejoue ici et les remplace par des
 * écritures DAC ordinaires (52 2A vv) aux bons instants, comme le fait libvgm (emu/dac_control.c : même compteur de
 * pas, même départ « au prochain échantillon », mêmes modes de longueur, boucle et lecture à l'envers). */
const RC = 2 ** 20;                              // précision du compteur de pas (libvgm : RatioCntr)
function streamsOf(u) {
  const streams = new Map();                     // id -> état
  return {
    any: () => [...streams.values()].some(t => t.running),
    cmd(c, p, bank, blocks) {
      const id = u[p + 1];
      if (c === 0x94) { for (const t of streams.values()) if (id === 0xFF || t === streams.get(id)) t.running = false; return; }
      if (c === 0x90) {
        if (id === 0xFF) return;
        const t = streams.get(id) || { running: false, step: 1, base: 0, freq: 0, inc: 0, ok: false, bank: 0xFF };
        t.ok = (u[p + 2] & 0x7F) === 0x02 && ((u[p + 3] << 8) | u[p + 4]) === 0x002A;   // YM2612, DAC (port 0, 0x2A)
        streams.set(id, t);
        return;
      }
      const t = streams.get(id);
      if (!t) return;
      if (c === 0x91) { t.bank = u[p + 2]; t.step = u[p + 3] || 1; t.base = u[p + 4]; return; }
      if (c === 0x92) { t.freq = rd32(u, p + 2); t.inc = Math.round(t.freq * RC / 44100); return; }
      let pos, mode, len;
      if (c === 0x93) { pos = rd32(u, p + 2); mode = u[p + 6]; len = rd32(u, p + 7); }
      else {                                       // 0x95 : le n-ième bloc de la banque
        const blk = blocks[u[p + 2] | u[p + 3] << 8];
        if (t.bank !== 0x00 || !blk) return;
        pos = blk[0]; len = blk[1]; mode = 0x0F | (u[p + 4] & 0x10) | (u[p + 4] & 0x01) << 7;
      }
      if (pos !== 0xFFFFFFFF) t.start = Math.min(pos + t.base, bank.length);
      const m = mode & 0x0F;
      if (m === 1) t.count = len;
      else if (m === 2) t.count = t.freq ? Math.floor(1000 * len / t.freq) : 0;
      else if (m === 3) t.count = Math.floor((bank.length - (t.start - t.base)) / t.step);
      else if (m === 0x0F) t.count = Math.floor(len / t.step);
      else if (m !== 0) t.count = 0;
      t.reverse = (mode & 0x10) !== 0; t.loop = (mode & 0x80) !== 0;
      t.remain = t.count; t.real = t.reverse ? (t.count - 1) * t.step : 0;
      t.val = RC - t.inc;                          // le premier échantillon part au prochain pas
      t.running = t.ok && t.bank === 0x00;
    },
    // avance d'un échantillon (44,1 kHz) : les valeurs à écrire dans le DAC
    tick(bank, out) {
      for (const t of streams.values()) {
        if (!t.running) continue;
        t.val += t.inc;
        let k = Math.floor(t.val / RC); t.val -= k * RC;
        if (k > t.remain) k = t.remain;
        t.remain -= k;
        for (; k > 0; k--) {
          const at = t.start + t.real;
          if (at < bank.length) out.push(bank[at]);
          t.real += t.reverse ? -t.step : t.step;
        }
        if (!t.remain && t.loop) { t.remain = t.count; t.real = t.reverse ? (t.count - 1) * t.step : 0; }
        if (!t.remain) t.running = false;
      }
    }
  };
}

/** Prépare le morceau pour le FM-1 : { blob, pcm, a, b, dacStream } (blob : ce qui est envoyé) */
async function packVgm(u) {
  const v = parseVgm(u);
  const ver = rd32(u, 8), start = ver >= 0x150 && rd32(u, 0x34) ? 0x34 + rd32(u, 0x34) : 0x40;
  const loop = rd32(u, 0x1C) ? 0x1C + rd32(u, 0x1C) : 0;
  const pcm = [], a = [], b = [], blocks = [];
  let nesMem = null;
  let p = start, dacStream = false, others = false, bankLen = 0;
  for (let q = start; q < u.length;) {            // d'abord la banque PCM (les flux DAC la lisent)
    const n = cmdLen(u, q);
    if (!n || q + n > u.length) break;
    if (u[q] === 0x67 && u[q + 2] === 0x00) { pcm.push(u.subarray(q + 7, q + n)); blocks.push([bankLen, n - 7]); bankLen += n - 7; }
    if (u[q] === 0x67 && u[q + 2] === 0xC2 && n >= 9) {   // NES : écriture dans sa mémoire (adresse sur 16 bits)
      const at = u[q + 7] | u[q + 8] << 8;
      if (!nesMem) nesMem = new Uint8Array(0x8000);
      for (let i = 0; i < n - 9; i++) if (at + i >= 0x8000 && at + i <= 0xFFFF) nesMem[at + i - 0x8000] = u[q + 9 + i];
    }
    if (u[q] >= 0x90 && u[q] <= 0x95) dacStream = true;
    q += n;
  }
  const bank = new Uint8Array(bankLen);
  { let o = 0; for (const q of pcm) { bank.set(q, o); o += q.length; } }
  const st = dacStream ? streamsOf(u) : null;
  let pend = 0;                                   // attente pas encore écrite (flux DAC : on réécrit les attentes)
  const flush = out => {
    while (pend > 0) { if (pend <= 16) { out.push(Uint8Array.of(0x70 + pend - 1)); pend = 0; } else { const k = Math.min(pend, 65535); out.push(Uint8Array.of(0x61, k & 255, k >> 8)); pend -= k; } }
  };
  const wait = (w, out) => {                      // w échantillons, avec les écritures des flux DAC
    if (!st || !st.any()) { pend += w; return; }
    const vals = [];
    for (let i = 0; i < w; i++) {
      st.tick(bank, vals);
      if (vals.length) { flush(out); for (const x of vals) out.push(Uint8Array.of(0x52, 0x2A, x)); vals.length = 0; }
      pend++;
    }
  };
  while (p < u.length) {
    const n = cmdLen(u, p), c = u[p];
    if (!n) break;
    if (p + n > u.length) break;
    const out = loop && p >= loop ? b : a;
    if (loop && p === loop && st) flush(a);      // (l'attente en cours appartient à la partie A)
    if (c === 0x67) { /* banque PCM : déjà prise */ }
    else if (c >= 0x90 && c <= 0x95) st.cmd(c, p, bank, blocks);
    else if (st && (c === 0x61 || c === 0x62 || c === 0x63 || (c >= 0x70 && c <= 0x7F))) {
      wait(c === 0x61 ? u[p + 1] | u[p + 2] << 8 : c === 0x62 ? 735 : c === 0x63 ? 882 : (c & 15) + 1, out);
    } else if (st && c >= 0x80 && c <= 0x8F) { flush(out); out.push(Uint8Array.of(0x80)); wait(c & 15, out); }
    else if (KEEP(c)) { if (st) flush(out); out.push(u.subarray(p, p + n)); }
    else others = true;
    p += n;
  }
  if (st) flush(loop ? b : a);
  const cat = parts => { const t = new Uint8Array(parts.reduce((x, y) => x + y.length, 0)); let o = 0; for (const q of parts) { t.set(q, o); o += q.length; } return t; };
  const end = Uint8Array.of(0x66);
  const P = v.ym ? cat(pcm) : nesMem || new Uint8Array(0), A = await deflateRaw(cat([...a, end])), B = loop ? await deflateRaw(cat([...b, end])) : new Uint8Array(0);
  const beat = beatsOf(u, start);
  const head = new Uint8Array(64), dv = new DataView(head.buffer);
  head.set([0x52, 0x53, 0x4D, 0x34]);                 // "RSM4"
  dv.setUint32(4, v.ym, true); dv.setUint32(8, v.sn, true);
  dv.setUint32(12, v.total, true); dv.setUint32(16, loop ? v.loop : 0, true);
  dv.setUint32(20, P.length, true); dv.setUint32(24, A.length, true); dv.setUint32(28, B.length, true);
  dv.setUint32(32, v.others || others ? 1 : 0, true);   // (les flux DAC sont convertis : plus d'avertissement)
  dv.setUint32(36, ver >= 0x110 ? (u[0x28] | u[0x29] << 8 | u[0x2A] << 16) >>> 0 : 0, true);   // bruit du SN76489
  dv.setUint32(40, v.gb, true); dv.setUint32(44, v.nes, true);
  dv.setUint32(48, Math.round(beat.bpm * 100), true); dv.setUint32(52, beat.phase, true);   // tempo (BPM x 100), 1er temps
  dv.setUint32(56, v.ay, true); dv.setUint32(60, v.ayType, true);                         // AY-3-8910 / YM2149
  const blob = cat([head, P, A, B]);
  return Object.assign(v, { blob, bpm: beat.bpm, phase: beat.phase, pcmLen: P.length, aLen: A.length, bLen: B.length, dacStream, raw: u.length });
}

/** La bibliothèque du FM-1, d'après ses SysEx 0x15 (un par morceau) et 0x16 (la place, en secteurs de 4 Ko) :
 * { entries: [{ index, nsect, size, crc, title, game, sys }], count, used, total, cur (127 : aucun) } */
const rd35 = (d, i) => (d[i] + d[i + 1] * 128 + d[i + 2] * 16384 + d[i + 3] * 2097152 + (d[i + 4] & 15) * 268435456) >>> 0;
function libFromFrames(frames) {
  const entries = [];
  let end = null;
  for (const d of frames) {
    if (d[0] !== 0xF0 || d[1] !== 0x7D || d[2] !== 0x52 || d[3] !== 0x53) continue;
    if (d[4] === 0x15 && d.length >= 18) {
      const str = [];
      let cur = '';
      for (let i = 17; i < d.length - 1; i++) { if (d[i]) cur += String.fromCharCode(d[i]); else { str.push(cur); cur = ''; } }
      str.push(cur);
      entries.push({ index: d[5], nsect: d[6], size: rd35(d, 7), crc: rd35(d, 12), title: str[0] || '', game: str[1] || '', sys: str[2] || '' });
    } else if (d[4] === 0x16 && d.length >= 10) end = { count: d[5], used: d[6], total: d[7], cur: d[8] };
  }
  return end && { entries, ...end };
}
/** La place pour un morceau (blob) si l'on retire sel (des entrées de la liste) :
 * { need, free, deficit (secteurs à libérer), full (déjà LIB_MAX morceaux), dup (déjà dans le FM-1), tooBig, ok } */
function libPlan(st, blob, sel = []) {
  const need = Math.ceil(blob.length / 4096), crc = crc32(blob), free = st.total - st.used;
  const dup = st.entries.some(e => e.crc === crc && e.size === blob.length);
  const deficit = dup ? 0 : Math.max(0, need - free), full = !dup && st.count >= LIB_MAX, tooBig = need > st.total;
  const freed = sel.reduce((a, e) => a + e.nsect, 0);
  const ok = dup || tooBig || (freed >= deficit && st.count - sel.length < LIB_MAX);
  return { need, free, deficit, full, dup, tooBig, ok };
}

/* ---------- Atari ST (et Amstrad CPC, ZX Spectrum) : fichiers .ym ---------- */
/** LHA « -lh5- » (les .ym sont presque toujours compressés ainsi) : le contenu décompressé. Écrit d'après la
 * description publique du format (dictionnaire 8 Ko, blocs de codes de Huffman). */
function lh5(src, outLen) {
  const NC = 510, NP = 14, NT = 19, CBIT = 9, PBIT = 4, TBIT = 5;
  const out = new Uint8Array(outLen), left = new Uint16Array(2 * NC), right = new Uint16Array(2 * NC);
  const cLen = new Uint8Array(NC), ptLen = new Uint8Array(NT > NP ? NT : NP), cTable = new Uint16Array(4096), ptTable = new Uint16Array(256);
  let pos = 0, bitbuf = 0, sub = 0, bitcount = 0;
  const fill = n => {
    bitbuf = (bitbuf << n) & 0xFFFF;
    while (n > bitcount) { n -= bitcount; bitbuf |= (sub << n) & 0xFFFF; sub = pos < src.length ? src[pos++] : 0; bitcount = 8; }
    bitcount -= n; bitbuf |= sub >> bitcount;
  };
  const get = n => { const x = bitbuf >> (16 - n); fill(n); return x; };
  function table(nchar, bitlen, tbits, tab) {
    const count = new Uint32Array(17), weight = new Uint32Array(17), start = new Uint32Array(18);
    for (let i = 0; i < nchar; i++) count[bitlen[i]]++;
    for (let i = 1; i <= 16; i++) start[i + 1] = start[i] + (count[i] << (16 - i));
    // (start[17] != 65536 : une table incomplète, par exemple un seul code ; LHA la construit quand même)
    const jut = 16 - tbits;
    for (let i = 1; i <= tbits; i++) { start[i] >>>= jut; weight[i] = 1 << (tbits - i); }
    for (let i = tbits + 1; i <= 16; i++) weight[i] = 1 << (16 - i);
    let i = (start[tbits + 1] >>> jut) & 0xFFFF;
    if (i !== 0) { const k = 1 << tbits; while (i !== k) tab[i++] = 0; }
    let avail = nchar;
    const mask = 1 << (15 - tbits);
    for (let ch = 0; ch < nchar; ch++) {
      const len = bitlen[ch];
      if (!len) continue;
      const next = start[len] + weight[len];
      if (len <= tbits) { for (let j = start[len]; j < next; j++) tab[j] = ch; }
      else {
        let k = start[len], arr = tab, idx = k >>> jut;
        for (let j = len - tbits; j; j--) {
          if (arr[idx] === 0) { right[avail] = left[avail] = 0; arr[idx] = avail++; }
          const node = arr[idx];
          if (k & mask) { arr = right; } else { arr = left; }
          idx = node; k <<= 1;
        }
        arr[idx] = ch;
      }
      start[len] = next;
    }
  }
  function ptRead(nn, nbit, special) {
    const n = get(nbit);
    if (!n) { const c = get(nbit); ptLen.fill(0, 0, nn); ptTable.fill(c); return; }
    let i = 0;
    while (i < n) {
      let c = bitbuf >> 13;
      if (c === 7) { let m = 1 << 12; while (m & bitbuf) { m >>= 1; c++; } }
      fill(c < 7 ? 3 : c - 3);
      ptLen[i++] = c;
      if (i === special) { let z = get(2); while (z-- > 0) ptLen[i++] = 0; }
    }
    while (i < nn) ptLen[i++] = 0;
    table(nn, ptLen, 8, ptTable);
  }
  function cRead() {
    const n = get(CBIT);
    if (!n) { const c = get(CBIT); cLen.fill(0); cTable.fill(c); return; }
    let i = 0;
    while (i < n) {
      let c = ptTable[bitbuf >> 8];
      if (c >= NT) { let m = 1 << 7; do { c = bitbuf & m ? right[c] : left[c]; m >>= 1; } while (c >= NT); }
      fill(ptLen[c]);
      if (c <= 2) { c = c === 0 ? 1 : c === 1 ? get(4) + 3 : get(CBIT) + 20; while (c-- > 0) cLen[i++] = 0; }
      else cLen[i++] = c - 2;
    }
    while (i < NC) cLen[i++] = 0;
    table(NC, cLen, 12, cTable);
  }
  let block = 0, o = 0;
  fill(16);
  while (o < outLen) {
    if (!block) { block = get(16); ptRead(NT, TBIT, 3); cRead(); ptRead(NP, PBIT, -1); }
    block--;
    let j = cTable[bitbuf >> 4];
    if (j < NC) fill(cLen[j]);
    else { fill(12); let m = 1 << 15; do { j = bitbuf & m ? right[j] : left[j]; m >>= 1; } while (j >= NC); fill(cLen[j] - 12); }
    if (j < 256) { out[o++] = j; continue; }
    const len = j - 256 + 3;
    let p = ptTable[bitbuf >> 8];
    if (p < NP) fill(ptLen[p]);
    else { fill(8); let m = 1 << 7; do { p = bitbuf & m ? right[p] : left[p]; m >>= 1; } while (p >= NP); fill(ptLen[p] - 8); }
    if (p) p = (1 << (p - 1)) + get(p - 1);
    for (let k = 0, from = o - p - 1; k < len && o < outLen; k++, from++) out[o++] = from >= 0 ? out[from] : 0x20;
  }
  return out;
}

/** Une archive LHA d'un seul fichier (en-tête de niveau 0, 1 ou 2) : le fichier, décompressé */
function unlha(u) {
  const m = String.fromCharCode(...u.subarray(2, 7));
  if (m !== '-lh5-' && m !== '-lh0-') throw new Error('archive LHA non gérée (' + m + ')');
  const level = u[20], orig = rd32(u, 11);
  let packed = rd32(u, 7), at;
  if (level === 2) at = u[0] | u[1] << 8;
  else {
    at = u[0] + 2;
    if (level === 1) { let next = u[at - 2] | u[at - 1] << 8; while (next) { at += next; packed -= next; next = u[at - 2] | u[at - 1] << 8; } }
  }
  const data = u.subarray(at, at + packed);
  return m === '-lh0-' ? data.slice(0, orig) : lh5(data, orig);
}

/** Un .ym (YM2!, YM3!, YM3b, YM5!, YM6!, compressé LHA ou non) -> un VGM (AY / YM2149, commande 0xA0) que packVgm lit.
 * Les effets spéciaux des YM5 / YM6 (digidrums, voix SID, sync-buzzer) ne sont pas rejoués : le son de base, oui. */
function ymToVgm(u) {
  if (String.fromCharCode(...u.subarray(2, 5)) === '-lh') u = unlha(u);
  const id = String.fromCharCode(...u.subarray(0, 4)), be32 = i => (u[i] << 24 | u[i + 1] << 16 | u[i + 2] << 8 | u[i + 3]) >>> 0;
  let nb, regs, clock = 2000000, rate = 50, loop = 0, data, inter = true, name = '', author = '', comment = '';
  if (id === 'YM2!' || id === 'YM3!' || id === 'YM3b') {
    regs = 14; nb = Math.floor((u.length - 4 - (id === 'YM3b' ? 4 : 0)) / 14); data = 4;
    if (id === 'YM3b') loop = rd32(u, u.length - 4);
  } else if (id === 'YM5!' || id === 'YM6!') {
    if (String.fromCharCode(...u.subarray(4, 12)) !== 'LeOnArD!') throw new Error('fichier .ym abîmé');
    nb = be32(12); inter = (be32(16) & 1) !== 0;
    const drums = u[20] << 8 | u[21];
    clock = be32(22); rate = u[26] << 8 | u[27]; loop = be32(28);
    let p = 34 + (u[32] << 8 | u[33]);
    for (let d = 0; d < drums; d++) p += 4 + be32(p);
    const str = () => { let t = ''; while (p < u.length && u[p]) t += String.fromCharCode(u[p++]); p++; return t; };
    name = str(); author = str(); comment = str();
    regs = 16; data = p;
  } else throw new Error(/^(YM|MIX|YMT)/.test(id) ? 'format ' + id.replace(/[^ -~]/g, '') + ' (remix ou tracker) : pas encore lu' : 'ce n’est pas un fichier .ym');
  if (!nb || data + nb * regs > u.length) throw new Error('fichier .ym incomplet');
  if (loop >= nb) loop = 0;
  const reg = (f, r) => inter ? u[data + r * nb + f] : u[data + f * regs + r];
  const cmd = [];
  const w = (r, v) => cmd.push(0xA0, r, v);
  const cur = new Int16Array(14).fill(-1);
  let t = 0, frac = 0, loopAt = -1, loopT = 0;
  for (let f = 0; f < nb; f++) {
    if (f === loop) { loopAt = cmd.length; loopT = t; cur.fill(-1); }
    for (let r = 0; r < 14; r++) {
      let v = reg(f, r);
      if (r === 13) { if (v !== 0xFF) w(13, v & 15); continue; }
      v &= [0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0x3F, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF][r];
      if (cur[r] !== v) { w(r, v); cur[r] = v; }
    }
    frac += 44100; let s = Math.floor(frac / rate); frac -= s * rate; t += s;
    while (s > 0) { if (s === 882) { cmd.push(0x63); s = 0; } else if (s === 735) { cmd.push(0x62); s = 0; } else { const k = Math.min(s, 65535); cmd.push(0x61, k & 255, k >> 8); s -= k; } }
  }
  cmd.push(0xA0, 7, 0x3F, 0x66);
  const sys = clock === 2000000 ? 'Atari ST' : clock === 1000000 ? 'Amstrad CPC' : clock === 1773400 ? 'ZX Spectrum' : 'AY-3-8910';
  const tag = [name, '', '', '', sys, '', author, '', '', '', comment].map(s => [...(s || '')].map(c => [c.charCodeAt(0) & 255, 0]).flat().concat([0, 0])).flat();
  const head = new Uint8Array(0x80), gd3 = new Uint8Array(12 + tag.length), body = Uint8Array.from(cmd);
  const dv = new DataView(head.buffer), gv = new DataView(gd3.buffer);
  head.set([0x56, 0x67, 0x6D, 0x20]);
  dv.setUint32(8, 0x151, true); dv.setUint32(0x34, 0x80 - 0x34, true);
  dv.setUint32(0x18, t, true);
  if (loopAt >= 0 && loop < nb) { dv.setUint32(0x1C, 0x80 + loopAt - 0x1C, true); dv.setUint32(0x20, t - loopT, true); }
  dv.setUint32(0x74, clock, true); head[0x78] = sys === 'Atari ST' ? 0x10 : 0x00;
  dv.setUint32(0x14, 0x80 + body.length - 0x14, true);
  gd3.set([0x47, 0x64, 0x33, 0x20]); gv.setUint32(4, 0x100, true); gv.setUint32(8, tag.length, true); gd3.set(tag, 12);
  const all = new Uint8Array(0x80 + body.length + gd3.length);
  all.set(head); all.set(body, 0x80); all.set(gd3, 0x80 + body.length);
  dv.setUint32(4, all.length - 4, true); all.set(head.subarray(0, 8));
  return all;
}
/** Un fichier .ym ? (compressé LHA ou non) */
const isYm = u => u.length > 8 && (String.fromCharCode(...u.subarray(2, 7)) === '-lh5-' || /^YM[2-6]/.test(String.fromCharCode(...u.subarray(0, 3))));

/** Un .zip (les packs de VGMRips) : [{ name, open() -> Promise<Uint8Array> }] pour ses .vgm / .vgz, dans l'ordre du pack.
 * Méthodes 0 (stocké) et 8 (deflate, décompressé par DecompressionStream). */
function zipList(z) {
  let e = -1;
  for (let i = z.length - 22; i >= Math.max(0, z.length - 22 - 65535); i--) if (rd32(z, i) === 0x06054B50) { e = i; break; }
  if (e < 0) throw new Error('archive ZIP illisible');
  const n = z[e + 10] | z[e + 11] << 8, out = [];
  let p = rd32(z, e + 16);
  for (let k = 0; k < n && rd32(z, p) === 0x02014B50; k++) {
    const flags = z[p + 8] | z[p + 9] << 8, method = z[p + 10] | z[p + 11] << 8, csize = rd32(z, p + 20);
    const nl = z[p + 28] | z[p + 29] << 8, xl = z[p + 30] | z[p + 31] << 8, cl = z[p + 32] | z[p + 33] << 8, lh = rd32(z, p + 42);
    const raw = z.subarray(p + 46, p + 46 + nl);
    const name = flags & 0x800 ? new TextDecoder().decode(raw) : String.fromCharCode(...raw);
    p += 46 + nl + xl + cl;
    if (!/\.(vg[mz]|ym)$/i.test(name) || rd32(z, lh) !== 0x04034B50) continue;
    const at = lh + 30 + (z[lh + 26] | z[lh + 27] << 8) + (z[lh + 28] | z[lh + 29] << 8), data = z.subarray(at, at + csize);
    out.push({ name: name.split('/').pop(), open: async () => {
      if (method === 0) return data;
      if (method !== 8) throw new Error('compression ZIP non gérée (' + method + ')');
      return new Uint8Array(await new Response(new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer());
    } });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
}

/** .vgz (gzip) -> .vgm ; un .vgm reste tel quel */
async function gunzipIf(u) {
  if (u[0] !== 0x1F || u[1] !== 0x8B) return u;
  if (typeof DecompressionStream === 'undefined') throw new Error('ce navigateur ne sait pas décompresser un .vgz : utilise un .vgm');
  return new Uint8Array(await new Response(new Blob([u]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
}

/** Changer le tempo d'un morceau déjà préparé (blob RSM3) : BPM (0 : inconnu) */
function setBpm(blob, bpm) { new DataView(blob.buffer, blob.byteOffset).setUint32(48, Math.round(bpm * 100), true); }

/** Les SysEx à envoyer : { begin, data: [...], end } (pack7 : celui de fm1ota.js) */
function vgmFrames(body, title, game, pack7, sys) {
  const a7 = t => [...(t || '')].map(c => c.charCodeAt(0) & 127);
  const name = a7(title), g = a7(game), y = a7(sys);
  const data = [];
  for (let seq = 0; seq * 512 < body.length; seq++)
    data.push([0xF0, 0x7D, 0x52, 0x53, 0x02, seq & 127, seq >> 7, ...pack7(body.subarray(seq * 512, seq * 512 + 512)), 0xF7]);
  return { begin: [0xF0, 0x7D, 0x52, 0x53, 0x01, ...b35(body.length), ...name, 0, ...g, 0, ...y, 0xF7], data,
           end: [0xF0, 0x7D, 0x52, 0x53, 0x03, ...b35(crc32(body)), 0xF7] };
}

const API = { beatsOf, setBpm, ymToVgm, isYm, unlha, VGM_MAX, VGM_ERR, LIB_MAX, parseVgm, packVgm, vgmFrames, crc32, ascii, zipList, gunzipIf, libFromFrames, libPlan, b35 };
if (typeof module !== 'undefined' && module.exports) module.exports = API; else root.RSVGM = API;
})(typeof self !== 'undefined' ? self : this);
