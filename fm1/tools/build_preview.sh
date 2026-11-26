#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ROMsounds : le lecteur de prévisualisation de la page (preview/preview.c + les puces du firmware) en WebAssembly,
# sans bibliothèque C : clang (cible wasm32) et wasm-ld (paquet lld).  ->  ${1:-../web/preview.wasm}
set -e
cd "$(dirname "$0")/.."
OUT=${1:-../web/preview.wasm}
F="--target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-const-variable"
I="-Ifirmware/src -Ifirmware/ext/shim -Ifirmware/ext/fmopn -Ifirmware/ext/fmopl -Ifirmware/ext/ym2151 -Ifirmware/ext/uzlib"
mkdir -p build/preview
clang $F $I -c preview/preview.c -o build/preview/preview.o
clang $F $I -c firmware/ext/fmopn/fmopn.c -o build/preview/fmopn.o
clang $F $I -c firmware/ext/fmopn/ymdeltat.c -o build/preview/ymdeltat.o
clang $F $I -c firmware/ext/fmopl/fmopl.c -o build/preview/fmopl.o
clang $F $I -c firmware/ext/ym2151/ym2151.c -o build/preview/ym2151.o
clang $F $I -c firmware/ext/uzlib/tinflate.c -o build/preview/tinflate.o
wasm-ld --no-entry --strip-all --initial-memory=33554432 -o "$OUT" build/preview/preview.o build/preview/fmopn.o build/preview/ymdeltat.o build/preview/fmopl.o build/preview/ym2151.o build/preview/tinflate.o
echo "preview -> $OUT ($(wc -c < "$OUT") octets)"
