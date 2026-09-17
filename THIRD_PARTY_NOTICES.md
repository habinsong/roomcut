# Third-Party Notices

Roomcut itself is licensed under the Apache License 2.0 (see [LICENSE](LICENSE)).
No third-party source code is vendored into this repository. The notices below
cover work that Roomcut references or interoperates with.

## mediaremote-adapter

Roomcut's Now Playing helper (`apps/macos/NowPlayingHelper/`) reads system Now
Playing metadata through Apple's private MediaRemote framework, loaded from a
child `/usr/bin/perl` process. That technique was published by
[ungive/mediaremote-adapter](https://github.com/ungive/mediaremote-adapter).

Roomcut's helper is an independent implementation of the technique — it shares no
meaningful code with the adapter — and is therefore covered by Roomcut's own
Apache-2.0 grant. The adapter's notice is reproduced here as attribution for the
approach:

```
BSD 3-Clause License

Copyright (c) 2025, Jonas van den Berg and contributors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

Neither Jonas van den Berg nor the mediaremote-adapter contributors endorse
Roomcut.

## LRCLIB

Synced lyrics are fetched on demand from [LRCLIB](https://lrclib.net) through its
public HTTP API. LRCLIB's own server and client are MIT-licensed; none of that
code ships with Roomcut, and Roomcut neither bundles nor redistributes lyrics.
Lyrics belong to their respective owners.

## Algorithms and references

- **Biquad filter coefficients** — Robert Bristow-Johnson, "Cookbook formulae for
  audio equalizer biquad filter coefficients" (the "Audio EQ Cookbook"). The
  coefficient formulas are published for free use; the implementation in
  `core/dsp/Biquad.hpp` is Roomcut's own.
- **RACE (Recursive Ambiophonic Crosstalk Elimination)** — Ralph Glasgal and
  Robin Miller released the technique free to the public. The crosstalk
  cancellation in `core/dsp/Spatial.hpp` and `core/dsp/SideCanceller.hpp` is
  Roomcut's own implementation of that published approach. Roomcut is not
  affiliated with, and not endorsed by, the Ambiophonics Institute.
- **Virtual room** — early reflections follow the image-source method (Allen and
  Berkley, 1979) and the late reverberation is a feedback delay network
  (Stautner and Puckette, 1982; Jot and Chaigne, 1991). `core/dsp/RoomSim.hpp` is Roomcut's own implementation of these
  published methods.
- **Level matching** — the K-weighting filter follows ITU-R BS.1770. The
  implementation in `core/dsp/KWeightedLevel.hpp` is Roomcut's own.

## Apple

Roomcut builds against Apple's public Core Audio, AudioUnit, AudioToolbox,
AVFoundation, CoreMotion, AppKit, and SwiftUI APIs and uses SF Symbols under
Apple's terms. The headphone 5.1/7.1 bed is rendered at runtime by macOS's own
`AUSpatialMixer` audio unit; Roomcut neither contains nor redistributes its
HRTF data. When the output is AirPods, macOS applies the listener's personalized
spatial audio profile inside that unit; Roomcut does not read, copy, or store
the profile. Head tracking reads headphone motion through Core Motion and keeps
it on the Mac. Roomcut's Now Playing helper
additionally resolves the private MediaRemote framework at runtime; because that
API is private, this feature may break on any macOS update and rules out Mac App
Store distribution. Roomcut is not affiliated with, authorized by, or endorsed by
Apple Inc.

## Trademarks

Device and product names used in preset names, folder names, and documentation —
including AirPods, AirPods Pro, AirPods Max, Beats, MacBook Pro, MacBook Air,
Apple Music, and any third-party audio hardware named in screenshots — are
trademarks of their respective owners. Roomcut uses them only to identify the
hardware or software a preset or feature is intended for. Their use implies no
affiliation with, sponsorship by, or endorsement from those owners.
