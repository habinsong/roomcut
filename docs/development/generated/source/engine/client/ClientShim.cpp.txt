/*
 * ClientShim.cpp — see include/roomcut_client.h.
 *
 * Converts the plain-C API to engine commands and device operations. Mach
 * ownership stays in EngineConnection; HAL IO lives in CoreAudioDevices.
 * Compiled into the SwiftPM target CRoomcutClient (see Package.swift).
 */
#include "roomcut_client.h"

#include "Control.hpp"
#include "EngineConnection.hpp"
#include "ParameterCodec.hpp"
#include "CoreAudioDevices.hpp"

#include "presets/BuiltinPresets.hpp"

#include <cmath>
#include <cstring>
#include <vector>

namespace {
namespace devices = roomcut::devices;

constexpr uint32_t kTimeoutMs = 500;

roomcut::EngineConnection engineConnection;

template <typename Fn>
int withEngine(Fn&& fn) {
    return engineConnection.perform(std::forward<Fn>(fn));
}

double clampEffectiveVolume(double scalar) {
    if (!std::isfinite(scalar)) return 1.0;
    if (scalar < 0.0) return 0.0;
    if (scalar > 2.0) return 2.0;
    return scalar;
}

double clampVolumeBoost(double boost) {
    if (!std::isfinite(boost)) return 1.0;
    if (boost < 1.0) return 1.0;
    if (boost > 2.0) return 2.0;
    return boost;
}

int setEngineVolumeBoost(double boost) {
    boost = clampVolumeBoost(boost);
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetVolumeBoost(svc, boost, kTimeoutMs, status);
    });
}

// Pick the device whose per-channel balance the user hears: the real output
// device (what Audio MIDI Setup shows for e.g. "iFi USB Audio SE") when one is
// selected, else the Roomcut virtual device. Mirrors the volume device choice.
AudioDeviceID findBalanceDevice() {
    RoomcutClientState state{};
    if (roomcutClientGetState(&state) != 0) return kAudioObjectUnknown;
    if (state.outputDeviceUID[0] != '\0') return devices::realOutput(state.outputDeviceUID);
    return devices::roomcutOutput();
}

} // namespace

