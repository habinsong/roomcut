<div align="center">

<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

**Réduisez la pièce. Gardez le son.**

[English](../README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Français** · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge) ![Core Audio plug-in](https://img.shields.io/badge/Core%20Audio-Plug--In-8957E5?style=for-the-badge)

![System-wide](https://img.shields.io/badge/System--wide-EQ%20%2B%20DSP-0A84FF?style=for-the-badge) ![EQ](https://img.shields.io/badge/EQ-10--band%20%2B%20Parametric-5E5CE6?style=for-the-badge) ![Spatial](https://img.shields.io/badge/Spatial-narrow%20%C2%B7%20widen-1F6FEB?style=for-the-badge) ![Room Tune](https://img.shields.io/badge/Room%20Tune-iPhone%20mic-2EA043?style=for-the-badge) ![Now Playing](https://img.shields.io/badge/Now%20Playing-Lyrics-C2185B?style=for-the-badge) ![UI languages](https://img.shields.io/badge/UI-5%20languages-FB8500?style=for-the-badge)

![Native tests](https://img.shields.io/badge/ctest-passing-2EA043?style=for-the-badge) ![Swift tests](https://img.shields.io/badge/swift%20test-passing-2EA043?style=for-the-badge) ![Driver](https://img.shields.io/badge/HAL%20driver-loads-0A84FF?style=for-the-badge)

Roomcut 1.0 propose actuellement : routage à l'échelle du système, EQ, limiteur, contrôles spatiaux, analyseur, préréglages, Now Playing et Room Tune.<br>Testé sur des Mac Apple Silicon sous macOS 26+.

</div>

> **Avis de dépôt officiel**
>
> Ceci est le dépôt officiel de **Roomcut**, créé et maintenu par [habinsong](https://github.com/habinsong).
>
> Les dépôts, paquets, fiches de places de marché, sites web, services ou autres projets qui copient, reproduisent, renomment ou imitent de près la documentation d'origine de ce dépôt, le texte du README, les descriptions d'architecture, les descriptions de fonctionnalités, les captures d'écran, les concepts d'interface, les métadonnées du projet ou tout autre élément original ne sont pas affiliés à ce projet, sauf indication explicite dans ce dépôt.
>
> Le code source est distribué sous licence Apache 2.0 (voir [LICENSE](../LICENSE)). Le nom Roomcut, l'identité visuelle, les captures d'écran et la documentation sont © 2026 송하빈 et ne sont pas couverts par cette licence.

Roomcut est un processeur audio à l'échelle du système pour macOS. Il ajoute un périphérique de sortie virtuel, fait passer tout ce que joue votre Mac par une chaîne de DSP en temps réel, puis l'envoie vers les enceintes, le casque ou le DAC que vous utilisez réellement. Il s'appuie sur un Audio Server Plug-in CoreAudio natif : aucun pilote de bouclage comme BlackHole ou Soundflower n'est nécessaire.

La plupart des « amplificateurs audio » ne font qu'élargir le son vers l'extérieur. Roomcut va dans les deux sens, et vous décidez jusqu'où. Focus atténue l'ambiance de pièce excessive et l'étalement stéréo pour rapprocher les voix et les dialogues de l'enregistrement ; Widen ouvre la scène quand vous voulez plus d'espace. Réduisez, élargissez, ou placez-vous n'importe où entre les deux.

---
#### En bref, Roomcut offre à vos oreilles une musique agréable à écouter, <br> tandis que son UI/UX en Apple Liquid Glass est belle rien qu'à regarder.
---

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home.png" alt="Fenêtre principale" width="200"><br><sub>Fenêtre principale</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/menubar.png" alt="Barre des menus" width="270"><br><sub>Barre des menus</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/compact_mode.png" alt="Mode compact" width="220"><br><sub>Mode compact</sub></td>
</tr>
</table>
</div>

## Fonctionnalités

- **EQ global.** Un égaliseur graphique 10 bandes et un égaliseur paramétrique 6 bandes (cloche, shelf, passe-haut/bas, notch) empilés, avec une courbe de réponse en direct.
- **Macros.** Les boutons Bass, Warmth, Vocal, Clarity et Air déplacent les bonnes bandes à votre place, pour ne pas avoir à raisonner en fréquences.
- **Spatial, dans les deux sens.** Resserrez ou élargissez l'image, librement : Space (largeur), Center (focalisation du centre fantôme), Damping (réduction de la pièce) et crossfeed / diaphonie. Choisissez le mode enceinte ou casque, activez le surround, et une vue du champ stéréo réagit en direct quand vous ajustez ; Focus et Widen sont des préréglages en un geste.
- **Limiteur et gain.** Préampli, ajustement de sortie et limiteur de crête, pour qu'une courbe d'EQ marquée n'écrête pas la sortie.
- **Compatible haute résolution.** Traitement interne en 32 bits flottant, avec choix de la fréquence d'échantillonnage et de la profondeur de bits du périphérique de sortie ; la carte Now Playing affiche le format et la latence en direct.
- **Analyseur.** Crête, RMS, largeur stéréo, centroïde spectral en direct, un affichage de spectre et un libellé en langage clair pour ce qui est en cours de lecture.
- **Préréglages.** Une bibliothèque intégrée classée par Signature, matériel Apple, Speakers et Headphones, plus vos propres préréglages enregistrés.
- **Room Tune.** Mesurez votre pièce avec un iPhone via le micro de continuité et obtenez des corrections d'EQ prudentes pour ses pires résonances. Il atténue uniquement, ne renforce jamais, et enregistre le résultat comme préréglage.
- **Now Playing.** Thèmes pilotés par la pochette, paroles synchronisées via [LRCLIB](https://lrclib.net) et commandes de lecture dans la fenêtre de la barre de menus.
- **Interface localisée.** Anglais, coréen, japonais, français et allemand, selon la langue du système ou un choix manuel.
- **Repli sûr.** Si le moteur plante, votre sortie revient à un périphérique réel, et l'audio brut ne quitte jamais votre Mac.

## Fonctionnement

macOS envoie l'audio vers « Roomcut Output », un périphérique virtuel. Le pilote est la partie mince : il vit dans `coreaudiod`, ne fait aucun DSP et se contente de transmettre les trames entrantes à un processus auxiliaire via un tampon circulaire partagé. L'auxiliaire, `RoomcutAudioEngine`, exécute la chaîne de DSP et rend vers votre périphérique de sortie réel.

```
Audio système
  → Roomcut Output            périphérique virtuel
  → Roomcut.driver            Audio Server Plug-in, en bac à sable dans coreaudiod
  → tampon circulaire partagé transmis via un service Mach
  → RoomcutAudioEngine        DSP + rendu
  → enceintes / AirPods / DAC / HDMI
```

Un Audio Server Plug-in s'exécute en bac à sable dans `coreaudiod` et ne peut ouvrir ni socket ni mémoire partagée arbitraire. Roomcut amorce le tampon circulaire via une connexion `AudioServerPlugIn_MachServices`, la même approche que Background Music.

| Composant | Rôle | Langage |
|---|---|---|
| `Roomcut.app` | Application de barre de menus et interface Now Playing | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | Périphérique de sortie virtuel (Audio Server Plug-in) | C |
| `RoomcutAudioEngine` | Auxiliaire en arrière-plan : DSP et rendu | C++ |
| `RoomcutCore` | DSP, préréglages et analyse partagés | C++ |
| `RoomcutNowPlaying.dylib` | Pont MediaRemote pour Now Playing | Objective-C |

## Prérequis

- macOS 26 (Tahoe) ou plus récent. L'interface utilise les API Liquid Glass du système.
- Apple Silicon.
- Pour compiler : Xcode 26 (il fournit le SDK macOS 26) et CMake.

## Installation

Il y a deux voies. Compiler depuis les sources est celle que je recommande pour l'instant, car elle évite les invites de Gatekeeper liées aux binaires téléchargés et non notarisés.

### Compiler depuis les sources

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

# pilote + moteur natifs
cmake -S . -B build
cmake --build build

# l'application de barre de menus
bash scripts/build-app.sh

# installer le pilote + le moteur (demande votre mot de passe, redémarre coreaudiod)
sudo bash scripts/install-driver.sh
```

Ouvrez `build/Roomcut.app` (elle réside dans la barre de menus, sans icône dans le Dock), puis choisissez « Roomcut Output » dans Réglages Système ▸ Son, ou laissez l'application le faire. L'installation redémarre `coreaudiod`, l'audio du système se coupe donc environ une seconde.

### Version précompilée

Téléchargez le dernier `.pkg` (double-clic) ou `.zip` (terminal) depuis les [Releases](https://github.com/habinsong/roomcut/releases). Les versions précompilées sont actuellement signées en ad-hoc et non notarisées ; macOS peut donc demander une approbation au premier lancement : faites un clic droit sur le `.pkg` et choisissez Ouvrir, ou lancez `sudo installer -pkg Roomcut-*.pkg -target /`. Le `.zip` fournit un `install.sh` qui s'occupe du reste. Compiler depuis les sources évite cette étape d'approbation.

## Utilisation

Une fois le pilote installé et l'application ouverte :

1. Choisissez **Roomcut Output** dans Réglages Système ▸ Son (ou laissez l'application le faire). Tout ce que joue votre Mac passe désormais par Roomcut.
2. Définissez le périphérique réel vers lequel Roomcut rend (vos enceintes, votre casque ou votre DAC) et activez Roomcut. Désactivé, c'est un contournement propre.
3. Partez d'un préréglage adapté à votre matériel, puis affinez.

La fenêtre comporte cinq onglets :

- **Home.** Now Playing, l'interrupteur on/off et une feuille de contrôles audio que l'on tire vers le haut : macros Bass / Warmth / Vocal / Clarity / Air, volume et sélecteur de préréglages. Tirez davantage pour l'EQ complet 10 bandes et paramétrique.
- **Space.** resserrez ou élargissez l'image stéréo, réglez la focalisation du centre, l'amortissement de la pièce et le crossfeed, avec des préréglages Focus / Widen pour enceintes ou casque.
- **Tune.** mesurez votre pièce avec un iPhone (micro de continuité) et appliquez des corrections d'EQ prudentes.
- **Inspect.** indicateurs en lecture seule : crête, limiteur, coupures, corrélation, largeur, fréquence d'échantillonnage, latence et état du moteur.
- **Settings.** périphérique et format de sortie, volume, lancement à l'ouverture de session, apparence (thème, disposition, langue) et cache des paroles.

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_space.png" alt="Space" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_start.png" alt="Tune" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_measuring.png" alt="Tune, mesure" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_result.png" alt="Tune, résultat" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_inspect.png" alt="Inspect" width="150">
</div>

La fenêtre se redimensionne par n'importe quel bord ou coin, et les touches ↑ / ↓ ouvrent et ferment la feuille de contrôles audio dans Home.

La fine barre de poignée en haut déplace la fenêtre. Cliquez une fois pour replier la fenêtre complète en une carte Now Playing compacte ; en mode compact, cliquez de nouveau sur la poignée pour ranger la fenêtre (Roomcut continue de tourner dans la barre des menus, il ne quitte pas), puis touchez la carte pour la rouvrir. L'interrupteur de cette barre épingle la fenêtre au-dessus de toutes les autres applications pour qu'elle ne soit jamais masquée. Roomcut vit aussi dans la barre des menus sous la forme d'un petit popover Now Playing avec des commandes de lecture.

### Basic et Advanced

La feuille de contrôles audio de **Home** a deux modes :

- **Basic** reste simple : les cinq boutons macro (Bass, Warmth, Vocal, Clarity, Air), un curseur de volume qui peut dépasser 100 % jusqu'à 200 %, une courbe de résumé d'EQ et le sélecteur de préréglages.
- **Advanced** ouvre l'ensemble complet sur cinq sous-onglets :
  - **graph.** La réponse d'EQ combinée en une seule courbe en lecture seule.
  - **10-Band.** L'égaliseur graphique classique ; tirez chaque bande à la main.
  - **Parametric.** Six bandes biquad (cloche, shelf, passe-haut/bas, notch) avec fréquence, gain et Q.
  - **Limiter.** Le limiteur de crête plus le préampli et le gain de sortie.
  - **Analyzer.** Spectre en direct avec crête, RMS, largeur stéréo et centroïde spectral.

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_basic.png" alt="Contrôles Basic" width="240"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_advanced.png" alt="Contrôles Advanced" width="240">
</div>

### Apparence

Réglages ▸ Apparence propose trois réglages rapides :

- **Auto / Light / Dark.** Suivre le système, ou forcer un thème clair ou sombre.
- **Halo / Cover / Mesh Gradient.** L'arrière-plan Now Playing : un halo lumineux discret autour de la carte (Halo), la pochette qui remplit toute la fenêtre (Cover), ou un dégradé maillé animé tiré des couleurs de la pochette (Mesh Gradient).
- **Card / Poster.** La disposition Now Playing : une seule carte centrée (Card), ou une pochette pleine largeur (Poster).

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_settings.png" alt="Réglages, Apparence" width="240">
</div>

## Désinstallation

```sh
sudo bash scripts/uninstall-driver.sh
```

Cela supprime le pilote et le moteur, restaure votre périphérique de sortie précédent, puis redémarre `coreaudiod`. Si le son reste muet ensuite, choisissez un périphérique à la main dans Réglages Système ▸ Son.

## Dépannage

**« Roomcut Output » n'apparaît pas dans les réglages Son.** Le pilote se charge au redémarrage de `coreaudiod`. Relancez `sudo bash scripts/install-driver.sh` (il s'en charge), ou redémarrez. Un pilote téléchargé ne doit pas non plus être en quarantaine ; le `.pkg` et `install.sh` s'en occupent.

**Aucun son après l'installation.** Vérifiez que le périphérique de sortie réel de Roomcut est défini et que Roomcut est activé. S'il reste muet, lancez `sudo bash scripts/reset-audio-output.sh`, ou choisissez un périphérique à la main dans Réglages Système ▸ Son.

**L'application ne s'ouvre pas (« développeur non identifié »).** Les versions précompilées ne sont pas encore notarisées. Faites un clic droit sur l'application ▸ Ouvrir, ou autorisez-la dans Réglages Système ▸ Confidentialité et sécurité. Compiler depuis les sources évite cela.

**Les paroles ne s'affichent pas pour un morceau.** Les paroles proviennent de LRCLIB, mises en correspondance par titre, artiste et durée. macOS n'expose pas les paroles propres à une application de streaming ; un morceau peut donc avoir des paroles dans Apple Music mais pas ici si LRCLIB ne l'a pas.

**Les contrôles spatiaux sont grisés.** Ils nécessitent une version du moteur qui les prend en charge. Réinstallez le moteur le plus récent depuis les sources.

## Confidentialité

Roomcut traite tout ce que vous entendez, donc les limites comptent : l'audio brut ne quitte jamais votre machine, le DSP et l'analyse s'exécutent localement, et les journaux conservent des compteurs et des noms de périphériques plutôt que des échantillons. Room Tune n'utilise le micro de l'iPhone que pendant une mesure.

## Compilation et tests

```sh
swift test                          # tests unitaires de l'app / Swift
ctest --test-dir build --output-on-failure   # tests natifs (DSP / moteur)
```

Les deux suites de tests s'exécutent dans la CI à chaque push et pull request, sur un runner macOS 26 :

[![CI](https://github.com/habinsong/roomcut/actions/workflows/ci.yml/badge.svg)](https://github.com/habinsong/roomcut/actions/workflows/ci.yml)

## Contribuer

Les issues et les pull requests sont les bienvenues. Les étapes ci-dessus vous donnent un arbre de travail fonctionnel ; si vous vous lancez sur quelque chose, ouvrir d'abord une issue fait généralement gagner du temps à tout le monde.

## Paroles et crédits

Les informations de piste (titre, artiste, pochette) proviennent des métadonnées Now Playing du système. Les paroles synchronisées sont mises en correspondance depuis [LRCLIB](https://lrclib.net) par titre, artiste et durée, récupérées à la demande et mises en cache localement ; Roomcut s'identifie avec un `User-Agent` et n'inclut ni ne redistribue jamais de paroles dans ce dépôt. Les paroles appartiennent à leurs détenteurs respectifs.

macOS ne transmet pas les paroles internes d'une application de streaming aux autres applications, donc Roomcut apporte les siennes via LRCLIB. Un morceau peut avoir des paroles dans Apple Music mais pas ici si LRCLIB ne possède pas encore cette piste.

Le serveur et le client de LRCLIB sont sous licence MIT, et Roomcut ne communique avec lui que via son API HTTP publique ; aucun de ce code n'est livré ici.

## Licence

Roomcut est distribué sous licence Apache 2.0. Voir [LICENSE](../LICENSE) pour le texte complet. La licence couvre le code source ; le nom et l'identité visuelle de Roomcut n'en font pas partie, car Apache 2.0 n'accorde pas de droits de marque.
