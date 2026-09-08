#include "EngineState.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>

namespace roomcut {

void capturePersistentSound(PersistentState& state, const ChainParams& params, const char* presetID) {
    state.presetId = presetID;
    state.paramsLine = state.presetId == "custom" ? serializeParamsLine(params) : std::string();
    state.parametricLine = serializeParametricLine(params);
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
        << ' ' << params.highpassHz << ' ' << params.compAmount;
    return output.str();
}

bool parseParamsLine(const std::string& line, ChainParams* params) {
    if (params == nullptr) return false;
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    constexpr size_t base = GraphicEQ::kNumBands + 3;
    double values[base + 7]{};
    for (size_t i = 0; i < base + 7; ++i) {
        input >> std::ws;
        if (i >= base && input.eof()) break; // pre-spatial/dynamics state files
        if (!(input >> values[i]) || !std::isfinite(values[i])) return false;
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
