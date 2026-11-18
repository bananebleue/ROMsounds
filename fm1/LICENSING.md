# Licences

## ROMsounds

ROMsounds est un logiciel libre sous GNU GPL version 3 seulement (`GPL-3.0-only`, texte complet dans `LICENSE`),
comme la plateforme Dinghy / Felucca / SLOOP sur laquelle il repose.

| | |
| --- | --- |
| Les changements de ROMsounds : `firmware/src/romsounds.c` (lecteur VGM), `firmware/src/psg.h` (SN76489), `firmware/src/vgm.h` (lecture VGM), `firmware/src/rx.h` (réception), `../web/romsounds.js` (envoi depuis la page), le crochet `APP_SYSEX` de `firmware/src/platform.c`, `tools/build.py` (nom, identité FM-1_95N, couleur), `firmware/src/store.h` (STORE_MAGIC « RSND », zone de la bibliothèque autorisée), la page `../web/`, le workflow | Copyright (C) 2026 Stéphane Faure |
| `../web/fm1ota.js` et `../tests/fm1ota_test.js` : transposition JavaScript de `tools/fm1_install.py` | Copyright (C) 2026 Stéphane Faure, GPL-3.0-only |
| `firmware/ext/uzlib/` : uzlib (pfalcon/uzlib 6d60d65), la décompression (inflate), non modifié | Copyright (C) 2003 Joergen Ibsen, 1997-2014 Simon Tatham, 2014-2020 Paul Sokolovsky ; licence zlib (`firmware/ext/uzlib/LICENSE`), compatible GPL |
| `firmware/ext/fmopn/` : cœur YM2612 de MAME (fm.c, Jarek Burczynski, Tatsuyuki Satoh ; version 1.5.1 de Genesis Plus GX, Eke-Eke), tel que dans libvgm (ValleyBell, `emu/cores/fmopn.c`, c5c03a2). Modifié pour ROMsounds (marqué « ROMsounds » dans le code) : YM2612, YM2610 et YM2203 seulement (la même place en RAM), tables calculées à l'avance (`fmopn_tables.h`, `tools/gen_fmopn_tables.c`), calcul à la fréquence native sans virgule, une seule puce statique, `fmopn_shim.h` à la place des en-têtes de libvgm | GPL-2.0-or-later (en-tête du fichier) ; utilisé ici sous GPL-3.0 |
| `firmware/ext/fmopn/ymdeltat.c`, `ymdeltat.h` : l'ADPCM delta-T de MAME (Hiromitsu Shioya, Tatsuyuki Satoh, Jarek Burczynski), tel que dans libvgm (c5c03a2), pour l'ADPCM-B du YM2610. Modifié pour ROMsounds : sans virgule flottante, `fmopn_shim.h` à la place des en-têtes de libvgm | GPL-2.0-or-later (licence de ymdeltat dans MAME, sans en-tête dans libvgm) ; utilisé ici sous GPL-3.0 |
| `firmware/ext/ym2151/` : cœur YM2151 de MAME (ym2151.c, Jarek Burczynski, Ernesto Corvi), tel que dans libvgm (c5c03a2). Modifié pour ROMsounds (marqué « ROMsounds ») : tables calculées à l'avance à la fréquence native (`ym2151_tables.h`, `tools/gen_ym2151_tables.c`), sans virgule flottante, une seule puce statique, `ym2151_shim.h` à la place des en-têtes de libvgm | GPL-2.0-or-later (en-tête du fichier) ; utilisé ici sous GPL-3.0 |
| `firmware/ext/fmopl/` : cœur OPL2 / OPL de MAME (fmopl.c, Jarek Burczynski, Tatsuyuki Satoh), tel que dans libvgm (c5c03a2). Modifié pour ROMsounds (marqué « ROMsounds ») : YM3812 et YM3526, tables calculées à l'avance (`fmopl_tables.h`, `tools/gen_fmopl_tables.c`), fréquence native, une seule puce statique, numéro d'opérateur calculé | GPL-2.0-or-later (en-tête du fichier) ; utilisé ici sous GPL-3.0 |
| Dans `firmware/src/ota.c` : `ota_boot_valid()` et la règle de l'étape 2 (« tout programme de mise à jour bien formé », pour revenir au firmware officiel V16), repris de [felucca-bsp](https://github.com/hugelton/felucca-bsp) `chip/ota.c` (9670b5d) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, MPL-2.0 ; combiné à ce projet GPL-3.0 comme le permet la MPL 2.0 (§ 3.3) |

La plateforme, ses auteurs et ses licences, inchangés :

---

# Licensing

Dinghy is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The SMK-37 board layer: `firmware/hal/smk_input.h`, `fm1_flash_map.h`, the SMK-37 parts of `fm1_cc.h`, `fm1_flash.h`, `fm1_lcd_hw.h`, `lcd.c`, `ota.c`, `bootguard.h`, `firmware/loader/`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `tools/build.py`; `firmware/src/smk_wheel.h`; `tests/ota_test.c`, `ldr_test.c`, `install_test.py`, `smk_wheel_test.c`, `smk_pkg_test.py` | Copyright (C) 2026 M. Geisler (genkigenki/makkuro), from [genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) branch `smk37` (the reference port of the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit) |
| The TRS MIDI IN `firmware/src/midi_uart.c` | Felucca's, as sloopDX carries it, with the ring read by content from Felucca [Salt] 0.9-salt14 by ChanceTheMaker (found by keremimo) |
| The update loader's screen: `firmware/loader/ldr_screen.c`, `tests/ldr_screen_test.c` | Copyright (C) 2026 zvenson (from OberVave, the Firmware Hub's look); the idea, a loader that draws, its 5 x 7 letters and the order of things from [Skiff](https://github.com/illuminus5/skiff) by illuminus5, GPL-3.0 |
| From OberVave: the storage (blobs, slots), `LCD_IDLE`, the SMK-37 wiring of `platform.c`, `store.h`, the pad event queue in `smk_input.h`, `tests/smk_flash_test.c`, `tests/run_tests.sh`, the CI workflow | Copyright (C) 2026 zvenson |
| The app `firmware/src/dinghy.c`, the wiring `platform.c`, the host mode `tests/`, the docs | zvenson |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The logo

`assets/logo.svg` / `logo.png` carry the designburg signet: Copyright (C) designburg, all rights reserved, not under
the GPL. It marks Dinghy itself; a fork uses its own logo.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE", "FM-1", "SMK-37" and "MKE-P37"
belong to their owners. Dinghy is not made or endorsed by them.
