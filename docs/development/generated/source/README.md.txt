<div align="center">

<img src="icon/roomcut_icon.png" alt="Roomcut" width="184" height="184">

# Roomcut

**English** · [한국어](docs/README.ko.md) · [日本語](docs/README.ja.md) · [Français](docs/README.fr.md) · [Deutsch](docs/README.de.md)

[![Download](https://img.shields.io/github/v/release/habinsong/roomcut?style=for-the-badge&label=download&color=2EA043)](https://github.com/habinsong/roomcut/releases/latest) [![License](https://img.shields.io/badge/license-Apache--2.0-D22128?style=for-the-badge)](LICENSE) ![macOS 26+](https://img.shields.io/badge/macOS-26%2B-000000?style=for-the-badge) ![Apple Silicon](https://img.shields.io/badge/Apple-Silicon-555555?style=for-the-badge) ![Local-first](https://img.shields.io/badge/LOCAL--FIRST-1f2328?style=for-the-badge)

</div>

> **Official repository**
>
> Roomcut is created and maintained by [habinsong](https://github.com/habinsong). Copies, mirrors, rebrands, or look-alike projects are not affiliated unless this repository says so. The source code is available under Apache License 2.0. The Roomcut name, branding, screenshots, and documentation are © 2026 송하빈 and are not covered by that licence.

macOS has no system-wide EQ. If your speakers are 6 dB hot at 120 Hz, you can fix it inside
one app that happens to have an equalizer, and nowhere else.

Roomcut adds an output device called **Roomcut Output**. Point macOS at it and everything —
Spotify, Safari, Zoom, notification sounds — goes through a DSP chain on its way to whatever
you actually listen on. The virtual device is a CoreAudio Audio Server Plug-in that lives in
this repo, so there is no BlackHole or Soundflower underneath it.

<div align="center">
<table>
<tr>
<td align="center" valign="middle"><img src="icon/app/main_home.png" alt="Roomcut home" width="200"><br><sub>Home</sub></td>
<td align="center" valign="middle"><img src="icon/app/menubar.png" alt="Roomcut menu bar" width="270"><br><sub>Menu bar</sub></td>
<td align="center" valign="middle"><img src="icon/app/compact_mode.png" alt="Roomcut compact mode" width="220"><br><sub>Compact mode</sub></td>
</tr>
</table>
</div>

## What's in it

**EQ.** Ten graphic bands from 31 Hz to 16 kHz, and six parametric bands on top of them —
bell, low/high shelf, high/low pass, notch. Five macro knobs (Bass, Warmth, Vocal, Clarity,
Air) move groups of bands for you when you don't want to think in frequencies. A limiter sits
at the end with 2 ms of lookahead, which is the only latency Roomcut adds on purpose.

**Stereo space.** Recent masters are mixed wide, and on laptop speakers the vocal can slide
out of the middle. Focus pulls the sides back until it sits there again; Space goes the other
way. Both act on the side signal only, so a dead-centre vocal comes out untouched at any
setting. That was the constraint, and it cost a rewrite: the first version widened by adding
a phase-rotated copy of the mid, which is mono-safe but not image-safe, and the vocal drifted
left as you pushed the slider. Center, Damping, Crossfeed and a speaker/headphone switch are
in the same tab.

**A/B with matched level.** Two slots, each with its own undo history. `⌘Z` and `⇧⌘Z` apply
to the side you're on, and the copy button hands the current settings to the other side.
Turn on Level and both chains — limiters included — are measured on the same passage with a
K-weighted meter; the louder one is pulled down so you're judging the sound instead of the
volume. Switching ramps over 15 ms.

**Room Tune.** An iPhone works as the measurement mic over Continuity Camera. Roomcut plays
sweeps, looks for obvious resonances, and proposes cuts only — never boosts — which it saves
as a preset. Read the section below on what this is not.

**Presets.** 25 built in, grouped as Signature, Apple, Speakers and Headphones. Save your
own, export and import them as JSON, and pin one per output device so plugging in headphones
brings back the right curve.

**Now Playing and Inspect.** The menu-bar window shows artwork, transport controls and timed
lyrics from [LRCLIB](https://lrclib.net). Inspect is read-only: peak, RMS, stereo width,
correlation, sample rate, device latency, limiter activity, dropouts.

Interface languages: English, Korean, Japanese, French, German. It follows the system
language unless you pick one in Settings.

## Install

Grab `Roomcut-1.0.9.pkg` from [Releases](https://github.com/habinsong/roomcut/releases), or
`Roomcut-1.0.9.dmg`, which is the same package inside a disk image.

These builds are ad-hoc signed. There is no Developer ID certificate behind them and they are
not notarized, so macOS will stop the first launch. Open it once anyway, then go to
**System Settings → Privacy & Security → Open Anyway**. The button shows up only after the
blocked attempt, which is the part that trips people up.

Or skip Gatekeeper altogether, since `installer` doesn't consult it:

```sh
sudo installer -pkg Roomcut-1.0.9.pkg -target /
```

If macOS says the package is *damaged* instead of unverified, that's a different thing — the
download is corrupt, or the signature is broken. Check what you got:

```sh
shasum -a 256 Roomcut-1.0.9.pkg
# 3636c8022857088b020798a3ac80b7bd63aaaf15ec069ab9579adb8fb56a8139
shasum -a 256 Roomcut-1.0.9.dmg
# 3382f6c434660624a0d02bb81576dac4fecaf20f55b90b3b90380f0423e760c2
```

The installer puts the app in `/Applications`, the driver in the system HAL folder, and a
background engine under `/Library/Application Support/Roomcut`. It then restarts
`coreaudiod`, so all audio on the Mac stops for about a second. Roomcut has no Dock icon; it
opens in the menu bar. Pick **Roomcut Output** in System Settings → Sound, or let the app do
it, then choose the real device you want it to render to and switch processing on.

### From source

```sh
git clone https://github.com/habinsong/roomcut.git
cd roomcut

cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
bash scripts/build-app.sh release
sudo bash scripts/install-driver.sh
```

You need Xcode 26 and CMake. Leave the last line off if you'd rather not touch your current
audio setup — everything up to it only writes into `build/`.

## What it won't do

- **Intel Macs and older systems.** Apple Silicon and macOS 26 (Tahoe) or later, full stop.
- **Multichannel.** The virtual device is stereo. Surround content gets downmixed by macOS
  before Roomcut ever sees it.
- **Per-app processing.** It's the system output or nothing.
- **Room correction in the serious sense.** Room Tune uses a phone microphone at one position
  in the room. The mic isn't flat, one position isn't the room, and the result is a starting
  point you should trust with your ears, not a calibrated measurement.
- **Live the App Store life.** Now Playing reads Apple's private MediaRemote framework, which
  keeps Roomcut off the Mac App Store and means a macOS update can break that panel while the
  audio path keeps working.
- **Survive without the engine.** The DSP runs in a background daemon. Quitting the app
  leaves audio flowing through it; uninstalling is what removes it.

## How it fits together

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

The engine, not the app, owns the audio. It runs as a LaunchDaemon, keeps its own state file,
and watches the driver's write index and heartbeat so it can tell "nothing is playing" from
"the driver went away". There's also a watchdog for a failure I hit on 2026-08-01: an iFi DAC
opened at 48 kHz, reopened at 384 kHz moments later, and the render callback then ran at 3.37×
real time with the ring draining and underruns climbing. Device and rate both looked
unchanged, so nothing else caught it. The engine now measures how fast frames are actually
leaving and rebuilds the output unit when the answer is wrong.

## Privacy

Audio never leaves the Mac. Logs hold counters and device names, no samples. Room Tune opens
the iPhone mic only while a measurement runs. Lyrics are the one network call: title, artist
and duration go to LRCLIB, and the answer is cached in
`~/Library/Caches/com.habinsong.roomcut/lyrics.json`.

## Build and test

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test -c release
cmake -S . -B build -DROOMCUT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

40 native tests and 218 Swift tests at this commit. What they don't cover is a real room, a
real microphone, or a second Mac — [docs/development](docs/development/README.md) has the
per-area verification records, including which limits are still open.

## Uninstall

```sh
sudo bash scripts/uninstall-driver.sh
```

Stopping the engine puts the system default back on the device it was rendering to; then the
driver is removed and `coreaudiod` restarts. If the Mac ends up silent, pick an output in
System Settings → Sound.

## Licence

Apache License 2.0 — see [LICENSE](LICENSE). Attributions and trademark notices are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); both ship inside the app and the installer.
