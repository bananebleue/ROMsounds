/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMsounds : relit les SysEx produits par la page (tests/rx_test.js) avec les outils du firmware (rx.h), comme
 * app_sysex (firmware/src/romsounds.c), et vérifie que le fichier revient à l'identique, CRC compris.
 *   cc -O2 -Ifirmware/src -o build/rx_test tests/rx_test.c && build/rx_test build/test.syx build/test.rsm */
#include <stdio.h>
#include <string.h>
#include "rx.h"
static uint8_t syx[1 << 21], out[1 << 20], ref[1 << 20];
int main(int argc, char **argv)
{
    FILE *f;
    uint32_t n, nr, i = 0, size = 0, got = 0, crc = 0, frames = 0;
    if (argc < 3 || !(f = fopen(argv[1], "rb"))) return 2;
    n = (uint32_t)fread(syx, 1, sizeof syx, f); fclose(f);
    if (!(f = fopen(argv[2], "rb"))) return 2;
    nr = (uint32_t)fread(ref, 1, sizeof ref, f); fclose(f);
    while (i < n) {
        uint32_t s = i + 1, e = s;
        const uint8_t *p = syx + s;
        if (syx[i] != 0xF0) { printf("pas de F0 à %u\n", i); return 1; }
        while (e < n && syx[e] != 0xF7) e++;
        n = n; frames++;
        if (p[0] != 0x7D || p[1] != 0x52 || p[2] != 0x53) { printf("en-tête faux\n"); return 1; }
        if (p[3] == 1) size = rd35(p + 4);
        else if (p[3] == 2) {
            uint32_t seq = p[4] | (uint32_t)p[5] << 7, off = seq * 512u, want = size - off < 512u ? size - off : 512u;
            if (unpack7(p + 6, e - s - 6, out + off, want) < want) { printf("bloc %u incomplet\n", seq); return 1; }
            got += want;
        } else if (p[3] == 3) crc = rd35(p + 4);
        i = e + 1;
    }
    printf("%u SysEx, %u octets reçus sur %u\n", frames, got, size);
    if (got != size || rx_crc32(out, size) != crc) { printf("CRC FAUX\n"); return 1; }
    if (size > nr || memcmp(out, ref, size)) { printf("contenu différent du fichier\n"); return 1; }
    printf("identique au fichier, CRC %08x : ok\n", crc);
    return 0;
}
