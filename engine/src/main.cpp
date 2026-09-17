/*
 * main.cpp — the engine process: start up, hand the driver a ring, then pump
 * control messages until told to stop.
 *
 * The runtime the render thread shares with this one — the context struct, the
 * CoreAudio render callback and their helpers — lives in EngineRuntime.hpp; the
 * control thread's loop and one handler per message in EngineControlLoop.hpp.
 */
#include "EngineControlLoop.hpp"



int main(int argc, char** argv) {
    // --dump <out.wav>: dev-only diagnostic — capture the post-DSP render
    // output and flush it as float32 WAV on exit so signal correctness
    // (freq/amplitude/gaps/clicks) is verifiable without ears
    // (scripts/analyze-dump.py). It records audio, so it is opt-in only and
    // must never be enabled by default (docs/06).
    // --eq <g0,g1,...,g9>: dev-only — band gains in dB (GraphicEQ::kCenters
    // order) so DSP curves are verifiable end-to-end before the app exists.
    // Absent flag = flat, identical to the default chain.
    EngineOptions options;
    std::string optionError;
    if (!parseEngineOptions(argc, argv, options, optionError)) {
        std::fprintf(stderr, "%s\n", optionError.c_str());
        return 2;
    }
    const char* dumpPath = options.dumping() ? options.dumpPath.c_str() : nullptr;
    ChainParams chainParams = options.params;
    const bool eqGiven = options.eqGiven;
    if (eqGiven) {
        std::fprintf(stderr, "[engine] dev EQ curve (dB):");
        for (double g : chainParams.eqGainsDb) std::fprintf(stderr, " %.1f", g);
        std::fprintf(stderr, "\n");
    }

    char currentPreset[ROOMCUT_PRESET_ID_MAX];
    std::snprintf(currentPreset, sizeof(currentPreset), "%s",
                  eqGiven ? "custom" : "flat");
    // Resume the last applied preset/params from the state file — without
    // this a fresh engine always boots flat, so reboot/crash-respawn/reinstall
    // all silently dropped the user's curve (2026-06-13). Dev --eq still wins.
    EngineStateStore stateStore(EngineStateStore::defaultPath());
    PersistentState pstate = stateStore.load();
    if (!eqGiven && !pstate.presetId.empty()) {
        bool resumed = false;
        if (pstate.presetId == "custom") {
            ChainParams restored = chainParams;
            resumed = parseParamsLine(pstate.paramsLine, &restored);
            if (resumed && !pstate.parametricLine.empty()) {
                resumed = parseParametricLine(pstate.parametricLine, &restored);
            }
            if (resumed && !pstate.dynamicsLine.empty()) {
                resumed = parseDynamicsLine(pstate.dynamicsLine, &restored);
            }
            if (resumed) chainParams = restored;
        } else {
            for (const auto& bp : builtinPresets()) {
                if (bp.id == pstate.presetId) {
                    chainParams = bp.params;
                    resumed = true;
                    break;
                }
            }
        }
        if (resumed) {
            std::snprintf(currentPreset, sizeof(currentPreset), "%s",
                          pstate.presetId.c_str());
            std::fprintf(stderr, "[engine] state: resumed preset '%s'\n",
                         currentPreset);
        }
    }
    EngineSoundState sound(chainParams, currentPreset);

    EngineContext ctx;
    ctx.useSystemBedRenderer = options.systemBedRenderer;
    ctx.volume.setBoost((float)clampVolumeBoost(pstate.volumeBoost));
    g_ctx = &ctx;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGUSR1, handleSigUsr1);

    ServicePort servicePort;
    if (!servicePort.acquire(ROOMCUT_MACH_SERVICE_NAME)) {
        std::fprintf(stderr, "[engine] could not acquire service port; exiting\n");
        return 1;
    }
    const mach_port_t service = servicePort.port();
    ctx.lifecycle.store(engineNext(ROOMCUT_ENGINE_STARTING, EngineEvent::ServicePublished),
                        std::memory_order_relaxed);
    std::fprintf(stderr, "[engine] service '%s' up; waiting for driver HELLO\n",
                 ROOMCUT_MACH_SERVICE_NAME);

    EngineControlLoop loop(ctx, sound, stateStore, pstate, dumpPath, service);
    loop.run();
    loop.shutdown();

    servicePort.release();
    return 0;
}