extern "C" {

int roomcutClientGetState(RoomcutClientState* out) {
    if (out == nullptr) {
        return -3;
    }
    RoomcutStateReply rep;
    std::memset(&rep, 0, sizeof(rep));
    int rc = withEngine([&](mach_port_t svc, uint32_t*) {
        return roomcut::controlGetState(svc, kTimeoutMs, &rep);
    });
    if (rc != 0) {
        return rc;
    }
    std::memset(out, 0, sizeof(*out));
    out->state        = rep.state;
    out->manualBypass = rep.manualBypass;
    out->safeBypass   = rep.safeBypass;
    static_assert(sizeof(out->presetId) <= sizeof(rep.presetId),
                  "presetId capacity");
    std::memcpy(out->presetId, rep.presetId, sizeof(out->presetId));
    out->presetId[sizeof(out->presetId) - 1] = '\0';
    out->limiterGainReductionDb = rep.limiterGainReductionDb;
    out->renderPeak             = rep.renderPeak;
    out->paramsRevision         = rep.paramsRevision;
    out->framesRendered         = rep.framesRendered;
    out->ringUnderruns          = rep.ringUnderruns;
    static_assert(sizeof(out->outputDeviceUID) <= sizeof(rep.outputDeviceUID),
                  "outputDeviceUID capacity");
    std::memcpy(out->outputDeviceUID, rep.outputDeviceUID, sizeof(out->outputDeviceUID));
    out->outputDeviceUID[sizeof(out->outputDeviceUID) - 1] = '\0';
    out->keepDefault = rep.keepDefault;
    out->capabilities = rep.capabilities;
    out->engineLatencyMs = rep.engineLatencyMs;
    out->volumeBoost = clampVolumeBoost(rep.volumeBoost);
    return 0;
}

int roomcutClientGetComparison(RoomcutClientComparison* out) {
    if (!out) return -3;
    RoomcutComparisonReply reply{};
    const int result = withEngine([&](mach_port_t service, uint32_t*) {
        return roomcut::controlGetComparison(service, kTimeoutMs, &reply);
    });
    if (result != 0) return result;
    std::memset(out, 0, sizeof(*out));
    out->enabled = reply.enabled;
    out->state = reply.state;
    out->revision = reply.revision;
    out->renderedRevision = reply.renderedRevision;
    out->currentReductionDb = reply.currentReductionDb;
    out->referenceReductionDb = reply.referenceReductionDb;
    std::memcpy(out->presetId, reply.presetId, sizeof(out->presetId));
    roomcut::encodeParameters(roomcut::decodeParameters(reply.current), out->current);
    roomcut::encodeParameters(roomcut::decodeParameters(reply.reference), out->reference);
    return 0;
}

int roomcutClientSetComparison(const RoomcutClientParams* current,
                                const RoomcutClientParams* reference,
                                int enabled, const char* builtinPresetId) {
    if (!reference || enabled < 0 || enabled > 1) return -3;
    RoomcutComparisonRequest request{};
    request.enabled = static_cast<uint32_t>(enabled);
    if (builtinPresetId && builtinPresetId[0]) {
        request.kind = ROOMCUT_COMPARISON_PRESET;
        std::snprintf(request.presetId, sizeof(request.presetId), "%s", builtinPresetId);
    } else {
        if (!current) return -3;
        request.kind = ROOMCUT_COMPARISON_PARAMETERS;
        roomcut::encodeParameters(roomcut::decodeParameters(*current), request.current);
    }
    roomcut::encodeParameters(roomcut::decodeParameters(*reference), request.reference);
    return withEngine([&](mach_port_t service, uint32_t* status) {
        return roomcut::controlSetComparison(service, request, kTimeoutMs, status);
    });
}

int roomcutClientGetParams(RoomcutClientParams* out) {
    if (out == nullptr) {
        return -3;
    }
    static_assert(ROOMCUT_CLIENT_EQ_BANDS == ROOMCUT_EQ_BANDS,
                  "EQ band count drifted from the wire protocol");
    RoomcutGetParamsReply rep;
    std::memset(&rep, 0, sizeof(rep));
    int rc = withEngine([&](mach_port_t svc, uint32_t*) {
        return roomcut::controlGetParams(svc, kTimeoutMs, &rep);
    });
    if (rc != 0) {
        return rc;
    }
    std::memset(out, 0, sizeof(*out));
    roomcut::encodeParameters(roomcut::decodeParameters(rep), *out);
    return 0;
}

int roomcutClientGetAnalysis(RoomcutClientAnalysis* out) {
    if (out == nullptr) {
        return -3;
    }
    static_assert(ROOMCUT_CLIENT_ANALYSIS_SPECTRUM_BINS == ROOMCUT_ANALYSIS_SPECTRUM_BINS,
                  "analysis spectrum bin count drifted from the wire protocol");
    RoomcutAnalysisReply rep;
    std::memset(&rep, 0, sizeof(rep));
    int rc = withEngine([&](mach_port_t svc, uint32_t*) {
        return roomcut::controlGetAnalysis(svc, kTimeoutMs, &rep);
    });
    if (rc != 0) {
        return rc;
    }
    std::memset(out, 0, sizeof(*out));
    out->valid = rep.valid;
    out->sampleRate = rep.sampleRate;
    out->channels = rep.channels;
    out->framesAnalyzed = rep.framesAnalyzed;
    out->peakDb = rep.peakDb;
    out->rmsDb = rep.rmsDb;
    out->crestFactor = rep.crestFactor;
    out->lowEnergy = rep.lowEnergy;
    out->lowMidEnergy = rep.lowMidEnergy;
    out->midEnergy = rep.midEnergy;
    out->highEnergy = rep.highEnergy;
    out->spectralCentroid = rep.spectralCentroid;
    out->stereoWidth = rep.stereoWidth;
    out->midSideRatio = rep.midSideRatio;
    out->correlation = rep.correlation;
    out->muddiness = rep.muddiness;
    out->harshness = rep.harshness;
    out->sibilance = rep.sibilance;
    out->voicePresence = rep.voicePresence;
    out->reverbEstimate = rep.reverbEstimate;
    out->dynamicRange = rep.dynamicRange;
    std::memcpy(out->spectrum, rep.spectrum, sizeof(out->spectrum));
    return 0;
}

int roomcutClientSetPreset(const char* presetId) {
    if (presetId == nullptr) {
        return -3;
    }
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetPreset(svc, presetId, kTimeoutMs, status);
    });
}

int roomcutClientSetBypass(int on) {
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetBypass(svc, on != 0, kTimeoutMs, status);
    });
}

int roomcutClientSetOutputDevice(const char* uid) {
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetDevice(svc, uid, kTimeoutMs, status);
    });
}

int roomcutClientSetKeepDefault(int on) {
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetKeepDefault(svc, on != 0, kTimeoutMs, status);
    });
}

int roomcutClientOutputDeviceCount(void) {
    return (int)devices::realOutputs().size();
}

