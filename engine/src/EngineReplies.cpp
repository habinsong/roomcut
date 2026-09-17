#include "EngineReplies.hpp"
#include "EngineSoundState.hpp"
#include "Lifecycle.hpp"
#include "ParameterCodec.hpp"
#include "dsp/Analyzer.hpp"
#include <cstdio>
#include <cstring>

namespace roomcut {
RoomcutEngineState presentedEngineState(RoomcutEngineLifecycle lifecycle, bool manual, bool safe) {
    const auto coarse = engineWireState(lifecycle);
    return coarse == ROOMCUT_STATE_RUNNING && (manual || safe) ? ROOMCUT_STATE_BYPASS : coarse;
}

RoomcutStateReply makeStateReply(const EngineSoundState& sound, const EngineStatusSnapshot& runtime) {
    RoomcutStateReply reply;
    std::memset(&reply, 0, sizeof(reply));
    reply.state = presentedEngineState(runtime.lifecycle, runtime.manualBypass, runtime.safeBypass);
    reply.manualBypass = runtime.manualBypass;
    reply.safeBypass = runtime.safeBypass;
    std::snprintf(reply.presetId, sizeof(reply.presetId), "%s", sound.presetID());
    reply.paramsRevision = sound.revision();
    reply.limiterGainReductionDb = runtime.limiterReductionDb;
    reply.renderPeak = runtime.renderPeak;
    reply.framesRendered = runtime.framesRendered;
    reply.ringUnderruns = runtime.ringUnderruns;
    if (runtime.outputRunning)
        std::snprintf(reply.outputDeviceUID, sizeof(reply.outputDeviceUID), "%s", runtime.outputDeviceUID.c_str());
    reply.keepDefault = runtime.keepDefault;
    reply.capabilities = ROOMCUT_CAP_SPATIAL_PARAMS | ROOMCUT_CAP_PARAMETRIC | ROOMCUT_CAP_ANALYZER
        | ROOMCUT_CAP_VOLUME_BOOST | ROOMCUT_CAP_DYNAMICS | ROOMCUT_CAP_LEVEL_MATCH
        | ROOMCUT_CAP_DYNAMIC_EQ | ROOMCUT_CAP_VIRTUAL_ROOM | ROOMCUT_CAP_HEAD_TRACKING
        | ROOMCUT_CAP_UPMIX | ROOMCUT_CAP_BED_RENDERER;
    reply.volumeBoost = runtime.volumeBoost;
    reply.engineLatencyMs = runtime.engineLatencyMs;
    reply.bedRenderer = runtime.bedRendererAttached ? 1 : 0;
    reply.bedPersonalizedHrtf = runtime.bedPersonalizedHrtf ? 1 : 0;
    reply.bedExternalGain = runtime.bedExternalGain;
    reply.bedUnitRate = runtime.bedUnitRate;
    return reply;
}

RoomcutGetParamsReply makeParamsReply(const EngineSoundState& sound) {
    RoomcutGetParamsReply reply;
    std::memset(&reply, 0, sizeof(reply));
    std::snprintf(reply.presetId, sizeof(reply.presetId), "%s", sound.presetID());
    reply.paramsRevision = sound.revision();
    encodeParameters(sound.parameters(), reply);
    return reply;
}

RoomcutComparisonReply makeComparisonReply(const EngineSoundState& sound, const ComparisonMetrics& meters,
                                          bool outputRunning, bool manualBypass) {
    RoomcutComparisonReply reply;
    std::memset(&reply, 0, sizeof(reply));
    const auto& settings = sound.settings();
    reply.enabled = settings.enabled;
    reply.revision = settings.revision;
    reply.renderedRevision = meters.revision;
    auto state = settings.enabled ? meters.state : LevelMatchState::Disabled;
    if (settings.enabled && meters.revision != settings.revision) state = LevelMatchState::Measuring;
    if (settings.enabled && !outputRunning) state = LevelMatchState::NoSignal;
    if (settings.enabled && manualBypass) state = LevelMatchState::Bypassed;
    reply.state = static_cast<uint32_t>(state);
    reply.currentReductionDb = meters.currentReductionDb;
    reply.referenceReductionDb = meters.referenceReductionDb;
    std::snprintf(reply.presetId, sizeof(reply.presetId), "%s", sound.presetID());
    encodeParameters(settings.current, reply.current);
    encodeParameters(settings.reference, reply.reference);
    // The room travels beside the parameter blocks (see the wire header), so the
    // app reads the same value here that GET_PARAMS reports.
    reply.currentRoomType = settings.current.roomType;
    reply.currentRoomAmount = settings.current.roomAmount;
    reply.referenceRoomType = settings.reference.roomType;
    reply.referenceRoomAmount = settings.reference.roomAmount;
    reply.currentSurroundType = settings.current.surroundType;
    reply.currentCenterWidth = settings.current.centerWidth;
    reply.currentSurroundDepth = settings.current.surroundDepth;
    reply.referenceSurroundType = settings.reference.surroundType;
    reply.referenceCenterWidth = settings.reference.centerWidth;
    reply.referenceSurroundDepth = settings.reference.surroundDepth;
    reply.currentBedRenderer = settings.current.bedRenderer;
    reply.referenceBedRenderer = settings.reference.bedRenderer;
    return reply;
}

RoomcutAnalysisReply makeAnalysisReply(const AnalysisSnapshot& snap) {
    RoomcutAnalysisReply reply;
    std::memset(&reply, 0, sizeof(reply));
    reply.valid = snap.valid;
    reply.sampleRate = snap.sampleRate;
    reply.channels = snap.channels;
    reply.framesAnalyzed = snap.framesAnalyzed;
    reply.peakDb = snap.peakDb;
    reply.rmsDb = snap.rmsDb;
    reply.crestFactor = snap.crestFactor;
    reply.lowEnergy = snap.lowEnergy;
    reply.lowMidEnergy = snap.lowMidEnergy;
    reply.midEnergy = snap.midEnergy;
    reply.highEnergy = snap.highEnergy;
    reply.spectralCentroid = snap.spectralCentroid;
    reply.stereoWidth = snap.stereoWidth;
    reply.midSideRatio = snap.midSideRatio;
    reply.correlation = snap.correlation;
    reply.muddiness = snap.muddiness;
    reply.harshness = snap.harshness;
    reply.sibilance = snap.sibilance;
    reply.voicePresence = snap.voicePresence;
    reply.reverbEstimate = snap.reverbEstimate;
    reply.dynamicRange = snap.dynamicRange;
    static_assert(AnalysisSnapshot::kSpectrumBins == ROOMCUT_ANALYSIS_SPECTRUM_BINS);
    for (std::size_t i = 0; i < snap.spectrum.size(); ++i) reply.spectrum[i] = snap.spectrum[i];
    return reply;
}
} // namespace roomcut
