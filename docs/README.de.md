<div align="center">

<img src="../icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

[English](../README.md) · [한국어](README.ko.md) · [日本語](README.ja.md) · [Français](README.fr.md) · **Deutsch**

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](../LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge)

</div>

> **Offizielles Repository**
>
> Roomcut wird von [habinsong](https://github.com/habinsong) entwickelt und gepflegt. Der Quellcode steht unter der Apache License 2.0. Name, Design, Screenshots und Dokumentation von Roomcut sind © 2026 송하빈 (habinsong).

macOS bietet nativ keinen systemweiten Equalizer. Roomcut installiert einen systemweiten virtuellen Audiotreiber (**Roomcut Output**) und leitet alle Systemtöne — von Apps wie Spotify, Safari oder Zoom bis hin zu Systemklängen — durch eine präzise DSP-Kette an das tatsächliche Ausgabegerät weiter.

iOS-inspirierte, moderne Ästhetik trifft auf professionelle Audioverarbeitung: EQ, Stereobreite, Raumakustik-Korrektur und Preset-Verwaltung direkt über ein elegantes Menüleisten-Interface.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="../icon/app/main_home.png" alt="Roomcut Startseite" width="200"><br><sub>Startseite</sub></td>
<td align="center" valign="middle"><img src="../icon/app/menubar.png" alt="Roomcut Menüleiste" width="270"><br><sub>Menüleiste</sub></td>
<td align="center" valign="middle"><img src="../icon/app/compact_mode.png" alt="Roomcut Kompaktmodus" width="220"><br><sub>Kompaktmodus</sub></td>
</tr>
</table>
</div>

## Kernfunktionen

- **Hybrid-EQ**: 10-Band-Grafik-EQ (31 Hz – 16 kHz) und 6-Band-Parametrik-EQ (Bell, Shelving, Hoch-/Tiefpass, Notch) mit 5 Makro-Reglern (Bass, Warmth, Vocal, Clarity, Air).
- **Dynamischer EQ**: Bänder lassen sich dynamisch schalten, um unerwünschte Resonanzen pegelabhängig präzise abzusenken.
- **Stereobreite & Raumklang**: Präzise Kontrolle über Focus (Mitte) und Space (Seite), Crossfeed, Lautsprecher-/Kopfhörer-Modi sowie ein optionaler virtueller Raum (Studio / Living / Hall), der den Klang auf Kopfhörern aus dem Kopf holt.
- **A/B-Vergleich mit Lautstärkeangleichung**: K-gewichteter Pegelabgleich für faire Vergleiche ohne Täuschung durch Lautstärkeunterschiede (`⌘Z` / `⇧⌘Z` Undo/Redo).
- **Room Tune**: Einfache akustische Einmessung und Resonanzkorrektur über ein iPhone als Messmikrofon (Continuity Camera).
- **Gerätespezifische Presets**: 25 Werkspresets und automatische Preset-Umschaltung je nach verbundenem Ausgabegerät.
- **Now Playing & Audio-Inspektor**: Cover-Art, synchrone Songtexte via LRCLIB sowie Echtzeit-Messung von Peak, RMS, Stereobreite, Latenz und Limiter-Aktivität.

## Systemanforderungen

- **Betriebssystem**: macOS 26 (Tahoe) oder neuer
- **Architektur**: Apple Silicon (M-Serie)
- Keine zusätzlichen Drittanbieter-Treiber (wie BlackHole oder Soundflower) erforderlich.

## Installation

Laden Sie `Roomcut-1.1.1.pkg` oder `Roomcut-1.1.1.dmg` aus den [Releases](https://github.com/habinsong/roomcut/releases/latest) herunter.

```sh
# Manuelle Installation über das Terminal (umgeht Gatekeeper)
sudo installer -pkg Roomcut-1.1.1.pkg -target /
```

Nach der Installation erscheint Roomcut in der Menüleiste. Wählen Sie **Roomcut Output** als Standard-Ausgabegerät in den macOS-Systemeinstellungen aus (oder lassen Sie Roomcut dies automatisch einrichten).

### Aus dem Quellcode erstellen

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

## Deinstallation

```sh
sudo bash scripts/uninstall-driver.sh
```

## Datenschutz

Roomcut verarbeitet alle Audiodaten ausschließlich lokal auf dem Gerät. Es werden keinerlei Audiodaten oder Nutzungsstatistiken übertragen. Lediglich die synchrone Songtext-Suche fragt Metadaten (Titel, Interpret) über die offene API von LRCLIB ab und speichert sie lokal im Cache.

## Lizenz

Apache License 2.0 — siehe [LICENSE](../LICENSE). Drittanbieter-Lizenzen sind in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) dokumentiert.
