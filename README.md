<div align="center">

<img src="icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

**English** · [한국어](docs/README.ko.md) · [日本語](docs/README.ja.md) · [Français](docs/README.fr.md) · [Deutsch](docs/README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

Roomcut 1.0.9 is for Apple-silicon Macs running macOS 26 or later.

</div>

> **Official repository**
>
> Roomcut is created and maintained by [habinsong](https://github.com/habinsong). Copies, mirrors, rebrands, or look-alike projects are not affiliated unless this repository says so. The source code is available under Apache License 2.0. The Roomcut name, branding, screenshots, and documentation are © 2026 송하빈 and are not covered by that licence.

Roomcut is a system-wide audio processor for macOS. It creates a virtual output
device, processes what the Mac is playing, and sends it to your speakers,
headphones, or DAC. The driver is built on CoreAudio; you do not need a second
loopback driver such as BlackHole or Soundflower.

## Stereo-space controls for what you are hearing

Focus brings an overly diffuse stereo image inward; Space widens it. Start with
a preset or adjust the controls directly.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="icon/app/main_home.png" alt="Roomcut home" width="200"><br><sub>Home</sub></td>
<td align="center" valign="middle"><img src="icon/app/menubar.png" alt="Roomcut menu bar" width="270"><br><sub>Menu bar</sub></td>
<td align="center" valign="middle"><img src="icon/app/compact_mode.png" alt="Roomcut compact mode" width="220"><br><sub>Compact mode</sub></td>
</tr>
</table>
</div>

## What's new in 1.0.9

- A/B comparison now keeps an independent edit history for each side and can
  match their playback level before you judge the change. Switching is ramped
  so ordinary comparisons do not add a click of their own.
- Output recovery, device reads, device writes, and app polling now have their
  own ownership boundaries. That keeps a late answer or an older write from
  casually replacing a newer choice.
- Room Tune has a clearer cleanup path for cancelled or delayed rounds. Its
  temporary bypass is returned only when it still owns that change.
- The release is available as both a `.pkg` installer and a `.dmg` containing
  that installer.

## What it does

- **EQ and tone controls.** Use a 10-band graphic EQ, six-band parametric EQ,
  preamp, output trim, limiter, and five quick controls for Bass, Warmth,
  Vocal, Clarity, and Air.
- **Space controls.** Bring the image inward with Focus or widen it with Space.
  Center focus, damping, crossfeed, speaker/headphone mode, and surround are
  there when you need them.
- **A/B and undo.** Keep two versions of a sound, copy either one to the other,
  compare them at a matched level, and undo a gesture as one edit.
- **Room Tune.** Use an iPhone through Continuity Camera to measure a room.
  Roomcut suggests cut-only EQ for obvious resonances and saves the result as a
  preset. It is a practical starting point, not a replacement for a calibrated
  measurement microphone.
- **Now Playing.** The menu-bar window can show artwork, transport controls,
  and timed lyrics from [LRCLIB](https://lrclib.net).
- **Presets and per-device settings.** Start with built-in presets, save your
  own, exchange them as JSON, and remember a preset for each output device.
- **Inspect.** Check live peak, RMS, stereo width, correlation, sample rate,
  latency, limiter activity, and dropouts without changing the sound.
- **Five interface languages.** English, Korean, Japanese, French, and German
  follow the system language unless you choose one in Settings.

## How it is put together

macOS sends audio to **Roomcut Output**, the virtual device. `Roomcut.driver`
runs inside `coreaudiod` and passes frames through a shared ring buffer. The
`RoomcutAudioEngine` helper applies the DSP and renders to the actual output.

```text
System audio
  → Roomcut Output                 virtual device
  → Roomcut.driver                 CoreAudio Audio Server Plug-in
  → shared-memory ring buffer
  → RoomcutAudioEngine             DSP and rendering
  → speakers / headphones / DAC / HDMI
```

| Component | Job | Language |
|---|---|---|
| `Roomcut.app` | Menu-bar app and Now Playing window | Swift (SwiftUI + AppKit) |
| `Roomcut.driver` | Virtual output device | C |
| `RoomcutAudioEngine` | DSP and audio rendering | C++ |
| `RoomcutCore` | DSP, analysis, presets | C++ |
| `RoomcutNowPlaying.dylib` | Now Playing bridge | Objective-C |

## Requirements

- Apple Silicon
- macOS 26 (Tahoe) or later
- Xcode 26 and CMake when building from source

## Install

### From a release

Download either `Roomcut-1.0.9.pkg` or `Roomcut-1.0.9.dmg` from
[Releases](https://github.com/habinsong/roomcut/releases). If you downloaded the
disk image, open it and double-click the package inside. The installer puts the
app in `/Applications`, installs the virtual driver and engine, then restarts
`coreaudiod`; sound may pause for a moment.

These builds are ad-hoc signed and not notarized. If macOS blocks the package,
Control-click it, choose **Open**, then approve it in **Privacy & Security** if
asked. You can also install it from Terminal:

```sh
sudo installer -pkg Roomcut-1.0.9.pkg -target /
```

Open Roomcut from Applications. It has no Dock icon; it lives in the menu bar.
Then select **Roomcut Output** in System Settings → Sound, or let Roomcut set
it for you.

### From source

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

Installing restarts `coreaudiod`. If you would rather not change your current
audio setup, build without the final install command.

## Use it

1. Choose **Roomcut Output** as the Mac's output device.
2. Choose the real device Roomcut should render to, then turn processing on.
   Turning it off is a bypass; it does not quit the app.
3. Begin with a preset for your headphones or speakers. Adjust only what you
   hear a reason to change.

The five tabs are deliberately small in scope:

- **Home** holds Now Playing, the processing switch, quick tone controls,
  volume, presets, and the expandable full EQ.
- **Space** holds stereo width, center focus, damping, crossfeed, and the
  Focus/Widen starting points.
- **Tune** runs the iPhone measurement and saves its result as a preset.
- **Inspect** is read-only: it reports what the engine is doing.
- **Settings** holds the output device and format, per-device preset choice,
  startup, appearance, language, preset files, and lyric cache.

When the sound sheet is fully open, **A/B** stores two sound states. Each side
has its own undo history; `⌘Z` and `⇧⌘Z` affect the active side. Level matching
is useful for a fair comparison, but it needs real program material before it
can report a match.

## Privacy and limits

Audio processing and analysis stay on the Mac. Logs contain counters and device
names, not audio samples. Room Tune uses the iPhone microphone only while a
measurement is running. Lyrics are requested from LRCLIB by title, artist, and
duration, then cached locally; a track can be missing if LRCLIB does not have it.

Roomcut reads Now Playing metadata through Apple's private MediaRemote framework.
That keeps it out of the Mac App Store, and a macOS update may affect the
Now Playing panel independently of the audio path.

## Build and test

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The detailed source inventory, development notes, verification records, and
remaining hardware limits are in [docs/development](docs/development/README.md).

## Uninstall

```sh
sudo bash scripts/uninstall-driver.sh
```

This restores the previous output device where possible and restarts
`coreaudiod`. If needed, choose an output device again in System Settings →
Sound.

## Licence and credits

Roomcut is licensed under the Apache License 2.0; see [LICENSE](LICENSE).
Attributions and trademark notices are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Both files are included in the
app and installer.
