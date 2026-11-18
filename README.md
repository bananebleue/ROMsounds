# ROMsounds

Extracteur de musiques de ROM de vieux jeux (Game Boy, Mega Drive, Super Nintendo…) et firmware lecteur pour le
synthé M-VAVE FM-1. Fiche du projet : `bananebleue/Clodio`, `projets/extracteur-musiques-rom.md` (ticket #1).

## État : lecteur Mega Drive, Master System, Game Gear, Game Boy, NES, Atari ST

Le firmware ROMsounds joue des musiques au format **VGM** : la page (section « Lecteur ») lit un fichier `.vgm` ou
`.vgz`, le prépare (banque d'échantillons à part, musique compressée, flux DAC convertis) et l'envoie au FM-1 par USB.
Le FM-1 le joue aussitôt et le **garde en mémoire flash** (bibliothèque d'environ 290 Ko, gardée après extinction).

- YM2612 : le cœur de MAME / Genesis Plus GX 1.5.1 (via libvgm), porté pour le FM-1 ; ~60 % du temps de calcul.
- SN76489 (PSG) : écrit pour ROMsounds ; bruit réglable (Sega, TI), stéréo Game Gear. DAC : banque PCM, 0x8n / 0xE0 ;
  flux DAC des VGM 1.60 (0x90-0x95) rejoués par la page en écritures DAC, comme libvgm.
- AY-3-8910 / YM2149 (`ay_apu.h`, d'après emu2149) : Atari ST, Amstrad CPC, ZX Spectrum ; VGM (0xA0) ou fichiers .ym
  (YM2!, YM3!, YM3b, YM5!, YM6!, compressés LHA), convertis par la page ; à 2 % de libvgm. Pas encore : SNDH, effets
  spéciaux des YM5/6 (digidrums, voix SID).
- Game Boy (`gb_apu.h`) et NES (`nes_apu.h`, DMC compris) : écrits pour ROMsounds ; niveaux à 1 % de libvgm sauf le
  bruit et le DMC de la NES (à ~1 dB). Pas encore : le FDS, les puces d'extension des cartouches (VRC6, N163…),
  les échantillons DMC réécrits pendant le morceau (la page garde la dernière version de chaque adresse).
- Niveaux et rendu comparés à libvgm (`vgm2wav`) : même enveloppe, mêmes niveaux.
- Sur le FM-1 : PLAY pause / lecture ; REC revenir au début ; OCT− / OCT+ ou SELECT : morceau précédent / suivant ;
  REC maintenu 2 s : retirer le morceau de la bibliothèque. Au démarrage, le dernier morceau joué est prêt.
- Bibliothèque : FM-1 0x97000-0xE0000 (~292 Ko, le magasin de données de la famille Felucca / Dinghy) ; installer un
  autre firmware peut l'effacer (le FM-1 le voit et repart d'une bibliothèque vide). L'écran affiche la place libre ;
  quand un trou laissé par un morceau retiré ne suffit pas, les morceaux sont tassés avant d'en retirer un ancien.
  Ordre de grandeur : un morceau Mega Drive 15-40 Ko, Game Boy ou NES 1-10 Ko (40 morceaux au plus).
- Écoute dans la page avant l'envoi : le lecteur du FM-1 compilé en WebAssembly (`fm1/preview/preview.c` + les puces
  du firmware ; `fm1/tools/build_preview.sh`, clang + wasm-ld, fait par la CI) : le même son que le FM-1, sans les effets.
- La page lit la liste des morceaux du FM-1 (SysEx 05 ; réponse : un SysEx 0x15 par morceau, puis 0x16 pour la place)
  et en retire à la demande (06 + CRC) ; quand un morceau n'entre pas, elle demande lesquels retirer avant l'envoi.
- Effets en direct (`fx.h` et `romsounds.c`) : touches = voix coupées (Solo + touche : seule), molettes et boutons
  attribuables depuis la page (le FM-1 dessiné ; SysEx 07 / 08, gardé dans le blob 1). Par défaut : KNOB1 tempo, KNOB2
  phaser / flanger, KNOB3 grain, KNOB4 filtre, ALGORITHM temps de l'écho et de la boucle, PRESETS écho (répétitions) ;
  OCT− / OCT+ hauteur, FX boucle, SEL solo, ENV backspin, LFO vinyl brake, EDIT rappeler un preset, HOME tout remettre,
  SAVE enregistrer un preset (+ touche 1-12, blob 2), ARP / SEQ beat roll ½ / 1 temps. Le tempo du beat roll : deviné
  par la page d'après les débuts de notes (RSM3), corrigeable avant l'envoi.

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
