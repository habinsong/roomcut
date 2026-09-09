<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · **Français** · [Deutsch](README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

Roomcut 1.0.9 est destiné aux Mac Apple Silicon sous macOS 26 ou version ultérieure.

</div>

> **Dépôt officiel**
>
> Roomcut est créé et maintenu par [habinsong](https://github.com/habinsong). Une copie, un miroir, une nouvelle marque ou un projet ressemblant n'est pas affilié à Roomcut sauf indication explicite dans ce dépôt. Le code source est proposé sous licence Apache 2.0. Le nom Roomcut, son identité, les captures et la documentation sont © 2026 송하빈 et ne relèvent pas de cette licence.

Roomcut est un processeur audio pour tout macOS. Il crée une sortie virtuelle,
traite ce que lit le Mac, puis l'envoie aux enceintes, au casque ou au DAC.
Le pilote repose directement sur CoreAudio : aucun pilote de boucle tel que
BlackHole ou Soundflower n'est nécessaire.

## Ajuster l'espace stéréo à ce que vous écoutez

Focus resserre une image stéréo trop diffuse; Space l'élargit. Commencez avec un
préréglage, puis ajustez chaque contrôle à la main si nécessaire.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Accueil Roomcut" width="200"><br><sub>Accueil</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Barre des menus Roomcut" width="270"><br><sub>Barre des menus</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Mode compact Roomcut" width="220"><br><sub>Mode compact</sub></td>
</tr>
</table>
</div>

## Nouveautés de la version 1.0.9

- La comparaison A/B garde un historique d'édition pour chaque côté et peut
  rapprocher leur niveau de lecture avant l'écoute. La bascule est lissée pour
  ne pas ajouter de clic lors d'une comparaison ordinaire.
- La reprise de sortie, les lectures et écritures de périphériques, ainsi que
  l'interrogation de l'app ont maintenant des responsabilités séparées. Une
  réponse tardive ou une ancienne écriture ne remplace donc pas un choix récent.
- Room Tune suit plus clairement le nettoyage d'une mesure annulée ou retardée.
  Son contournement temporaire n'est restauré que tant qu'il lui appartient.
- La version est proposée en installateur `.pkg` et en image `.dmg` qui contient
  ce même installateur.

## Ce que Roomcut fait

- **EQ et réglages de timbre.** Égaliseur graphique à 10 bandes, paramétrique à
  six bandes, préampli, trim de sortie, limiteur et réglages Bass, Warmth,
  Vocal, Clarity et Air.
- **Espace stéréo.** Resserrez l'image avec Focus ou élargissez-la avec Space.
  Center, Damping, crossfeed, mode enceintes/casque et surround restent à portée
  de main sans encombrer le réglage de base.
- **A/B et annulation.** Gardez deux versions d'un son, copiez l'une sur l'autre,
  comparez-les à niveau proche et annulez un glissement comme une seule édition.
- **Room Tune.** Mesurez la pièce avec un iPhone via Continuity Camera. Roomcut
  propose des corrections qui coupent les résonances évidentes et les sauvegarde
  en préréglage. Ce n'est pas un substitut à un micro de mesure calibré.
- **Now Playing.** La fenêtre de la barre des menus affiche la pochette, les
  commandes de lecture et les paroles synchronisées de [LRCLIB](https://lrclib.net).
- **Préréglages et réglages par périphérique.** Enregistrez vos propres réglages,
  échangez-les en JSON et associez un préréglage à chaque sortie.
- **Inspect.** Consultez pic, RMS, largeur stéréo, corrélation, fréquence,
  latence, activité du limiteur et interruptions, sans modifier le son.
- **Cinq langues d'interface.** Anglais, coréen, japonais, français et allemand,
  selon le système ou un choix dans Settings.

## Fonctionnement

macOS envoie le son vers le périphérique virtuel **Roomcut Output**.
`Roomcut.driver` s'exécute dans `coreaudiod` et transmet les trames via un tampon
circulaire partagé. `RoomcutAudioEngine` applique le DSP puis rend le son vers
la sortie réelle.

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

## Configuration requise

- Apple Silicon
- macOS 26 (Tahoe) ou ultérieur
- Xcode 26 et CMake pour compiler les sources

## Installation

### Depuis une version publiée

Téléchargez `Roomcut-1.0.9.pkg` ou `Roomcut-1.0.9.dmg` dans
[Releases](https://github.com/habinsong/roomcut/releases). Pour l'image disque,
ouvrez-la puis double-cliquez sur le paquet. L'installateur place l'app dans
`/Applications`, installe le pilote virtuel et le moteur, puis relance
`coreaudiod`. Le son peut s'interrompre brièvement.

Ces builds sont signés ad-hoc et non notarisés. Si macOS bloque le paquet,
faites un Control-clic, choisissez **Ouvrir**, puis autorisez-le dans
**Confidentialité et sécurité** si nécessaire. Installation possible au Terminal :

```sh
sudo installer -pkg Roomcut-1.0.9.pkg -target /
```

Ouvrez Roomcut depuis Applications. Il n'a pas d'icône dans le Dock et vit dans
la barre des menus. Choisissez ensuite **Roomcut Output** dans System Settings →
Sound, ou laissez Roomcut le faire.

### Depuis les sources

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

L'installation relance `coreaudiod`. Pour ne pas modifier la configuration audio
actuelle, construisez le projet sans lancer la dernière commande.

## Utilisation

1. Choisissez **Roomcut Output** comme sortie du Mac.
2. Choisissez la sortie réelle que Roomcut doit utiliser, puis activez le
   traitement. Le désactiver laisse l'app ouverte et active seulement le bypass.
3. Partez d'un préréglage adapté au casque ou aux enceintes, puis ajustez ce qui
   mérite de l'être à l'écoute.

Les cinq onglets ont chacun leur place :

- **Home** : Now Playing, traitement, réglages rapides, volume, préréglages et EQ complet extensible
- **Space** : largeur stéréo, centre, amortissement, crossfeed et points de départ Focus/Widen
- **Tune** : mesure iPhone et enregistrement du résultat comme préréglage
- **Inspect** : compteurs en lecture seule
- **Settings** : sortie et format, préréglage par appareil, démarrage, apparence, langue, fichiers et cache des paroles

En ouvrant complètement la feuille de son, **A/B** apparaît. A et B ont leur
propre historique ; `⌘Z` et `⇧⌘Z` s'appliquent au côté actif. L'alignement de
niveau aide à comparer honnêtement, mais il a besoin de vrai contenu en lecture
avant de pouvoir indiquer un résultat.

## Vie privée et limites

Le traitement et l'analyse restent sur le Mac. Les journaux conservent des
compteurs et des noms de périphérique, pas des échantillons audio. Room Tune
utilise le microphone de l'iPhone seulement pendant une mesure. Les paroles sont
demandées à LRCLIB avec le titre, l'artiste et la durée, puis gardées localement.
Un morceau absent de LRCLIB peut donc ne pas s'afficher.

Roomcut lit Now Playing via le framework privé MediaRemote d'Apple. Il ne peut
donc pas être proposé sur le Mac App Store, et une mise à jour de macOS peut
affecter ce panneau sans toucher au chemin audio.

## Compiler et tester

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

L'inventaire complet, les notes de développement, les vérifications et les
limites matérielles restantes sont dans [docs/development](development/README.md).

## Désinstallation

```sh
sudo bash scripts/uninstall-driver.sh
```

Le script restaure la sortie précédente lorsque c'est possible et relance
`coreaudiod`. Au besoin, choisissez à nouveau une sortie dans System Settings → Sound.

## Licence et crédits

Roomcut est proposé sous Apache License 2.0 ; voir [LICENSE](../LICENSE).
Les attributions et mentions de marques figurent dans
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Ces deux fichiers sont inclus
dans l'app et l'installateur.
