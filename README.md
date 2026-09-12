<div align="center">

<img src="icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

**English** · [한국어](docs/README.ko.md) · [日本語](docs/README.ja.md) · [Français](docs/README.fr.md) · [Deutsch](docs/README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

</div>

> **Official repository**
>
> Roomcut is created and maintained by [habinsong](https://github.com/habinsong). Copies, mirrors, rebrands, or look-alike projects are not affiliated unless this repository says so. The source code is available under Apache License 2.0. The Roomcut name, branding, screenshots, and documentation are © 2026 송하빈 and are not covered by that licence.

Roomcut is a native macOS application that controls and enhances all Mac audio system-wide through its own virtual audio driver (CoreAudio HAL). Without third-party routing tools like BlackHole, it applies precision real-time DSP to all system sound with an elegant, iOS-inspired native interface.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="icon/app/main_home.png" alt="Roomcut Home" width="200"><br><sub>Home</sub></td>
<td align="center" valign="middle"><img src="icon/app/menubar.png" alt="Roomcut Menu Bar" width="270"><br><sub>Menu Bar</sub></td>
<td align="center" valign="middle"><img src="icon/app/compact_mode.png" alt="Roomcut Compact Mode" width="220"><br><sub>Compact Mode</sub></td>
</tr>
</table>
</div>

## Key Features

- **System-wide EQ**: 10-band graphic EQ + 6-band parametric EQ (with dynamic EQ support) and 5 intuitive macro dials (Bass, Warmth, Vocal, Clarity, Air).
- **Stereo Space**: Adjust width and focus on the side channel while preserving center vocals (Focus/Space controls) plus headphone Crossfeed.
- **Level-Matched A/B**: Instant comparison between two independent slots with K-weighted loudness matching to eliminate volume bias.
- **Room Tune**: Measure room resonances via Continuity Camera using your iPhone microphone and generate cut-only correction presets.
- **25 Built-in Presets**: Tailored curves for built-in speakers, headphones, and AirPods, plus custom JSON preset import/export.
- **Now Playing & Inspect**: Menu bar playback controls, synchronized lyrics via [LRCLIB](https://lrclib.net), and real-time audio telemetry (Peak, RMS, correlation, latency).
- **Multilingual**: English, 한국어, 日本語, Français, Deutsch.

## Installation

Download the latest `Roomcut-1.1.0.pkg` or `Roomcut-1.1.0.dmg` from [Releases](https://github.com/habinsong/roomcut/releases).

> **Ad-hoc Signing Notice**<br>
> If macOS blocks the first run, go to **System Settings → Privacy & Security → Open Anyway**.

Install via Terminal:
```sh
sudo installer -pkg Roomcut-1.1.0.pkg -target /
```

### Build from Source
```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

## System Requirements & Notes

- **Platform**: Apple Silicon Mac running macOS 26 (Tahoe) or later.
- **Format**: 2-channel stereo only (multichannel audio is automatically downmixed by macOS).
- **System-wide Scope**: Applies globally to all system output; per-app processing is not supported.
- **Background Daemon**: The DSP engine runs as a system LaunchDaemon (`RoomcutAudioEngine`), ensuring audio processing continues even when the window is closed.
- **Privacy First**: Audio never leaves your Mac. Network requests are strictly limited to on-demand lyrics lookup (LRCLIB).

## Uninstall

```sh
sudo bash scripts/uninstall-driver.sh
```
Restores default audio output to your original device, removes the virtual driver, and restarts CoreAudio.

## License

Apache License 2.0. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
