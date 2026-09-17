#include "EngineState.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>

namespace roomcut {

// Bumped when a field in the params line changes MEANING rather than being
// appended. 1 = centre width / surround depth are 0..100 steering.
constexpr double kParamsSchema = 1.0;

void capturePersistentSound(PersistentState& state, const ChainParams& params, const char* presetID) {
    state.presetId = presetID;
    state.paramsLine = state.presetId == "custom" ? serializeParamsLine(params) : std::string();
    state.parametricLine = serializeParametricLine(params);
    state.dynamicsLine = serializeDynamicsLine(params);
}

double clampVolumeBoost(double boost) {
    if (!std::isfinite(boost)) return 1.0;
    return std::min(2.0, std::max(1.0, boost));
}

std::string serializeParamsLine(const ChainParams& params) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << params.preampDb;
    for (double gain : params.eqGainsDb) output << ' ' << gain;
    output << ' ' << params.limiterReleaseMs << ' ' << params.outputGainDb
        << ' ' << params.spatialWidth << ' ' << params.centerFocus
        << ' ' << params.crossfeed << ' ' << params.roomReduce << ' ' << params.spatialMode
        << ' ' << params.highpassHz << ' ' << params.compAmount
        << ' ' << params.roomType << ' ' << params.roomAmount
        << ' ' << params.surroundType << ' ' << params.centerWidth
        << ' ' << params.surroundDepth
        // Schema marker. The two fields before it changed meaning once already
        // — they were -12..+6 dB trims and became 0..100 steering — and a file
        // written before that change stores a 0 that used to mean "no trim" and
        // now reads as "extract no centre at all". A file without this marker is
        // from before, so those two are ignored and the defaults stand.
        << ' ' << kParamsSchema
        << ' ' << params.bedRenderer;
    return output.str();
}

bool parseParamsLine(const std::string& line, ChainParams* params) {
    if (params == nullptr) return false;
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    constexpr size_t base = GraphicEQ::kNumBands + 3;
    constexpr size_t fields = base + 14;  // spatial (5) + dynamics (2) + room (2) + upmix (3) + schema + bed renderer
    double values[fields]{};
    size_t read = 0;
    for (size_t i = 0; i < fields; ++i) {
        input >> std::ws;
        if (i >= base && input.eof()) break; // pre-spatial/dynamics/room state files
        if (!(input >> values[i]) || !std::isfinite(values[i])) return false;
        read = i + 1;
    }
    params->preampDb = values[0];
    for (size_t band = 0; band < GraphicEQ::kNumBands; ++band) params->eqGainsDb[band] = values[band + 1];
    params->limiterReleaseMs = values[base - 2];
    params->outputGainDb = values[base - 1];
    params->spatialWidth = values[base];
    params->centerFocus = values[base + 1];
    params->crossfeed = values[base + 2];
    params->roomReduce = values[base + 3];
    params->spatialMode = values[base + 4];
    params->highpassHz = values[base + 5];
    params->compAmount = values[base + 6];
    // A state file written before the virtual room existed keeps the struct's
    // own defaults (room off at the reference level) instead of reading zeros.
    if (read >= base + 9) {
        params->roomType = values[base + 7];
        params->roomAmount = values[base + 8];
    }
    // Likewise for a file written before the upmix: keep the struct's defaults
    // (upmix off, trims at unity) instead of reading zeros that are not there.
    if (read >= base + 12) {
        params->surroundType = values[base + 9];
        // Only trust the steering values from a file that was written with the
        // current meaning; see kParamsSchema.
        if (read >= base + 13 && values[base + 12] >= 1.0) {
            params->centerWidth = values[base + 10];
            params->surroundDepth = values[base + 11];
        }
    }
    // A file from before the bed renderer keeps the default (system renderer).
    if (read >= base + 14) params->bedRenderer = values[base + 13];
    return true;
}

std::string serializeParametricLine(const ChainParams& params) {
    bool edited = false;
    for (const auto& band : params.parametric) edited |= !(band == ParametricBand{});
    if (!edited) return {};
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& band : params.parametric) {
        output << (band.enabled ? 1 : 0) << ' ' << band.type << ' ' << band.freqHz
               << ' ' << band.gainDb << ' ' << band.q << ' ';
    }
    return output.str();
}

std::string serializeDynamicsLine(const ChainParams& params) {
    bool used = false;
    for (const auto& band : params.parametric) used |= band.dynamic;
    if (!used) return {};
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& band : params.parametric) {
        output << (band.dynamic ? 1 : 0) << ' ' << band.thresholdDb << ' ' << band.rangeDb
               << ' ' << band.attackMs << ' ' << band.releaseMs << ' ';
    }
    return output.str();
}

bool parseDynamicsLine(const std::string& line, ChainParams* params) {
    if (params == nullptr) return false;
    if (line.empty()) return true;
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    auto bands = params->parametric;
    for (auto& band : bands) {
        int dynamic;
        if (!(input >> dynamic >> band.thresholdDb >> band.rangeDb >> band.attackMs >> band.releaseMs)
            || !std::isfinite(band.thresholdDb) || !std::isfinite(band.rangeDb)
            || !std::isfinite(band.attackMs) || !std::isfinite(band.releaseMs)) return false;
        band.dynamic = dynamic != 0;
    }
    params->parametric = bands;
    return true;
}

bool parseParametricLine(const std::string& line, ChainParams* params) {
    if (params == nullptr) return false;
    if (line.empty()) return true;
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    auto bands = params->parametric;
    for (auto& band : bands) {
        int enabled;
        if (!(input >> enabled >> band.type >> band.freqHz >> band.gainDb >> band.q)
            || !std::isfinite(band.freqHz) || !std::isfinite(band.gainDb) || !std::isfinite(band.q)) return false;
        band.enabled = enabled != 0;
    }
    params->parametric = bands;
    return true;
}

} // namespace roomcut
