<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Français** · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

</div>

> **Dépôt officiel**
>
> Roomcut est créé et maintenu par [habinsong](https://github.com/habinsong). Une copie, un miroir, une nouvelle marque ou un projet ressemblant n'est pas affilié à Roomcut sauf indication explicite dans ce dépôt. Le code source est proposé sous licence Apache 2.0. Le nom Roomcut, son identité, les captures et la documentation sont © 2026 송하빈 et ne relèvent pas de cette licence.

Roomcut est une application macOS native qui contrôle et calibre l'ensemble du son système via son propre pilote audio virtuel (CoreAudio HAL). Sans nécessiter d'outils tiers comme BlackHole, il applique un traitement DSP de précision à toutes vos applications avec une interface élégante et intuitive inspirée d'iOS.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut Accueil" width="200"><br><sub>Accueil</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut Barre des menus" width="270"><br><sub>Barre des menus</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut Mode compact" width="220"><br><sub>Mode compact</sub></td>
</tr>
</table>
</div>

## Fonctionnalités clés

- **Égaliseur système global** : Égaliseur graphique 10 bandes + paramétrique 6 bandes (avec égalisation dynamique) et 5 potentiomètres macro (Bass, Warmth, Vocal, Clarity, Air).
- **Espace stéréo (Stereo Space)** : Élargit ou resserre le signal latéral sans altérer la voix centrale (commandes Focus/Space) et Crossfeed pour casque.
- **Comparaison A/B à niveau égalisé** : Bascule instantanée entre deux réglages avec alignement automatique de volume (pondération K) pour comparer le timbre sans biais de niveau.
- **Room Tune** : Mesure les résonances acoustiques de la pièce via le micro de l'iPhone (Continuité) et génère des préréglages d'atténuation précis.
- **25 préréglages intégrés** : Profils optimisés pour haut-parleurs, casques et AirPods, avec import/export de fichiers JSON personnalisés.
- **Now Playing & Inspect** : Contrôles de lecture dans la barre des menus, paroles synchronisées via [LRCLIB](https://lrclib.net) et télémétrie audio en direct (Crête, RMS, corrélation, latence).
- **Multilingue** : Français, English, 한국어, 日本語, Deutsch.

## Installation

Téléchargez la dernière version `Roomcut-1.1.0.pkg` ou `Roomcut-1.1.0.dmg` depuis les [Releases](https://github.com/habinsong/roomcut/releases).

> **Avis signature ad-hoc**<br>
> Si macOS bloque le premier lancement, accédez à **Réglages Système → Confidentialité et sécurité → Ouvrir quand même**.

Installation via Terminal :
```sh
sudo installer -pkg Roomcut-1.1.0.pkg -target /
```

### Compiler depuis les sources
```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

## Exigences système & Remarques

- **Configuration requise** : Mac Apple Silicon sous macOS 26 (Tahoe) ou supérieur.
- **Format audio** : Stéréo 2 canaux uniquement (les flux multicanaux sont automatiquement mixés par macOS).
- **Traitement global** : S'applique à l'ensemble du système ; le filtrage application par application n'est pas pris en charge.
- **Démon d'arrière-plan** : Le moteur DSP fonctionne comme un démon système (`RoomcutAudioEngine`), assurant la continuité audio même lorsque la fenêtre est fermée.
- **Confidentialité** : Aucun signal audio ne quitte votre Mac. Les accès réseau sont strictement limités à la récupération des paroles (LRCLIB).

## Désinstallation

```sh
sudo bash scripts/uninstall-driver.sh
```
Rétablit la sortie audio d'origine, supprime le pilote virtuel et redémarre CoreAudio.

## Licence

Apache License 2.0. Voir [LICENSE](../LICENSE) and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
