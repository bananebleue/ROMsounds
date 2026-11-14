# ROMsounds

Extracteur de musiques de ROM de vieux jeux (Game Boy, Mega Drive, Super Nintendo…) et firmware lecteur pour le
synthé M-VAVE FM-1. Fiche du projet : `bananebleue/Clodio`, `projets/extracteur-musiques-rom.md` (ticket #1).

## État : banc d'essai du changement de firmware

Pour l'instant, le firmware ROMsounds est Dinghy 0.5 renommé (un simple synthé sinus sur les touches), avec sa propre
identité. Il sert à vérifier qu'on peut passer d'un firmware maison à un autre depuis le téléphone, avant d'écrire le
lecteur.

| | |
| --- | --- |
| `fm1/` | firmware : Dinghy 0.5 (zvenson/dinghy ea1b6bf) + `ota.c` corrigé (retour au firmware officiel, règle de felucca-bsp) ; app `firmware/src/romsounds.c` ; identité `FM-1_950` (95N, à vérifier dans le Firmware Hub avant toute publication) ; `STORE_MAGIC` « RSND » |
| `web/` | page d'installation (Web MIDI, Chrome ou Edge) : ROMsounds, Spring's Sonar, un fichier `.fwsc`, ou le firmware officiel |
| `tests/fm1ota_test.js` | l'installateur web contre un FM-1 simulé : `node tests/fm1ota_test.js` |
| `.github/workflows/romsounds.yml` | compilation, bancs d'essai, tests de changement de firmware (ROMsounds ↔ Spring's Sonar, officiel V16 par-dessus ROMsounds), publication sur GitHub Pages |

## Publication

À chaque envoi sur `main`, GitHub Actions compile le firmware et publie sur GitHub Pages la page, `romsounds.fwsc`
et `latest.json` (taille, SHA-256, commit). La page se trouve à l'adresse indiquée dans Settings → Pages.

Prérequis : Settings → Pages → Source : **GitHub Actions**. Pour un dépôt privé, GitHub Pages demande un compte
GitHub Pro, et le site publié reste public.

## Pièges connus

Voir le skill `m-vave-fm1` (dépôt Clodio, `skills/m-vave-fm1/SKILL.md`).
