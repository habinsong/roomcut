#ifndef ROOMCUT_ENGINE_STATE_HPP
#define ROOMCUT_ENGINE_STATE_HPP

#include <string>
#include "dsp/ChainParams.hpp"

namespace roomcut {

struct PersistentState {
    std::string realOutputUID;
    std::string presetId;
    std::string paramsLine;
    std::string parametricLine;
    std::string preferredOutputUID;
    bool keepRoomcutDefault = false;
    double volumeBoost = 1.0;
};

double clampVolumeBoost(double boost);
std::string serializeParamsLine(const ChainParams& params);
bool parseParamsLine(const std::string& line, ChainParams* params);
std::string serializeParametricLine(const ChainParams& params);
bool parseParametricLine(const std::string& line, ChainParams* params);
void capturePersistentSound(PersistentState& state, const ChainParams& params, const char* presetID);

} // namespace roomcut
#endif
