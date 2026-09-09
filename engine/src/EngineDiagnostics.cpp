#include "EngineDiagnostics.hpp"

#include "dsp/GraphicEQ.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace roomcut {

bool parseEngineOptions(int argc, char* const argv[], EngineOptions& out, std::string& error) {
    const char* program = argc > 0 && argv[0] != nullptr ? argv[0] : "RoomcutAudioEngine";
    char usage[256];
    std::snprintf(usage, sizeof(usage), "usage: %s [--dump out.wav] [--eq g0,g1,...,g9]", program);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            out.dumpPath = argv[++i];
        } else if (std::strcmp(argv[i], "--eq") == 0 && i + 1 < argc) {
            const char* s = argv[++i];
            char* end = nullptr;
            bool ok = true;
            for (std::size_t b = 0; b < GraphicEQ::kNumBands && ok; ++b) {
                out.params.eqGainsDb[b] = std::strtod(s, &end);
                ok = end != s
                  && (b + 1 == GraphicEQ::kNumBands ? *end == '\0' : *end == ',');
                s = end + 1;
            }
            if (!ok) {
                char message[128];
                std::snprintf(message, sizeof(message),
                              "bad --eq: need %zu comma-separated dB values", GraphicEQ::kNumBands);
                error = message;
                return false;
            }
            out.eqGiven = true;
        } else {
            error = usage;
            return false;
        }
    }
    return true;
}

bool writeWavF32(const char* path, const float* samples, uint32_t frames,
                 uint32_t channels, uint32_t sampleRate) {
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const uint32_t dataBytes  = frames * channels * (uint32_t)sizeof(float);
    const uint32_t riffBytes  = 36u + dataBytes;
    const uint32_t byteRate   = sampleRate * channels * (uint32_t)sizeof(float);
    const uint16_t blockAlign = (uint16_t)(channels * sizeof(float));
    const uint32_t fmtSize    = 16u;
    const uint16_t fmtFloat   = 3u;
    const uint16_t bits       = 32u;
    const uint16_t ch16       = (uint16_t)channels;
    bool ok = std::fwrite("RIFF", 1, 4, f) == 4
           && std::fwrite(&riffBytes, 4, 1, f) == 1
           && std::fwrite("WAVE", 1, 4, f) == 4
           && std::fwrite("fmt ", 1, 4, f) == 4
           && std::fwrite(&fmtSize, 4, 1, f) == 1
           && std::fwrite(&fmtFloat, 2, 1, f) == 1
           && std::fwrite(&ch16, 2, 1, f) == 1
           && std::fwrite(&sampleRate, 4, 1, f) == 1
           && std::fwrite(&byteRate, 4, 1, f) == 1
           && std::fwrite(&blockAlign, 2, 1, f) == 1
           && std::fwrite(&bits, 2, 1, f) == 1
           && std::fwrite("data", 1, 4, f) == 4
           && std::fwrite(&dataBytes, 4, 1, f) == 1;
    if (ok && dataBytes > 0) {
        ok = std::fwrite(samples, 1, dataBytes, f) == dataBytes;
    }
    return std::fclose(f) == 0 && ok;
}

} // namespace roomcut
