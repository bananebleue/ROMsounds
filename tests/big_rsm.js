// SPDX-License-Identifier: GPL-3.0-only
// ROMsounds : un RSM de test plus gros (banque PCM rallongée d'octets pseudo-aléatoires) jusqu'à N Ko, pour la bibliothèque.
//   node tests/big_rsm.js PETIT.rsm GROS.rsm N
// un RSM de test plus gros : la banque PCM rallongée (octets pseudo-aléatoires) jusqu'à N Ko
const fs=require('fs'); const [inp,out,kb]=process.argv.slice(2); const m=fs.readFileSync(inp);
const hl=m[3]===0x33?56:m[3]===0x32?48:40, pcm=m.readUInt32LE(20), add=kb*1024-m.length; const extra=Buffer.alloc(add); let x=kb*7919+out.length;
for(let i=0;i<add;i++){x=(x*1103515245+12345)>>>0; extra[i]=x>>>24;}
const h=Buffer.from(m.subarray(0,hl)); h.writeUInt32LE(pcm+add,20);
fs.writeFileSync(out,Buffer.concat([h,m.subarray(hl,hl+pcm),extra,m.subarray(hl+pcm)]));
