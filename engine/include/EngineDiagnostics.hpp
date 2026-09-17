#ifndef ROOMCUT_ENGINE_DIAGNOSTICS_HPP
#define ROOMCUT_ENGINE_DIAGNOSTICS_HPP

#include "dsp/ChainParams.hpp"
#include <cstdint>
#include <string>

namespace roomcut {

// Dev-only diagnostic surface: the command line that turns capture on and the
// float32 WAV it writes. Kept out of the control loop, which only asks for the
// parsed options and hands back the captured block on exit.
struct EngineOptions {
    std::string dumpPath;                       // empty = no capture
    ChainParams params = ChainParams::flat();
    bool eqGiven = false;                       // --eq wins over the resumed state
    // The headphone upmix goes through AUSpatialMixer (SpatialMixerBedRenderer)
    // wherever it opens — the default since P0 G1; each parameter set can still
    // pick the built-in bed (ChainParams::bedRenderer), which is how A/B
    // compares the two. --bed-renderer builtin does not attach it at all.
    bool systemBedRenderer = true;

    bool dumping() const { return !dumpPath.empty(); }
};

// Capture cap for --dump (covers the 30 s soak with margin).
constexpr uint32_t kDumpMaxSeconds = 35;

// Parses argv into `out`. On a bad argument, fills `error` with the message the
// engine prints and returns false; `out` is then not meaningful.
bool parseEngineOptions(int argc, char* const argv[], EngineOptions& out, std::string& error);

// Minimal RIFF/WAVE writer for --dump: 32-bit float PCM (wFormatTag=3),
// interleaved. Little-endian host assumed (macOS).
bool writeWavF32(const char* path, const float* samples, uint32_t frames,
                 uint32_t channels, uint32_t sampleRate);

} // namespace roomcut
#endif
