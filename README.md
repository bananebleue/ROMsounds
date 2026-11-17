# ROMsounds

Extracteur de musiques de ROM de vieux jeux (Game Boy, Mega Drive, Super Nintendo…) et firmware lecteur pour le
synthé M-VAVE FM-1. Fiche du projet : `bananebleue/Clodio`, `projets/extracteur-musiques-rom.md` (ticket #1).

## État : lecteur Mega Drive

Le firmware ROMsounds joue des musiques Mega Drive au format **VGM** : la page (section « Lecteur Mega Drive ») lit
un fichier `.vgm` ou `.vgz` et l'envoie au FM-1 par USB, qui le joue aussitôt (300 Ko au plus, en mémoire vive).

- YM2612 : le cœur de MAME / Genesis Plus GX 1.5.1 (via libvgm), porté pour le FM-1 ; ~60 % du temps de calcul.
- SN76489 (PSG) : écrit pour ROMsounds. DAC (samples) : banque PCM et commandes 0x8n / 0xE0.
- Pas encore : les flux DAC des VGM 1.60 (commandes 0x90-0x95), plusieurs banques PCM, la mémoire après extinction.
- PLAY : pause / lecture ; REC : revenir au début.

| | |
| --- | --- |
| `fm1/` | firmware : Dinghy 0.5 (zvenson/dinghy ea1b6bf) + `ota.c` corrigé (retour au firmware officiel, règle de felucca-bsp) ; app `firmware/src/romsounds.c` (lecteur), `psg.h`, `vgm.h`, `rx.h`, `firmware/ext/fmopn` ; identité `FM-1_950` (95N, à vérifier dans le Firmware Hub avant toute publication) ; `STORE_MAGIC` « RSND » |
| `web/` | page (Web MIDI, Chrome ou Edge) : envoi d'un VGM au lecteur (`romsounds.js`), installation de ROMsounds, d'un fichier `.fwsc` ou du firmware officiel |
| `tests/fm1ota_test.js` | l'installateur web contre un FM-1 simulé : `node tests/fm1ota_test.js` |
| `.github/workflows/romsounds.yml` | compilation, bancs d'essai, tests de changement de firmware (Dinghy publié par-dessus ROMsounds, officiel V16 par-dessus ROMsounds), publication sur GitHub Pages |

## Publication

Page : https://bananebleue.github.io/ROMsounds/ (domaine prévu : https://romsounds.bananebleue.com, ticket Clodio #7).

À chaque envoi sur `main`, GitHub Actions compile le firmware et publie sur GitHub Pages la page, `romsounds.fwsc`
et `latest.json` (taille, SHA-256, commit). La page se trouve à l'adresse indiquée dans Settings → Pages.

Prérequis : Settings → Pages → Source : **GitHub Actions** (fait). Le dépôt est public : Pages sur un dépôt privé demande GitHub Pro.

## Pièges connus

Voir le skill `m-vave-fm1` (dépôt Clodio, `skills/m-vave-fm1/SKILL.md`).
