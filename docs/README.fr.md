<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Français** · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

</div>

> **Dépôt officiel**
>
> Roomcut est créé et maintenu par [habinsong](https://github.com/habinsong). Une copie, un miroir, une nouvelle marque ou un projet ressemblant n'est pas affilié à Roomcut sauf indication explicite dans ce dépôt. Le code source est proposé sous licence Apache 2.0. Le nom Roomcut, son identité, les captures et la documentation sont © 2026 송하빈 et ne relèvent pas de cette licence.

macOS n'a pas d'égaliseur système. Si vos enceintes sortent 6 dB de trop à 120 Hz, vous
corrigez ça dans l'application qui embarque un égaliseur, et nulle part ailleurs.

Roomcut ajoute une sortie audio appelée **Roomcut Output**. Envoyez-y la sortie de macOS et
tout passe par une chaîne DSP avant d'arriver à vos enceintes : Spotify, Safari, Zoom, les
sons du système. Ce périphérique virtuel est un CoreAudio Audio Server Plug-in qui se trouve
dans ce dépôt, donc pas de BlackHole ni de Soundflower à installer en dessous.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Accueil Roomcut" width="200"><br><sub>Accueil</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Barre des menus Roomcut" width="270"><br><sub>Barre des menus</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Mode compact Roomcut" width="220"><br><sub>Mode compact</sub></td>
</tr>
</table>
</div>

## Ce qu'il y a dedans

**EQ.** Dix bandes graphiques de 31 Hz à 16 kHz, et six bandes paramétriques par-dessus :
cloche, shelf grave et aigu, passe-haut, passe-bas, notch. Cinq macros — Bass, Warmth, Vocal,
Clarity, Air — déplacent des groupes de bandes quand vous n'avez pas envie de raisonner en
fréquences. Toute bande cloche ou shelf peut devenir dynamique : donnez-lui un seuil et une
plage, et elle ne descend que pendant que cette bande est réellement forte — pratique sur une
résonance qui ne pose problème que sur certaines notes. Un limiteur ferme la marche, avec 2 ms
de lookahead.

**Espace stéréo.** Les masters récents sont larges, et sur les haut-parleurs d'un portable la
voix décroche parfois du centre. Focus resserre les côtés jusqu'à ce qu'elle y revienne ;
Space fait l'inverse. Les deux n'agissent que sur le signal de côté, donc une voix
parfaitement centrée ressort intacte quel que soit le réglage. Cette contrainte a coûté une
réécriture : la première version élargissait en ajoutant une copie du mid à phase tournée,
compatible mono mais pas stable en image, et la voix partait vers la gauche à mesure qu'on
poussait le curseur. Space va jusqu'à ±200, Center et Damping jusqu'à 200, avec les mêmes
courbes qu'avant : un réglage que vous aimiez sonne toujours pareil. Crossfeed et le sélecteur
enceintes/casque sont dans le même onglet.

**A/B à niveau égalisé.** Deux emplacements avec un historique de modification indépendant.
`⌘Z` et `⇧⌘Z` s'appliquent au côté affiché, et le bouton de copie transmet le réglage actuel à
l'autre côté. La bascule s'effectue via une rampe de 15 ms. Lorsque l'égalisation de niveau est
activée, les deux chaînes — limiteur compris — sont mesurées sur le même intervalle avec une
pondération K, et la plus forte est atténuée. Le but est de comparer le timbre sans être trompé
par le volume.

**Room Tune.** Un iPhone sert de micro de mesure via Continuity Camera. Roomcut joue des
balayages, cherche les résonances nettes, propose uniquement des atténuations et enregistre le
résultat comme préréglage. Voir plus bas ce que ce n'est pas.

**Préréglages.** 25 fournis, répartis en Signature, Apple, Speakers et Headphones. Vous
enregistrez les vôtres, vous les échangez en JSON, et vous en épinglez un par sortie : brancher
le casque ramène la bonne courbe.

**Now Playing et Inspect.** La fenêtre de la barre des menus affiche la pochette, les commandes
de lecture et les paroles synchronisées de [LRCLIB](https://lrclib.net). Inspect ne fait que
lire : crête, RMS, largeur stéréo, corrélation, fréquence d'échantillonnage, activité du
limiteur, décrochages, et deux latences distinctes — celle du périphérique, et celle que
Roomcut ajoute lui-même (le lookahead du limiteur, plus la conversion de fréquence s'il y en a).

Langues de l'interface : anglais, coréen, japonais, français, allemand. Roomcut suit la langue
du système, sauf si vous en choisissez une dans Settings.

## Installation

Prenez `Roomcut-1.1.0.pkg` dans [Releases](https://github.com/habinsong/roomcut/releases).
`Roomcut-1.1.0.dmg` contient exactement le même paquet dans une image disque.

Ces builds sont signés ad-hoc. Aucun certificat Developer ID derrière, pas de notarisation :
macOS bloquera le premier lancement. Ouvrez le paquet une fois quand même, puis allez dans
**Réglages Système → Confidentialité et sécurité → Ouvrir quand même**. Le bouton n'apparaît
qu'après le blocage, et c'est là que la plupart des gens s'arrêtent.

Vous pouvez aussi contourner Gatekeeper, `installer` ne le consulte pas :

```sh
sudo installer -pkg Roomcut-1.1.0.pkg -target /
```

Si macOS dit que le paquet est **endommagé** plutôt que non vérifié, le problème est ailleurs :
téléchargement corrompu ou signature cassée. Vérifiez d'abord ce que vous avez :

```sh
shasum -a 256 Roomcut-1.1.0.pkg
# 2f45b09f446f42c3c8f3f7649dccf910f6a629785f25152d045b635b7431d693
shasum -a 256 Roomcut-1.1.0.dmg
# b0337c5df3b7a45c36785d27597d61f67719532fb1b3db2137d2eea110fe1cdb
```

L'installateur place l'app dans `/Applications`, le pilote dans le dossier HAL du système et un
moteur d'arrière-plan sous `/Library/Application Support/Roomcut`. Il relance ensuite
`coreaudiod`, donc tout le son du Mac s'arrête une seconde environ. Roomcut n'a pas d'icône
dans le Dock : il s'ouvre dans la barre des menus. Choisissez **Roomcut Output** dans Réglages
Système → Son, ou laissez l'app le faire, puis désignez la sortie réelle et activez le
traitement.

### Depuis les sources

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

Il vous faut Xcode 26 et CMake. Enlevez la dernière ligne si vous préférez ne pas toucher à
votre configuration audio : jusque-là, tout reste dans `build/`.

## Ce qu'il ne fait pas

- **Les Mac Intel et les anciens systèmes.** Apple Silicon et macOS 26 (Tahoe) minimum.
- **Le multicanal.** Le périphérique virtuel est stéréo. macOS mixe le surround avant que
  Roomcut ne le voie.
- **Le traitement par application.** S'applique à l'ensemble de la sortie système. Il n'est pas possible d'activer ou désactiver le traitement application par application.
- **La correction acoustique sérieuse.** Room Tune mesure avec le micro d'un téléphone, à un
  point de la pièce. Le micro n'est pas plat, un point n'est pas une pièce, et le résultat est
  un point de départ que vos oreilles valident ou non.
- **Le Mac App Store.** Now Playing lit le framework privé MediaRemote d'Apple. Roomcut ne peut
  donc pas y être publié, et une mise à jour de macOS peut casser ce panneau alors que le
  chemin audio continue de fonctionner.
- **Disparaître quand vous quittez l'app.** Le DSP tourne dans un démon d'arrière-plan. Quitter
  l'app laisse le son y passer ; c'est la désinstallation qui l'enlève.

## Comment c'est assemblé

```text
Audio système
  → Roomcut Output                 périphérique virtuel
  → Roomcut.driver                 CoreAudio Audio Server Plug-in
  → tampon circulaire en mémoire partagée
  → RoomcutAudioEngine             DSP et rendu
  → enceintes / casque / DAC / HDMI
```

| Composant | Rôle | Langage |
|---|---|---|
| `Roomcut.app` | App de barre des menus et fenêtre Now Playing | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | Sortie virtuelle | C |
| `RoomcutAudioEngine` | DSP et rendu audio | C++ |
| `RoomcutCore` | DSP, analyse, préréglages | C++ |
| `RoomcutNowPlaying.dylib` | Pont Now Playing | Objective-C |

C'est le moteur qui possède l'audio, pas l'app. Il tourne en LaunchDaemon, conserve son propre
fichier d'état et surveille à la fois l'index d'écriture et le heartbeat du pilote. C'est la
seule façon de distinguer « rien ne joue » de « le pilote a disparu ». Une panne le
2026-08-01 a motivé une surveillance supplémentaire : un DAC iFi s'est ouvert en 48 kHz puis
immédiatement réouvert en 384 kHz, et à partir de là le rappel de rendu tournait à 3,37 fois le
temps réel. Le tampon s'est vidé, les coupures se sont accumulées. Périphérique inchangé, taux
d'échantillonnage inchangé en apparence : aucun contrôle ne bronchait. Maintenant le moteur mesure
le débit réel des trames et reconstruit l'unité de sortie si les chiffres deviennent incohérents.

## Confidentialité

Aucun signal audio ne quitte votre Mac. Les journaux ne contiennent que des compteurs et des
noms de périphériques, jamais d'échantillons audio. Room Tune n'active le micro de l'iPhone que
pendant une mesure. Le réseau ne sert qu'à la récupération des paroles : le titre, l'artiste et
la durée sont envoyés à LRCLIB, et la réponse est mise en cache dans
`~/Library/Caches/com.habinsong.roomcut/lyrics.json`.

## Compiler et tester

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

43 tests natifs et 257 tests Swift à ce commit. Ce qu'ils ne couvrent pas : une vraie pièce, un
vrai micro, un second Mac. [docs/development](development/README.md) contient les rapports de
vérification par domaine, y compris les limites encore ouvertes.

## Désinstallation

```sh
sudo bash scripts/uninstall-driver.sh
```

L'arrêt du moteur remet la sortie système sur le périphérique qu'il utilisait, puis le pilote
est supprimé et `coreaudiod` redémarre. Si le Mac reste muet, rechoisissez une sortie dans
Réglages Système → Son.

## Licence

Apache License 2.0 — voir [LICENSE](../LICENSE). Les attributions et mentions de marques sont
dans [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) ; les deux fichiers sont livrés dans
l'app et dans l'installateur.