int roomcutClientOutputDeviceInfo(int index, char* uidOut, int uidCap,
                                  char* nameOut, int nameCap) {
    const auto devs = devices::realOutputs();
    if (index < 0 || index >= (int)devs.size()) {
        return -3;
    }
    const auto& d = devs[(size_t)index];
    if (uidOut != nullptr && uidCap > 0) {
        std::snprintf(uidOut, (size_t)uidCap, "%s", d.uid.c_str());
    }
    if (nameOut != nullptr && nameCap > 0) {
        std::snprintf(nameOut, (size_t)nameCap, "%s", d.name.c_str());
    }
    return 0;
}

int roomcutClientAudioFormat(const char* uid, RoomcutClientAudioFormat* out) {
    if (out == nullptr) return -3;
    devices::AudioFormat format{};
    const int result = devices::audioFormat(devices::realOutput(uid), &format);
    if (result != 0) return result;
    out->bitDepth = format.bitDepth;
    out->sampleRate = format.sampleRate;
    out->latencyMs = format.latencyMs;
    return 0;
}

int roomcutClientDeviceFormatOptions(const char* uid,
                                     RoomcutClientDeviceFormat* out, int cap) {
    std::vector<devices::FormatOption> formats;
    const int count = devices::formatOptions(devices::realOutput(uid), formats);
    if (count < 0) return count;
    if (out != nullptr && cap > 0) {
        const int written = count < cap ? count : cap;
        for (int i = 0; i < written; ++i) {
            out[i].sampleRate = formats[(size_t)i].sampleRate;
            out[i].bitDepth = formats[(size_t)i].bitDepth;
        }
    }
    return count;
}

int roomcutClientSetDeviceFormat(const char* uid, double sampleRate, unsigned bitDepth) {
    return devices::setFormat(devices::realOutput(uid), sampleRate, bitDepth);
}

int roomcutClientVolumeGet(double* outScalar) {
    if (outScalar == nullptr) return -3;
    RoomcutClientState state;
    std::memset(&state, 0, sizeof(state));
    const bool haveState = roomcutClientGetState(&state) == 0;
    const double boost = haveState ? clampVolumeBoost(state.volumeBoost) : 1.0;
    if (haveState && state.outputDeviceUID[0] != '\0') {
        double realVolume = 0.0;
        if (devices::volume(devices::realOutput(state.outputDeviceUID), &realVolume)) {
            *outScalar = clampEffectiveVolume(realVolume * boost);
            return 0;
        }
    }

    AudioDeviceID rc = devices::roomcutOutput();
    if (rc == kAudioObjectUnknown) return -1;
    double roomcutVolume = 0.0;
    if (!devices::volume(rc, &roomcutVolume)) return 1;
    *outScalar = clampEffectiveVolume(roomcutVolume * boost);
    return 0;
}

int roomcutClientVolumeSet(double scalar) {
    if (!std::isfinite(scalar)) return -3;
    scalar = clampEffectiveVolume(scalar);
    const double hardwareScalar = scalar > 1.0 ? 1.0 : scalar;
    const double boost = scalar > 1.0 ? scalar : 1.0;

    RoomcutClientState state{};
    const int stateResult = roomcutClientGetState(&state);
    if (stateResult != 0) return stateResult; // Existing boost must be known before changing the hardware level.
    const bool supportsBoost = (state.capabilities & ROOMCUT_CLIENT_CAP_VOLUME_BOOST) != 0;
    if (!supportsBoost && boost > 1.0) return 1;

    const AudioDeviceID real = devices::realOutput(state.outputDeviceUID);
    if (state.outputDeviceUID[0] != '\0' && real == kAudioObjectUnknown) return -1;
    const AudioDeviceID rc = devices::roomcutOutput();
    if (real == kAudioObjectUnknown && rc == kAudioObjectUnknown) return -1;

    if (supportsBoost) {
        const int result = setEngineVolumeBoost(boost);
        if (result != 0) return result;
    }
    int realResult = 1;
    if (real != kAudioObjectUnknown) {
        realResult = devices::setVolume(real, hardwareScalar);
        if (realResult < 0) return realResult;
    }
    // Non-settable hardware can use the engine's software-volume fallback.
    // A present virtual device is authoritative; its failure cannot be hidden
    // by a real-device write that the engine may subsequently overwrite.
    if (rc != kAudioObjectUnknown) return devices::setVolume(rc, hardwareScalar);
    return realResult;
}

int roomcutClientBalanceGet(double* outPan) {
    if (outPan == nullptr) return -3;
    return devices::balance(findBalanceDevice(), outPan);
}

