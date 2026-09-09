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
    std::string dynamicsLine;
    std::string preferredOutputUID;
    bool keepRoomcutDefault = false;
    double volumeBoost = 1.0;
};

double clampVolumeBoost(double boost);
std::string serializeParamsLine(const ChainParams& params);
bool parseParamsLine(const std::string& line, ChainParams* params);
std::string serializeParametricLine(const ChainParams& params);
bool parseParametricLine(const std::string& line, ChainParams* params);
// Kept on its own line so a state file written before the dynamic side existed
// still loads: no line means every band is static, which is what it was.
std::string serializeDynamicsLine(const ChainParams& params);
bool parseDynamicsLine(const std::string& line, ChainParams* params);
void capturePersistentSound(PersistentState& state, const ChainParams& params, const char* presetID);

} // namespace roomcut
#endif
