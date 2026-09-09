<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · [Français](README.fr.md) · **Deutsch**

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

Roomcut 1.0.9 ist für Apple-Silicon-Macs mit macOS 26 oder neuer gedacht.

</div>

> **Offizielles Repository**
>
> Roomcut wird von [habinsong](https://github.com/habinsong) entwickelt und gepflegt. Kopien, Spiegel, Umbenennungen oder ähnliche Projekte sind nicht mit Roomcut verbunden, sofern dieses Repository es nicht ausdrücklich sagt. Der Quellcode steht unter der Apache License 2.0. Name, Gestaltung, Screenshots und Dokumentation von Roomcut sind © 2026 송하빈 und nicht Teil dieser Lizenz.

Roomcut ist ein systemweiter Audioprozessor für macOS. Er legt ein virtuelles
Ausgabegerät an, verarbeitet alles, was der Mac abspielt, und gibt es an
Lautsprecher, Kopfhörer oder DAC weiter. Der Treiber nutzt CoreAudio direkt;
ein zweiter Loopback-Treiber wie BlackHole oder Soundflower ist nicht nötig.

## Raumklang passend zum Gehörten einstellen

Focus zieht ein zu weit aufgefächertes Stereobild zusammen, Space macht es
breiter. Ein Preset ist ein Ausgangspunkt; jeder Regler lässt sich bei Bedarf
direkt einstellen.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut Startseite" width="200"><br><sub>Startseite</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut Menüleiste" width="270"><br><sub>Menüleiste</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut Kompaktmodus" width="220"><br><sub>Kompaktmodus</sub></td>
</tr>
</table>
</div>

## Neu in 1.0.9

- Der A/B-Vergleich führt für beide Seiten einen eigenen Bearbeitungsverlauf und
  kann die Wiedergabepegel vor dem Hören angleichen. Der Wechsel wird kurz
  gerampt, damit beim normalen Vergleichen kein eigener Klick entsteht.
- Ausgabewiederherstellung, Geräte-Lesen, Geräte-Schreiben und App-Polling haben
  getrennte Verantwortlichkeiten. Eine späte Antwort oder ein älterer Schreibzugriff
  soll eine neuere Auswahl nicht mehr beiläufig überschreiben.
- Room Tune räumt abgebrochene oder verspätete Messrunden klarer auf. Sein
  vorübergehender Bypass wird nur zurückgenommen, solange er ihm noch gehört.
- Die Veröffentlichung liegt als `.pkg`-Installer und als `.dmg` mit demselben
  Installer vor.

## Was Roomcut kann

- **EQ und Klangregler.** 10-Band-Grafik-EQ, 6-Band-Parametric-EQ, Preamp,
  Ausgangstrimmung, Limiter sowie Bass, Warmth, Vocal, Clarity und Air.
- **Raumsteuerung.** Mit Focus rückt das Bild zusammen, mit Space wird es weiter.
  Center, Damping, Crossfeed, Lautsprecher-/Kopfhörermodus und Surround stehen
  bereit, ohne die einfache Einstellung zu überladen.
- **A/B und Rückgängig.** Zwei Klangstände behalten, einen zum anderen kopieren,
  auf ähnlichem Pegel vergleichen und eine Geste als eine Änderung zurücknehmen.
- **Room Tune.** Ein iPhone misst den Raum über Continuity Camera. Roomcut schlägt
  nur Absenkungen für deutliche Resonanzen vor und speichert sie als Preset.
  Das ersetzt kein kalibriertes Messmikrofon.
- **Now Playing.** Das Fenster in der Menüleiste zeigt Cover, Transportsteuerung
  und synchronen Liedtext von [LRCLIB](https://lrclib.net).
- **Presets und gerätebezogene Einstellungen.** Eigene Presets speichern, als
  JSON austauschen und pro Ausgabegerät ein Preset merken.
- **Inspect.** Peak, RMS, Stereobreite, Korrelation, Abtastrate, Latenz,
  Limiter-Aktivität und Aussetzer ansehen, ohne den Klang zu ändern.
- **Fünf Oberflächensprachen.** Englisch, Koreanisch, Japanisch, Französisch und
  Deutsch folgen der Systemauswahl oder einer Wahl in Settings.

## Aufbau

macOS schickt das Audio an das virtuelle Gerät **Roomcut Output**.
`Roomcut.driver` läuft in `coreaudiod` und reicht Frames über einen gemeinsamen
Ringpuffer weiter. `RoomcutAudioEngine` wendet das DSP an und rendert zur
wirklichen Ausgabe.

```text
Systemaudio
  → Roomcut Output                 virtuelles Gerät
  → Roomcut.driver                 CoreAudio Audio Server Plug-in
  → Ringpuffer im gemeinsamen Speicher
  → RoomcutAudioEngine             DSP und Rendering
  → Lautsprecher / Kopfhörer / DAC / HDMI
```

| Komponente | Aufgabe | Sprache |
|---|---|---|
| `Roomcut.app` | Menüleisten-App und Now-Playing-Fenster | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | Virtuelles Ausgabegerät | C |
| `RoomcutAudioEngine` | DSP und Audiowiedergabe | C++ |
| `RoomcutCore` | DSP, Analyse, Presets | C++ |
| `RoomcutNowPlaying.dylib` | Now-Playing-Brücke | Objective-C |

## Voraussetzungen

- Apple Silicon
- macOS 26 (Tahoe) oder neuer
- Xcode 26 und CMake zum Bauen aus dem Quellcode

## Installation

### Aus einer Veröffentlichung

Laden Sie `Roomcut-1.0.9.pkg` oder `Roomcut-1.0.9.dmg` aus
[Releases](https://github.com/habinsong/roomcut/releases). Bei einem Disk-Image
öffnen Sie es und doppelklicken auf das Paket. Der Installer legt die App unter
`/Applications` ab, installiert virtuellen Treiber und Engine und startet dann
`coreaudiod` neu. Der Ton kann kurz aussetzen.

Diese Builds sind ad-hoc signiert und nicht notarisiert. Blockiert macOS das
Paket, klicken Sie bei gedrückter Control-Taste darauf, wählen **Öffnen** und
erlauben es bei Bedarf unter **Datenschutz & Sicherheit**. Im Terminal geht es so:

```sh
sudo installer -pkg Roomcut-1.0.9.pkg -target /
```

Starten Sie Roomcut aus Applications. Es hat kein Dock-Symbol und läuft in der
Menüleiste. Wählen Sie anschließend **Roomcut Output** in System Settings →
Sound oder lassen Sie Roomcut dies erledigen.

### Aus dem Quellcode

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

Die Installation startet `coreaudiod` neu. Wenn die aktuelle Audioeinrichtung
nicht verändert werden soll, bauen Sie ohne den letzten Installationsbefehl.

## Verwendung

1. Wählen Sie **Roomcut Output** als Ausgabe des Mac.
2. Wählen Sie das echte Gerät, an das Roomcut ausgeben soll, und schalten Sie
   die Verarbeitung ein. Beim Ausschalten bleibt die App offen; nur Bypass ist aktiv.
3. Starten Sie mit einem passenden Preset für Kopfhörer oder Lautsprecher und
   ändern Sie nur, was beim Hören Anlass dazu gibt.

Die fünf Tabs haben klar getrennte Aufgaben:

- **Home**: Now Playing, Verarbeitungsschalter, schnelle Klangregler, Lautstärke, Presets und aufklappbarer voller EQ
- **Space**: Stereobreite, Center, Dämpfung, Crossfeed und die Startpunkte Focus/Widen
- **Tune**: iPhone-Messung und Speichern des Ergebnisses als Preset
- **Inspect**: Anzeigen ohne Schreibzugriff
- **Settings**: Ausgabegerät und Format, gerätebezogenes Preset, Start, Aussehen, Sprache, Preset-Dateien und Text-Cache

Ist das Klangblatt ganz geöffnet, erscheint **A/B**. A und B führen getrennte
Verläufe; `⌘Z` und `⇧⌘Z` betreffen die aktive Seite. Pegelanpassung hilft beim
fairen Vergleich, kann aber erst mit echtem laufenden Programmmaterial ein
Ergebnis anzeigen.

## Datenschutz und Grenzen

Verarbeitung und Analyse bleiben auf dem Mac. Protokolle enthalten Zähler und
Gerätenamen, keine Audiosamples. Room Tune verwendet das iPhone-Mikrofon nur
während einer Messung. Liedtexte werden mit Titel, Künstler und Dauer bei LRCLIB
angefragt und lokal zwischengespeichert. Fehlt ein Titel dort, kann er fehlen.

Roomcut liest Now-Playing-Daten über Apples privates MediaRemote-Framework. Es
kann daher nicht im Mac App Store erscheinen; ein macOS-Update kann dieses Panel
unabhängig vom Audioweg beeinflussen.

## Bauen und testen

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Vollständiges Quellinventar, Entwicklungsnotizen, Prüfprotokolle und verbleibende
Hardware-Grenzen stehen in [docs/development](development/README.md).

## Deinstallation

```sh
sudo bash scripts/uninstall-driver.sh
```

Das Skript stellt, wenn möglich, die vorherige Ausgabe wieder her und startet
`coreaudiod` neu. Wählen Sie bei Bedarf in System Settings → Sound wieder eine Ausgabe.

## Lizenz und Hinweise

Roomcut steht unter Apache License 2.0; siehe [LICENSE](../LICENSE). Quellen-
und Markenhinweise stehen in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
Beide Dateien liegen der App und dem Installer bei.
