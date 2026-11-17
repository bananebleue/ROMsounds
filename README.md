# ROMsounds

Extracteur de musiques de ROM de vieux jeux (Game Boy, Mega Drive, Super Nintendo…) et firmware lecteur pour le
synthé M-VAVE FM-1. Fiche du projet : `bananebleue/Clodio`, `projets/extracteur-musiques-rom.md` (ticket #1).

## État : lecteur Mega Drive, Master System, Game Gear, Game Boy, NES

Le firmware ROMsounds joue des musiques au format **VGM** : la page (section « Lecteur ») lit un fichier `.vgm` ou
`.vgz`, le prépare (banque d'échantillons à part, musique compressée, flux DAC convertis) et l'envoie au FM-1 par USB.
Le FM-1 le joue aussitôt et le **garde en mémoire flash** (bibliothèque d'environ 290 Ko, gardée après extinction).

- YM2612 : le cœur de MAME / Genesis Plus GX 1.5.1 (via libvgm), porté pour le FM-1 ; ~60 % du temps de calcul.
- SN76489 (PSG) : écrit pour ROMsounds ; bruit réglable (Sega, TI), stéréo Game Gear. DAC : banque PCM, 0x8n / 0xE0 ;
  flux DAC des VGM 1.60 (0x90-0x95) rejoués par la page en écritures DAC, comme libvgm.
- Game Boy (`gb_apu.h`) et NES (`nes_apu.h`, DMC compris) : écrits pour ROMsounds ; niveaux à 1 % de libvgm sauf le
  bruit et le DMC de la NES (à ~1 dB). Pas encore : le FDS, les puces d'extension des cartouches (VRC6, N163…),
  les échantillons DMC réécrits pendant le morceau (la page garde la dernière version de chaque adresse).
- Niveaux et rendu comparés à libvgm (`vgm2wav`) : même enveloppe, mêmes niveaux.
- Sur le FM-1 : PLAY pause / lecture ; REC revenir au début ; OCT− / OCT+ ou SELECT : morceau précédent / suivant ;
  REC maintenu 2 s : retirer le morceau de la bibliothèque. Au démarrage, le dernier morceau joué est prêt.
- Bibliothèque : FM-1 0x97000-0xE0000 (le magasin de données de la famille Felucca / Dinghy) ; installer un autre
  firmware peut l'effacer (le FM-1 le voit et repart d'une bibliothèque vide).

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
