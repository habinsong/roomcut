/*
 * roomcutctl.cpp — Roomcut control CLI (Phase 6; the `roomcutctl` row of the
 * DEVELOPMENT_PLAN component table). Drives the engine over the same Mach
 * control plane the menu-bar app will use, so every subcommand here doubles
 * as a reference client for the app's IPC.
 *
 *   roomcutctl status            engine state / preset / limiter GR / counters
 *   roomcutctl status --json     same, as one machine-readable JSON line
 *   roomcutctl preset list       builtin preset ids
 *   roomcutctl preset list --json builtin presets as a JSON array
 *   roomcutctl preset <id>       apply a builtin preset (live, crossfaded)
 *   roomcutctl params get        read current engine ChainParams
 *   roomcutctl params get --json read current engine ChainParams as JSON
 *   roomcutctl analysis --json   read latest analyzer snapshot as JSON
 *   roomcutctl params P g0..g9 R O [W F X RR]
 *   roomcutctl bypass on|off     manual bypass (crossfaded)
 *   roomcutctl health            heartbeat probe (liveness + coarse state)
 */
#include "Control.hpp"
#include "Heartbeat.hpp"

#include "presets/BuiltinPresets.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <bootstrap.h>

using namespace roomcut;

namespace {

constexpr uint32_t kTimeoutMs = 1000;

const char* stateName(uint32_t s) {
    switch (s) {
        case ROOMCUT_STATE_STOPPED: return "STOPPED";
        case ROOMCUT_STATE_RUNNING: return "RUNNING";
        case ROOMCUT_STATE_BYPASS:  return "BYPASS";
        case ROOMCUT_STATE_RECOVER: return "RECOVER";
    }
    return "?";
}

mach_port_t lookupEngine() {
    mach_port_t bp = MACH_PORT_NULL;
    if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS) {
        std::fprintf(stderr, "roomcutctl: no bootstrap port\n");
        return MACH_PORT_NULL;
    }
    mach_port_t service = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_look_up(bp, ROOMCUT_MACH_SERVICE_NAME, &service);
    if (kr != KERN_SUCCESS || service == MACH_PORT_NULL) {
        std::fprintf(stderr, "roomcutctl: engine not reachable (%s)\n",
                     bootstrap_strerror(kr));
        return MACH_PORT_NULL;
    }
    return service;
}

int usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s status [--json] | preset list [--json] | preset <id> | "
        "params get [--json] | analysis --json | "
        "params <preamp> <g0..g9> <releaseMs> <outDb> "
        "[<width> <centerFocus> <crossfeed> <roomReduce>] | "
        "peq <type> <freqHz> <gainDb> <q> | "
        "device <uid|auto> | "
        "bypass on|off | keepdefault on|off | health\n",
        argv0);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage(argv[0]);
    }
    const char* cmd = argv[1];

    // `preset list` needs no engine.
    if (std::strcmp(cmd, "preset") == 0 && argc > 2 &&
        std::strcmp(argv[2], "list") == 0) {
        const bool json = (argc > 3 && std::strcmp(argv[3], "--json") == 0);
        if (json) {
            std::printf("[");
            bool first = true;
            for (const auto& bp : builtinPresets()) {
                std::printf("%s{\"id\":\"%s\",\"name\":\"%s\"}",
                            first ? "" : ",", bp.id.c_str(), bp.name.c_str());
                first = false;
            }
            std::printf("]\n");
        } else {
            for (const auto& bp : builtinPresets()) {
                std::printf("%-10s %s\n", bp.id.c_str(), bp.name.c_str());
            }
        }
        return 0;
    }

    // `status` is handled before the engine lookup so --json can report
    // engineReachable:false cleanly (exit 0) when the engine is down — the app
    // polls this and shows an "offline" state rather than treating it as error.
    if (std::strcmp(cmd, "status") == 0 &&
        (argc == 2 || (argc == 3 && std::strcmp(argv[2], "--json") == 0))) {
        const bool json = (argc == 3);
        mach_port_t svc = lookupEngine();
        if (svc == MACH_PORT_NULL) {
            if (json) {
                std::printf("{\"engineReachable\":false}\n");
                return 0;
            }
            return 1; // lookupEngine already printed to stderr
        }
        RoomcutStateReply st;
        kern_return_t kr = controlGetState(svc, kTimeoutMs, &st);
        mach_port_deallocate(mach_task_self(), svc);
        if (kr != KERN_SUCCESS) {
            if (json) { std::printf("{\"engineReachable\":false}\n"); return 0; }
            std::fprintf(stderr, "roomcutctl: status failed (%d)\n", kr);
            return 1;
        }
        const double volumeBoost =
            (st.volumeBoost >= 1.0 && st.volumeBoost <= 2.0) ? st.volumeBoost : 1.0;
        if (json) {
            std::printf(
                "{\"engineReachable\":true,\"state\":\"%s\",\"preset\":\"%s\","
                "\"paramsRevision\":%u,\"capabilities\":%u,\"volumeBoost\":%.4f,"
                "\"manualBypass\":%s,\"safeBypass\":%s,\"limiterGrDb\":%.4f,"
                "\"peak\":%.6f,\"frames\":%llu,\"underruns\":%llu,"
                "\"outputDevice\":\"%s\",\"keepDefault\":%s}\n",
                stateName(st.state), st.presetId, st.paramsRevision, st.capabilities,
                volumeBoost,
                st.manualBypass ? "true" : "false",
                st.safeBypass ? "true" : "false",
                (double)st.limiterGainReductionDb, (double)st.renderPeak,
                (unsigned long long)st.framesRendered,
                (unsigned long long)st.ringUnderruns,
                st.outputDeviceUID,
                st.keepDefault ? "true" : "false");
        } else {
            std::printf("state:    %s\n", stateName(st.state));
            std::printf("preset:   %s\n", st.presetId);
            std::printf("revision: %u\n", st.paramsRevision);
            std::printf("boost:    %.2fx\n", volumeBoost);
            std::printf("caps:     %s%s%s%s\n",
                        (st.capabilities & ROOMCUT_CAP_SPATIAL_PARAMS) ? "spatial" : "",
                        (st.capabilities & ROOMCUT_CAP_PARAMETRIC) ? " parametric" : "",
                        (st.capabilities & ROOMCUT_CAP_ANALYZER) ? " analyzer" : "",
                        st.capabilities == 0 ? "(none)" : "");
            std::printf("bypass:   manual=%s safe=%s\n",
                        st.manualBypass ? "on" : "off",
                        st.safeBypass ? "on" : "off");
            std::printf("limiter:  GR %.2f dB%s\n", st.limiterGainReductionDb,
                        st.limiterGainReductionDb > 0.05f ? "  [clipping]" : "");
            std::printf("peak:     %.5f\n", st.renderPeak);
            std::printf("frames:   %llu (underruns %llu)\n",
                        (unsigned long long)st.framesRendered,
                        (unsigned long long)st.ringUnderruns);
            std::printf("output:   %s\n",
                        st.outputDeviceUID[0] ? st.outputDeviceUID : "(none)");
        }
        return 0;
    }

    if (std::strcmp(cmd, "analysis") == 0 &&
        argc == 3 && std::strcmp(argv[2], "--json") == 0) {
        mach_port_t svc = lookupEngine();
        if (svc == MACH_PORT_NULL) {
            std::printf("{\"engineReachable\":false}\n");
            return 0;
        }
        RoomcutAnalysisReply a;
        kern_return_t kr = controlGetAnalysis(svc, kTimeoutMs, &a);
        mach_port_deallocate(mach_task_self(), svc);
        if (kr != KERN_SUCCESS) {
            std::printf("{\"engineReachable\":false}\n");
            return 0;
        }
        std::printf(
            "{\"engineReachable\":true,\"valid\":%s,\"sampleRate\":%u,"
            "\"channels\":%u,\"framesAnalyzed\":%llu,"
            "\"peakDb\":%.4f,\"rmsDb\":%.4f,\"crestFactor\":%.4f,"
            "\"lowEnergy\":%.4f,\"lowMidEnergy\":%.4f,\"midEnergy\":%.4f,"
            "\"highEnergy\":%.4f,\"spectralCentroid\":%.4f,"
            "\"stereoWidth\":%.4f,\"midSideRatio\":%.4f,"
            "\"muddiness\":%.4f,\"harshness\":%.4f,\"sibilance\":%.4f,"
            "\"voicePresence\":%.4f,\"reverbEstimate\":%.4f,"
            "\"dynamicRange\":%.4f,\"spectrum\":[",
            a.valid ? "true" : "false", a.sampleRate, a.channels,
            (unsigned long long)a.framesAnalyzed,
            (double)a.peakDb, (double)a.rmsDb, (double)a.crestFactor,
            (double)a.lowEnergy, (double)a.lowMidEnergy, (double)a.midEnergy,
            (double)a.highEnergy, (double)a.spectralCentroid,
            (double)a.stereoWidth, (double)a.midSideRatio,
            (double)a.muddiness, (double)a.harshness, (double)a.sibilance,
            (double)a.voicePresence, (double)a.reverbEstimate,
            (double)a.dynamicRange);
        for (int i = 0; i < ROOMCUT_ANALYSIS_SPECTRUM_BINS; ++i) {
            std::printf("%s%.4f", i == 0 ? "" : ",", (double)a.spectrum[i]);
        }
        std::printf("]}\n");
        return 0;
    }

    mach_port_t service = lookupEngine();
    if (service == MACH_PORT_NULL) {
        return 1;
    }
    int rc = 1;

    if (std::strcmp(cmd, "preset") == 0 && argc == 3) {
        uint32_t status = 1;
        kern_return_t kr = controlSetPreset(service, argv[2], kTimeoutMs, &status);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "roomcutctl: preset failed (%d)\n", kr);
        } else if (status != 0) {
            std::fprintf(stderr, "roomcutctl: unknown preset '%s' (try: preset list)\n",
                         argv[2]);
        } else {
            std::printf("preset -> %s\n", argv[2]);
            rc = 0;
        }
    } else if (std::strcmp(cmd, "device") == 0 && argc == 3) {
        // `device <uid>` pins the real output; `device auto` clears the pin.
        const char* uid = std::strcmp(argv[2], "auto") == 0 ? "" : argv[2];
        uint32_t status = 1;
        kern_return_t kr = controlSetDevice(service, uid, kTimeoutMs, &status);
        if (kr != KERN_SUCCESS || status != 0) {
            std::fprintf(stderr, "roomcutctl: device failed (%d)\n", kr);
        } else {
            std::printf("device -> %s\n", uid[0] ? uid : "(auto)");
            rc = 0;
        }
    } else if (std::strcmp(cmd, "bypass") == 0 && argc == 3 &&
               (std::strcmp(argv[2], "on") == 0 || std::strcmp(argv[2], "off") == 0)) {
        const bool on = std::strcmp(argv[2], "on") == 0;
        uint32_t status = 1;
        kern_return_t kr = controlSetBypass(service, on, kTimeoutMs, &status);
        if (kr != KERN_SUCCESS || status != 0) {
            std::fprintf(stderr, "roomcutctl: bypass failed (%d)\n", kr);
        } else {
            std::printf("bypass %s\n", argv[2]);
            rc = 0;
        }
    } else if (std::strcmp(cmd, "keepdefault") == 0 && argc == 3 &&
               (std::strcmp(argv[2], "on") == 0 || std::strcmp(argv[2], "off") == 0)) {
        const bool on = std::strcmp(argv[2], "on") == 0;
        uint32_t status = 1;
        kern_return_t kr = controlSetKeepDefault(service, on, kTimeoutMs, &status);
        if (kr != KERN_SUCCESS || status != 0) {
            std::fprintf(stderr, "roomcutctl: keepdefault failed (%d)\n", kr);
        } else {
            std::printf("keepdefault %s\n", argv[2]);
            rc = 0;
        }
    } else if (std::strcmp(cmd, "params") == 0 && argc >= 3 &&
               std::strcmp(argv[2], "get") == 0) {
        const bool json = (argc == 4 && std::strcmp(argv[3], "--json") == 0);
        if (argc != 3 && !json) {
            rc = usage(argv[0]);
        } else {
            RoomcutGetParamsReply params;
            kern_return_t kr = controlGetParams(service, kTimeoutMs, &params);
            if (kr != KERN_SUCCESS) {
                std::fprintf(stderr, "roomcutctl: params get failed (%d)\n", kr);
            } else if (json) {
                std::printf(
                    "{\"preset\":\"%s\",\"paramsRevision\":%u,\"preampDb\":%.4f,"
                    "\"eqGainsDb\":[",
                    params.presetId, params.paramsRevision, params.preampDb);
                for (int b = 0; b < ROOMCUT_EQ_BANDS; ++b) {
                    std::printf("%s%.4f", b == 0 ? "" : ",", params.eqGainsDb[b]);
                }
                std::printf(
                    "],\"limiterReleaseMs\":%.4f,"
                    "\"outputGainDb\":%.4f,\"spatialWidth\":%.4f,"
                    "\"centerFocus\":%.4f,\"crossfeed\":%.4f,"
                    "\"roomReduce\":%.4f,\"spatialMode\":%.4f}\n",
                    params.limiterReleaseMs,
                    params.outputGainDb, params.spatialWidth,
                    params.centerFocus, params.crossfeed, params.roomReduce,
                    params.spatialMode);
                rc = 0;
            } else {
                std::printf("preset:   %s\n", params.presetId);
                std::printf("revision: %u\n", params.paramsRevision);
                std::printf("preamp:   %.2f dB\n", params.preampDb);
                std::printf("eq:       ");
                for (int b = 0; b < ROOMCUT_EQ_BANDS; ++b) {
                    std::printf("%s%.2f", b == 0 ? "" : " ", params.eqGainsDb[b]);
                }
                std::printf(" dB\n");
                std::printf("limiter:  release %.2f ms (ceiling fixed at 0 dBFS)\n",
                            params.limiterReleaseMs);
                std::printf("output:   %.2f dB\n", params.outputGainDb);
                std::printf("spatial:  width %.2f, center %.2f, crossfeed %.2f, room %.2f, mode %.2f\n",
                            params.spatialWidth, params.centerFocus,
                            params.crossfeed, params.roomReduce, params.spatialMode);
                rc = 0;
            }
        }
    } else if (std::strcmp(cmd, "params") == 0 && (argc == 15 || argc == 19)) {
        double preamp = std::strtod(argv[2], nullptr);
        double gains[10];
        for (int b = 0; b < 10; ++b) gains[b] = std::strtod(argv[3 + b], nullptr);
        double releaseMs = std::strtod(argv[13], nullptr);
        double outDb     = std::strtod(argv[14], nullptr);
        double width = argc == 19 ? std::strtod(argv[15], nullptr) : 0.0;
        double center = argc == 19 ? std::strtod(argv[16], nullptr) : 0.0;
        double crossfeed = argc == 19 ? std::strtod(argv[17], nullptr) : 0.0;
        double room = argc == 19 ? std::strtod(argv[18], nullptr) : 0.0;
        uint32_t status = 1;
        // CLI does not edit parametric bands — pass none (the engine keeps the
        // band array flat for a custom set from the CLI).
        kern_return_t kr = controlSetParams(service, preamp, gains,
                                            releaseMs, outDb,
                                            width, center, crossfeed, room, 0.0 /* spatialMode */,
                                            nullptr, kTimeoutMs, &status);
        if (kr != KERN_SUCCESS || status != 0) {
            std::fprintf(stderr, "roomcutctl: params failed (%d)\n", kr);
        } else {
            std::printf("params -> custom\n");
            rc = 0;
        }
    } else if (std::strcmp(cmd, "peq") == 0 && argc == 6) {
        // peq <type> <freqHz> <gainDb> <q>: set parametric band 0 (others off),
        // everything else flat — to verify the parametric stage end-to-end.
        // type: 0 Bell 1 LowShelf 2 HighShelf 3 HighPass 4 LowPass 5 Notch.
        RoomcutParamBand bands[ROOMCUT_PARAM_BANDS];
        std::memset(bands, 0, sizeof(bands));
        bands[0].enabled = 1;
        bands[0].type    = (uint32_t)std::strtoul(argv[2], nullptr, 10);
        bands[0].freqHz  = std::strtod(argv[3], nullptr);
        bands[0].gainDb  = std::strtod(argv[4], nullptr);
        bands[0].q       = std::strtod(argv[5], nullptr);
        double flatGains[10] = {0,0,0,0,0,0,0,0,0,0};
        uint32_t status = 1;
        kern_return_t kr = controlSetParams(service, 0.0, flatGains,
                                            100.0, 0.0,
                                            0.0, 0.0, 0.0, 0.0, 0.0,
                                            bands, kTimeoutMs, &status);
        if (kr != KERN_SUCCESS || status != 0) {
            std::fprintf(stderr, "roomcutctl: peq failed (%d)\n", kr);
        } else {
            std::printf("peq -> band0 type=%u freq=%.0f gain=%.1f q=%.2f\n",
                        bands[0].type, bands[0].freqHz, bands[0].gainDb, bands[0].q);
            rc = 0;
        }
    } else if (std::strcmp(cmd, "health") == 0 && argc == 2) {
        uint32_t peerState = 0xffffffffu;
        kern_return_t kr = heartbeatProbe(service, 1, kTimeoutMs, &peerState);
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "roomcutctl: engine not responding (%d)\n", kr);
        } else {
            std::printf("engine alive; state %s\n", stateName(peerState));
            rc = 0;
        }
    } else {
        rc = usage(argv[0]);
    }

    mach_port_deallocate(mach_task_self(), service);
    return rc;
}
