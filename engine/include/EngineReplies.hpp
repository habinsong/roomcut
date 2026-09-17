#ifndef ROOMCUT_ENGINE_REPLIES_HPP
#define ROOMCUT_ENGINE_REPLIES_HPP

#include "roomcut_handshake.h"
#include <string>

namespace roomcut {
class EngineSoundState;
struct ComparisonMetrics;
struct AnalysisSnapshot;

// An owned control-thread snapshot. Collect HAL/atomic observations before
// building a reply; conversion itself never reads devices or shared state.
struct EngineStatusSnapshot {
    RoomcutEngineLifecycle lifecycle = ROOMCUT_ENGINE_STARTING;
    bool manualBypass = false, safeBypass = false, outputRunning = false;
    float limiterReductionDb = 0, renderPeak = 0;
    uint64_t framesRendered = 0, ringUnderruns = 0;
    std::string outputDeviceUID;
    bool keepDefault = false;
    double volumeBoost = 1;
    double engineLatencyMs = 0;   // limiter look-ahead + resampler group delay
    bool bedRendererAttached = false, bedPersonalizedHrtf = false;
    float bedExternalGain = 0, bedUnitRate = 0;
};

RoomcutEngineState presentedEngineState(RoomcutEngineLifecycle lifecycle, bool manualBypass, bool safeBypass);
RoomcutStateReply makeStateReply(const EngineSoundState& sound, const EngineStatusSnapshot& runtime);
RoomcutGetParamsReply makeParamsReply(const EngineSoundState& sound);
RoomcutComparisonReply makeComparisonReply(const EngineSoundState& sound, const ComparisonMetrics& meters,
                                          bool outputRunning, bool manualBypass);
RoomcutAnalysisReply makeAnalysisReply(const AnalysisSnapshot& analysis);
} // namespace roomcut
#endif
