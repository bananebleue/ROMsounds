#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ROMsounds : le lecteur de fichiers SNDH (Atari ST) de la page (sndh/sndh.c + le 68000 de Musashi, ext/musashi) en
# WebAssembly, sans bibliothèque C (sndh/libc : le peu que demande Musashi).  ->  ${1:-../web/sndh.wasm}
#   SNDH_MAIN=1 : le même, pour ce PC (build/sndh/sndh_test FICHIER HZ TRAMES : les écritures du YM en texte)
set -e
cd "$(dirname "$0")/.."
M="ext/musashi/m68kcpu.c ext/musashi/m68kops.c ext/musashi/softfloat/softfloat.c"
if [ -n "$SNDH_MAIN" ]; then
  mkdir -p build/sndh
  cc -O2 -w -DSNDH_MAIN -Iext/musashi -o build/sndh/sndh_test sndh/sndh.c $M -lm
  echo "sndh -> build/sndh/sndh_test"
  exit 0
fi
OUT=${1:-../web/sndh.wasm}
F="--target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -w -DSNDH_WASM -Isndh/libc -Iext/musashi"
mkdir -p build/sndhw
for f in sndh/sndh.c sndh/libc.c $M; do clang $F -c "$f" -o "build/sndhw/$(basename "$f" .c).o"; done
wasm-ld --no-entry --strip-all --initial-memory=33554432 -o "$OUT" build/sndhw/*.o
echo "sndh -> $OUT ($(wc -c < "$OUT") octets)"