int roomcutClientBalanceSet(double pan) {
    if (!std::isfinite(pan)) return -3;
    return devices::setBalance(findBalanceDevice(), pan);
}

int roomcutClientMakeDefaultOutput(void) {
    AudioDeviceID rc = devices::roomcutOutput();
    if (rc == kAudioObjectUnknown) return -1;
    return devices::setDefaultOutput(rc);
}

int roomcutClientRestoreRealDefault(void) {
    // Prefer the real device the engine renders to (what the user thinks of as
    // "my output"); fall back to any real output device if the engine hasn't
    // reported one yet.
    AudioDeviceID target = kAudioObjectUnknown;
    RoomcutClientState st;
    std::memset(&st, 0, sizeof(st));
    if (roomcutClientGetState(&st) == 0 && st.outputDeviceUID[0] != '\0') {
        target = devices::realOutput(st.outputDeviceUID);
    }
    if (target == kAudioObjectUnknown) {
        const auto devs = devices::realOutputs();
        if (!devs.empty()) target = devs.front().id;
    }
    if (target == kAudioObjectUnknown) return -1;
    return devices::setDefaultOutput(target);
}

int roomcutClientRoomcutIsDefault(void) {
    AudioDeviceID rc = devices::roomcutOutput();
    if (rc == kAudioObjectUnknown) return -1;
    AudioDeviceID cur = devices::defaultOutput();
    if (cur == kAudioObjectUnknown) return -1;
    return cur == rc ? 1 : 0;
}

int roomcutClientSetParams(double preampDb,
                           const double eqGainsDb[ROOMCUT_CLIENT_EQ_BANDS],
                           double limiterReleaseMs,
                           double outputGainDb, double spatialWidth,
                           double centerFocus, double crossfeed,
                           double roomReduce, double spatialMode,
                           double highpassHz, double compAmount,
                           const RoomcutClientParamBand parametric[ROOMCUT_CLIENT_PARAM_BANDS],
                           const RoomcutClientParamDynamics dynamics[ROOMCUT_CLIENT_PARAM_BANDS]) {
    if (eqGainsDb == nullptr) {
        return -3;
    }
    static_assert(ROOMCUT_CLIENT_EQ_BANDS == ROOMCUT_EQ_BANDS,
                  "EQ band count drifted from the wire protocol");
    static_assert(ROOMCUT_CLIENT_PARAM_BANDS == ROOMCUT_PARAM_BANDS,
                  "parametric band count drifted from the wire protocol");
    RoomcutParamBand bands[ROOMCUT_PARAM_BANDS];
    std::memset(bands, 0, sizeof(bands));
    if (parametric != nullptr) {
        for (int b = 0; b < ROOMCUT_PARAM_BANDS; ++b) {
            bands[b].enabled = parametric[b].enabled;
            bands[b].type    = parametric[b].type;
            bands[b].freqHz  = parametric[b].freqHz;
            bands[b].gainDb  = parametric[b].gainDb;
            bands[b].q       = parametric[b].q;
        }
    }
    RoomcutParamDynamics dyn[ROOMCUT_PARAM_BANDS];
    std::memset(dyn, 0, sizeof(dyn));
    if (dynamics != nullptr) {
        for (int b = 0; b < ROOMCUT_PARAM_BANDS; ++b) {
            dyn[b].enabled     = dynamics[b].enabled;
            dyn[b].thresholdDb = dynamics[b].thresholdDb;
            dyn[b].rangeDb     = dynamics[b].rangeDb;
            dyn[b].attackMs    = dynamics[b].attackMs;
            dyn[b].releaseMs   = dynamics[b].releaseMs;
        }
    }
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetParams(svc, preampDb, eqGainsDb,
                                         limiterReleaseMs,
                                         outputGainDb, spatialWidth,
                                         centerFocus, crossfeed, roomReduce, spatialMode,
                                         highpassHz, compAmount,
                                         bands, dyn, kTimeoutMs, status);
    });
}

int roomcutClientPresetCount(void) {
    return (int)roomcut::builtinPresets().size();
}

int roomcutClientPresetInfo(int index, char* idOut, int idCap,
                            char* nameOut, int nameCap) {
    const auto presets = roomcut::builtinPresets();
    if (index < 0 || index >= (int)presets.size()) {
        return -3;
    }
    const auto& p = presets[(size_t)index];
    if (idOut != nullptr && idCap > 0) {
        std::snprintf(idOut, (size_t)idCap, "%s", p.id.c_str());
    }
    if (nameOut != nullptr && nameCap > 0) {
        std::snprintf(nameOut, (size_t)nameCap, "%s", p.name.c_str());
    }
    return 0;
}

} // extern "C"
