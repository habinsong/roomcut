<div align="center">

<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

**Den Raum zurücknehmen. Den Klang behalten.**

[English](../README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · [Français](README.fr.md) · **Deutsch**

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge) ![Core Audio plug-in](https://img.shields.io/badge/Core%20Audio-Plug--In-8957E5?style=for-the-badge)

![System-wide](https://img.shields.io/badge/System--wide-EQ%20%2B%20DSP-0A84FF?style=for-the-badge) ![EQ](https://img.shields.io/badge/EQ-10--band%20%2B%20Parametric-5E5CE6?style=for-the-badge) ![Spatial](https://img.shields.io/badge/Spatial-narrow%20%C2%B7%20widen-1F6FEB?style=for-the-badge) ![Room Tune](https://img.shields.io/badge/Room%20Tune-iPhone%20mic-2EA043?style=for-the-badge) ![Now Playing](https://img.shields.io/badge/Now%20Playing-Lyrics-C2185B?style=for-the-badge) ![UI languages](https://img.shields.io/badge/UI-5%20languages-FB8500?style=for-the-badge)

![Native tests](https://img.shields.io/badge/ctest-passing-2EA043?style=for-the-badge) ![Swift tests](https://img.shields.io/badge/swift%20test-passing-2EA043?style=for-the-badge) ![Driver](https://img.shields.io/badge/HAL%20driver-loads-0A84FF?style=for-the-badge)

Roomcut 1.0 bietet derzeit: systemweites Routing, EQ, Limiter, räumliche Steuerung, Analyzer, Presets, Now Playing und Room Tune.<br>Getestet auf Apple-Silicon-Macs mit macOS 26+.

</div>

> **Hinweis zum offiziellen Repository**
>
> Dies ist das offizielle Repository von **Roomcut**, erstellt und gepflegt von [habinsong](https://github.com/habinsong).
>
> Repositories, Pakete, Marketplace-Einträge, Websites, Dienste oder andere Projekte, die die ursprüngliche Dokumentation dieses Repositorys, den README-Text, Architekturbeschreibungen, Funktionsbeschreibungen, Screenshots, UI-Konzepte, Projekt-Metadaten oder andere Originalmaterialien kopieren, spiegeln, umbenennen oder eng nachahmen, stehen in keiner Verbindung zu diesem Projekt, sofern dies nicht ausdrücklich in diesem Repository angegeben ist.
>
> Der Quellcode steht unter der Apache-Lizenz 2.0 (siehe [LICENSE](../LICENSE)). Der Name Roomcut, das Branding, die Screenshots und die Dokumentation sind © 2026 송하빈 und werden von dieser Lizenz nicht abgedeckt.

Roomcut ist ein systemweiter Audioprozessor für macOS. Er fügt ein virtuelles Ausgabegerät hinzu, leitet alles, was dein Mac abspielt, durch eine Echtzeit-DSP-Kette und gibt das Ergebnis an die Lautsprecher, Kopfhörer oder den DAC aus, die du tatsächlich verwendest. Er läuft auf einem nativen CoreAudio-Audio-Server-Plug-in, sodass kein Loopback-Treiber wie BlackHole oder Soundflower nötig ist.

Die meisten „Audio-Enhancer" machen den Klang nur breiter. Roomcut bewegt ihn in beide Richtungen, und du bestimmst, wie weit. Focus nimmt übermäßige Raumakustik und Stereobreite zurück, um Stimmen und Dialoge näher an die Aufnahme zu holen; Widen öffnet die Bühne, wenn du mehr Raum möchtest. Verkleinern, verbreitern oder irgendwo dazwischen.

---
#### Kurz gesagt: Roomcut schenkt deinen Ohren Musik, die Spaß macht, <br> und dank des UI/UX in Apple Liquid Glass ist die App schon beim Ansehen schön.
---

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home.png" alt="Hauptfenster" width="200"><br><sub>Hauptfenster</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/menubar.png" alt="Menüleiste" width="270"><br><sub>Menüleiste</sub></td>
<td align="center" valign="middle"><img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/compact_mode.png" alt="Kompaktmodus" width="220"><br><sub>Kompaktmodus</sub></td>
</tr>
</table>
</div>

## Funktionen

- **Globaler EQ.** Ein grafischer 10-Band-EQ und ein parametrischer 6-Band-EQ (Bell, Shelf, Hoch-/Tiefpass, Notch) gestapelt, mit einer Live-Frequenzgangkurve.
- **Makros.** Die Regler Bass, Warmth, Vocal, Clarity und Air bewegen die passenden Bänder für dich, damit du nicht in Frequenzen denken musst.
- **Räumlich, in beide Richtungen.** Das Klangbild frei verengen oder verbreitern: Space (Breite), Center (Phantommitten-Fokus), Damping (Raumreduktion) und Crossfeed / Übersprechen. Zwischen Lautsprecher- und Kopfhörermodus wechseln, Surround umschalten, und eine Live-Ansicht des Stereofelds reagiert beim Justieren; Focus und Widen sind Ein-Tipp-Presets.
- **Limiter und Gain.** Vorverstärker, Ausgangs-Trim und ein Peak-Limiter, damit eine kräftige EQ-Kurve den Ausgang nicht übersteuert.
- **High-Res-fähig.** Verarbeitet intern in 32-Bit-Float und lässt dich Abtastrate und Bittiefe des Ausgabegeräts wählen; die Now-Playing-Karte zeigt Format und Latenz live.
- **Analyzer.** Live-Peak, RMS, Stereobreite, spektraler Schwerpunkt, eine Spektrumansicht und ein verständliches Label für das, was gerade läuft.
- **Presets.** Eine integrierte Bibliothek, gegliedert nach Signature, Apple-Geräten, Speakers und Headphones, plus deine eigenen gespeicherten Presets.
- **Room Tune.** Miss deinen Raum mit einem iPhone über das Integrationsmikrofon und erhalte zurückhaltende EQ-Korrekturen für die stärksten Resonanzen. Es senkt nur ab, hebt nie an, und speichert das Ergebnis als Preset.
- **Now Playing.** Cover-gesteuerte Themes, synchronisierte Songtexte über [LRCLIB](https://lrclib.net) und Wiedergabesteuerung im Menüleistenfenster.
- **Lokalisierte Oberfläche.** Englisch, Koreanisch, Japanisch, Französisch und Deutsch, entsprechend der Systemsprache oder manuell gewählt.
- **Sicherer Rückfall.** Stürzt die Engine ab, fällt deine Ausgabe auf ein echtes Gerät zurück, und Roh-Audio verlässt nie deinen Mac.

## Funktionsweise

macOS sendet Audio an „Roomcut Output", ein virtuelles Gerät. Der Treiber ist der dünne Teil: Er läuft in `coreaudiod`, macht kein DSP und reicht eingehende Frames lediglich über einen gemeinsamen Ringpuffer an einen Hilfsprozess weiter. Der Helfer, `RoomcutAudioEngine`, führt die DSP-Kette aus und rendert auf dein echtes Ausgabegerät.

```
System-Audio
  → Roomcut Output            virtuelles Gerät
  → Roomcut.driver            Audio Server Plug-in, in coreaudiod gesandboxt
  → gemeinsamer Ringpuffer    über einen Mach-Dienst übergeben
  → RoomcutAudioEngine        DSP + Rendering
  → Lautsprecher / AirPods / DAC / HDMI
```

Ein Audio Server Plug-in läuft in `coreaudiod` gesandboxt und kann keine beliebigen Sockets oder gemeinsamen Speicher öffnen. Roomcut bootet den Ringpuffer über eine `AudioServerPlugIn_MachServices`-Verbindung, derselbe Ansatz wie bei Background Music.

| Komponente | Rolle | Sprache |
|---|---|---|
| `Roomcut.app` | Menüleisten-App und Now-Playing-Oberfläche | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | Virtuelles Ausgabegerät (Audio Server Plug-in) | C |
| `RoomcutAudioEngine` | Hintergrund-Helfer: DSP und Rendering | C++ |
| `RoomcutCore` | Gemeinsames DSP, Presets und Analyse | C++ |
| `RoomcutNowPlaying.dylib` | MediaRemote-Brücke für Now Playing | Objective-C |

## Voraussetzungen

- macOS 26 (Tahoe) oder neuer. Die Oberfläche nutzt die Liquid-Glass-APIs des Systems.
- Apple Silicon.
- Zum Bauen: Xcode 26 (liefert das macOS-26-SDK) und CMake.

## Installation

Es gibt zwei Wege. Aus dem Quellcode zu bauen empfehle ich derzeit, da es die Gatekeeper-Abfragen vermeidet, die heruntergeladene, nicht notarisierte Binärdateien mit sich bringen.

### Aus dem Quellcode bauen

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

# nativer Treiber + Engine
cmake -S . -B build
cmake --build build

# die Menüleisten-App
bash scripts/build-app.sh

# Treiber + Engine installieren (fragt dein Passwort, startet coreaudiod neu)
sudo bash scripts/install-driver.sh
```

Öffne `build/Roomcut.app` (sie sitzt in der Menüleiste, ohne Dock-Symbol) und wähle dann „Roomcut Output" unter Systemeinstellungen ▸ Ton, oder lass die App es einstellen. Die Installation startet `coreaudiod` neu, daher setzt der Systemton etwa eine Sekunde aus.

### Vorgefertigte Version

Lade das neueste `.pkg` (Doppelklick) oder `.zip` (Terminal) von den [Releases](https://github.com/habinsong/roomcut/releases). Vorgefertigte Builds sind derzeit ad-hoc signiert und nicht notarisiert, daher fragt macOS beim ersten Start eventuell nach einer Freigabe: Rechtsklick auf das `.pkg`, „Öffnen" wählen, oder `sudo installer -pkg Roomcut-*.pkg -target /` ausführen. Das `.zip` enthält ein `install.sh`, das den Rest erledigt. Aus dem Quellcode zu bauen umgeht diesen Freigabeschritt.

## Verwendung

Mit installiertem Treiber und geöffneter App:

1. Wähle **Roomcut Output** unter Systemeinstellungen ▸ Ton (oder lass die App es einstellen). Alles, was dein Mac abspielt, läuft nun durch Roomcut.
2. Lege das echte Gerät fest, auf das Roomcut rendert (Lautsprecher, Kopfhörer oder DAC), und schalte Roomcut ein. Aus ist ein sauberer Bypass.
3. Beginne mit einem Preset, das zu deiner Hardware passt, und feile dann nach.

Das Fenster hat fünf Tabs:

- **Home.** Now Playing, der Ein/Aus-Schalter und ein Klangsteuerungs-Blatt, das man nach oben zieht: Makros Bass / Warmth / Vocal / Clarity / Air, Lautstärke und die Preset-Auswahl. Weiter ziehen für den vollständigen 10-Band- und parametrischen EQ.
- **Space.** das Stereobild verengen oder verbreitern, Mitten-Fokus, Raumdämpfung und Crossfeed einstellen, mit Focus- / Widen-Presets für Lautsprecher oder Kopfhörer.
- **Tune.** miss deinen Raum mit einem iPhone (Integrationsmikrofon) und wende zurückhaltende EQ-Korrekturen an.
- **Inspect.** schreibgeschützte Anzeigen: Peak, Limiter, Aussetzer, Korrelation, Breite, Abtastrate, Latenz und Engine-Zustand.
- **Settings.** Ausgabegerät und Format, Lautstärke, Start bei der Anmeldung, Erscheinungsbild (Thema, Layout, Sprache) und der Songtext-Cache.

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_space.png" alt="Space" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_start.png" alt="Tune" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_measuring.png" alt="Tune, Messung" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_tune_result.png" alt="Tune, Ergebnis" width="150"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_inspect.png" alt="Inspect" width="150">
</div>

Das Fenster lässt sich an jeder Kante oder Ecke skalieren, und die Tasten ↑ / ↓ öffnen und schließen das Klangsteuerungs-Blatt in Home.

Die dünne Griffleiste oben bewegt das Fenster. Ein Klick klappt das ganze Fenster in die kompakte Now-Playing-Karte; im Kompaktmodus räumt ein erneuter Klick auf die Leiste das Fenster weg (Roomcut läuft weiter in der Menüleiste und beendet sich nicht), und ein Tippen auf die Karte holt sie zurück. Der Schalter auf dieser Leiste heftet das Fenster über alle anderen Apps, sodass es nie verdeckt wird. Roomcut lebt außerdem in der Menüleiste als kleines Now-Playing-Popover mit Wiedergabesteuerung.

### Basic und Advanced

Das Klangsteuerungs-Blatt in **Home** hat zwei Modi:

- **Basic** hält es schlicht: die fünf Makro-Regler (Bass, Warmth, Vocal, Clarity, Air), einen Lautstärkeregler, der über 100 % hinaus bis 200 % geht, eine EQ-Übersichtskurve und die Preset-Auswahl.
- **Advanced** öffnet den vollen Umfang über fünf Unter-Tabs:
  - **graph.** Der kombinierte EQ-Frequenzgang als eine schreibgeschützte Kurve.
  - **10-Band.** Der klassische grafische EQ; jede Band von Hand ziehen.
  - **Parametric.** Sechs Biquad-Bänder (Bell, Shelf, Hoch-/Tiefpass, Notch) mit Frequenz, Gain und Q.
  - **Limiter.** Der Peak-Limiter plus Vorverstärker und Ausgangs-Gain.
  - **Analyzer.** Live-Spektrum mit Peak, RMS, Stereobreite und spektralem Schwerpunkt.

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_basic.png" alt="Basic-Steuerung" width="240"> <img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_home_advanced.png" alt="Advanced-Steuerung" width="240">
</div>

### Erscheinungsbild

Einstellungen ▸ Erscheinungsbild bietet drei schnelle Schalter:

- **Auto / Light / Dark.** Dem System folgen oder ein helles bzw. dunkles App-Thema erzwingen.
- **Halo / Cover / Mesh Gradient.** Der Now-Playing-Hintergrund: ein ruhiger Leuchtring um die Karte (Halo), das Albumcover, das das ganze Fenster füllt (Cover), oder ein animierter Mesh-Verlauf aus den Coverfarben (Mesh Gradient).
- **Card / Poster.** Das Now-Playing-Layout: eine einzelne zentrierte Karte (Card) oder ein randloses Cover (Poster).

<div align="center">
<img src="https://raw.githubusercontent.com/habinsong/roomcut/master/icon/app/main_settings.png" alt="Einstellungen, Erscheinungsbild" width="240">
</div>

## Deinstallation

```sh
sudo bash scripts/uninstall-driver.sh
```

Das entfernt Treiber und Engine, stellt dein vorheriges Ausgabegerät wieder her und startet `coreaudiod` neu. Bleibt der Ton danach stumm, wähle ein Gerät von Hand unter Systemeinstellungen ▸ Ton.

## Fehlerbehebung

**„Roomcut Output" erscheint nicht in den Ton-Einstellungen.** Der Treiber wird beim Neustart von `coreaudiod` geladen. Führe `sudo bash scripts/install-driver.sh` erneut aus (es startet ihn neu) oder starte neu. Ein heruntergeladener Treiber darf außerdem nicht in Quarantäne sein; das `.pkg` und `install.sh` erledigen das.

**Kein Ton nach der Installation.** Prüfe, ob das echte Ausgabegerät von Roomcut gesetzt ist und Roomcut eingeschaltet ist. Bleibt es stumm, führe `sudo bash scripts/reset-audio-output.sh` aus oder wähle ein Gerät von Hand unter Systemeinstellungen ▸ Ton.

**Die App lässt sich nicht öffnen („nicht verifizierter Entwickler").** Vorgefertigte Builds sind noch nicht notarisiert. Rechtsklick auf die App ▸ Öffnen, oder erlaube sie unter Systemeinstellungen ▸ Datenschutz & Sicherheit. Aus dem Quellcode zu bauen vermeidet das.

**Songtexte erscheinen bei einem Titel nicht.** Songtexte stammen von LRCLIB, abgeglichen über Titel, Interpret und Dauer. macOS gibt die app-eigenen Songtexte eines Streaming-Dienstes nicht heraus; ein Titel kann also in Apple Music Songtexte haben, hier aber nicht, wenn LRCLIB ihn nicht hat.

**Die Raumsteuerung ist ausgegraut.** Sie erfordert eine Engine-Version, die sie unterstützt. Installiere die neueste Engine aus dem Quellcode neu.

## Datenschutz

Roomcut verarbeitet alles, was du hörst, daher sind die Grenzen wichtig: Roh-Audio verlässt nie deine Maschine, DSP und Analyse laufen lokal, und Logs enthalten Zähler und Gerätenamen statt Samples. Room Tune nutzt das iPhone-Mikrofon nur während einer Messung.

## Bauen und Testen

```sh
swift test                          # App- / Swift-Unit-Tests
ctest --test-dir build --output-on-failure   # native Tests (DSP / Engine)
```

Beide Test-Suites laufen bei jedem Push und Pull Request in der CI auf einem macOS-26-Runner:

[![CI](https://github.com/habinsong/roomcut/actions/workflows/ci.yml/badge.svg)](https://github.com/habinsong/roomcut/actions/workflows/ci.yml)

## Mitwirken

Issues und Pull Requests sind willkommen. Die obigen Schritte geben dir einen funktionierenden Arbeitsbaum; wenn du etwas übernimmst, spart ein zuerst eröffnetes Issue meist allen Zeit.

## Songtexte und Credits

Titelinfos (Titel, Interpret, Cover) stammen aus den Now-Playing-Metadaten des Systems. Synchronisierte Songtexte werden über Titel, Interpret und Dauer von [LRCLIB](https://lrclib.net) abgeglichen, bei Bedarf abgerufen und lokal zwischengespeichert; Roomcut identifiziert sich mit einem `User-Agent` und bündelt oder verteilt in diesem Repository niemals Songtexte. Songtexte gehören ihren jeweiligen Rechteinhabern.

macOS reicht die app-eigenen Songtexte eines Streaming-Dienstes nicht an andere Apps weiter, daher holt Roomcut seine eigenen über LRCLIB. Ein Titel kann in Apple Music Songtexte haben, hier aber nicht, wenn LRCLIB diesen Titel noch nicht hat.

Server und Client von LRCLIB stehen unter der MIT-Lizenz, und Roomcut kommuniziert mit ihm nur über seine öffentliche HTTP-API; nichts von diesem Code wird hier mitgeliefert.

## Lizenz

Roomcut steht unter der Apache-Lizenz 2.0. Den vollständigen Text findest du in [LICENSE](../LICENSE). Die Lizenz deckt den Quellcode ab; der Name und das Branding von Roomcut gehören nicht dazu, da Apache 2.0 keine Markenrechte gewährt.
