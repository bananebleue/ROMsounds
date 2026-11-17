# ROMsounds

Extracteur de musiques de ROM de vieux jeux (Game Boy, Mega Drive, Super Nintendo…) et firmware lecteur pour le
synthé M-VAVE FM-1. Fiche du projet : `bananebleue/Clodio`, `projets/extracteur-musiques-rom.md` (ticket #1).

## État : banc d'essai du changement de firmware

Pour l'instant, le firmware ROMsounds est un **banc de mesure** : une puce YM2612 (Mega Drive) simulée par Nuked-OPN2,
dont l'écran affiche le coût de calcul (en % du temps disponible). Les touches jouent des notes, PLAY un accord sur
les 6 voies. Il sert à savoir si le FM-1 peut simuler la puce, avant d'écrire le lecteur.

| | |
| --- | --- |
| `fm1/` | firmware : Dinghy 0.5 (zvenson/dinghy ea1b6bf) + `ota.c` corrigé (retour au firmware officiel, règle de felucca-bsp) ; app `firmware/src/romsounds.c` ; identité `FM-1_950` (95N, à vérifier dans le Firmware Hub avant toute publication) ; `STORE_MAGIC` « RSND » |
| `web/` | page d'installation (Web MIDI, Chrome ou Edge) : ROMsounds, un fichier `.fwsc`, ou le firmware officiel |
| `tests/fm1ota_test.js` | l'installateur web contre un FM-1 simulé : `node tests/fm1ota_test.js` |
| `.github/workflows/romsounds.yml` | compilation, bancs d'essai, tests de changement de firmware (Dinghy publié par-dessus ROMsounds, officiel V16 par-dessus ROMsounds), publication sur GitHub Pages |

## Publication

Page : https://bananebleue.github.io/ROMsounds/ (domaine prévu : https://romsounds.bananebleue.com, ticket Clodio #7).

À chaque envoi sur `main`, GitHub Actions compile le firmware et publie sur GitHub Pages la page, `romsounds.fwsc`
et `latest.json` (taille, SHA-256, commit). La page se trouve à l'adresse indiquée dans Settings → Pages.

Prérequis : Settings → Pages → Source : **GitHub Actions** (fait). Le dépôt est public : Pages sur un dépôt privé demande GitHub Pro.

## Pièges connus

Voir le skill `m-vave-fm1` (dépôt Clodio, `skills/m-vave-fm1/SKILL.md`).
