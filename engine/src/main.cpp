/*
 * RoomcutAudioEngine startup and control/routing coordination.
 *
 * The HAL driver hands over audio through the shared ring and Mach service.
 * RenderPipeline processes samples, PublishedRing guards mapping lifetime,
 * AnalysisWorker supplies metering snapshots, and EngineStateStore owns disk
 * persistence. OutputDevice admits only its current callback generation and
 * quiesces it before any render state is reconfigured or destroyed.
 */
#include "Control.hpp"
#include "ControlValidation.hpp"
#include "EngineSoundState.hpp"
#include "SoundCommands.hpp"
#include "EngineReplies.hpp"
#include "DeviceSelection.hpp"
#include "Handshake.hpp"
#include "Heartbeat.hpp"
#include "Lifecycle.hpp"
#include "RenderPipeline.hpp"
#include "AnalysisWorker.hpp"
#include "EngineStateStore.hpp"
#include "PublishedRing.hpp"
#include "RealtimeParams.hpp"
#include "OutputDevice.hpp"
#include "OutputRecovery.hpp"
#include "DriverFeedWatchdog.hpp"
#include "EngineDiagnostics.hpp"
#include "ServicePort.hpp"
#include "RenderProgressWatchdog.hpp"
#include "DeviceWatcher.hpp"
#include "VolumeController.hpp"
#include "RingRegion.hpp"

#include "dsp/DSPChain.hpp"
#include "presets/BuiltinPresets.hpp"
#include "presets/PresetValidator.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>   // _mm_getcsr / _mm_setcsr for FTZ/DAZ
#endif


#include <mach/mach.h>
#include <unistd.h>

#include <bootstrap.h>

extern "C" {
#include "roomcut_handshake.h"
#include "roomcut_ring.h"
}

using namespace roomcut;

namespace {

// Receive buffer big enough for any service message plus its trailer.
union RxBuffer {
    RoomcutHelloMsgBuffer   hello;
    RoomcutHealthMsgBuffer  health;
    RoomcutControlMsgBuffer control;
};

// Shared between the control (main) thread and the render thread. The control
// thread creates the region on HELLO and publishes its header here; the render
// callback borrows it through PublishedRing. Retirement waits for that borrow
// before reclaiming a mapping. DSP state is quiesced by OutputDevice on close.
struct EngineContext {
    std::atomic<bool>               running{true};
    PublishedRing                  ring;
    std::atomic<RoomcutEngineLifecycle> lifecycle{ROOMCUT_ENGINE_STARTING};

    RenderPipeline         render;              // render-thread only after prepare()
    std::atomic<uint32_t>  renderPeakBits{0};   // float bits of the last block's peak
    std::atomic<uint64_t>  framesRendered{0};
    std::atomic<uint64_t>  renderUnderruns{0};  // output pulled more than the ring had
    VolumeController      volume;              // serialized hardware control + atomic render gain

    // Phase 5 bypass (docs/05-recovery). Manual bypass crossfades via
    // DSPChain::setBypass, applied ON the render thread so the chain is only
    // ever touched from one thread. Safe bypass is a hard latch: the render
    // thread detected non-finite DSP output and skips the tonal stages from
    // then on. Final gain and limiting remain active on the clean dry signal.
    std::atomic<bool>      bypassRequested{false}; // control → render (manual)
    std::atomic<bool>      safeBypass{false};      // render → control (latched)

    RealtimeParams         params;               // control → render, owned snapshots
    RealtimeMailbox<ComparisonMetrics> comparisonMeters; // render → control
    ComparisonMetrics comparisonSnapshot;        // control-owned last received metrics
    std::atomic<uint32_t>  limiterGRBits{0};       // float bits, render → control

    // --dump diagnostic: the control thread allocates before output.start();
    // the render thread is the only writer of the contents + frame count.
    std::vector<float>     dumpBuf;
    uint32_t               dumpCapFrames = 0;
    std::atomic<uint32_t>  dumpFrames{0};

    AnalysisWorker         analysis;
};

EngineContext* g_ctx = nullptr;

void handleSignal(int) {
    if (g_ctx) {
        g_ctx->running.store(false, std::memory_order_relaxed);
    }
}

// SIGUSR1 toggles manual bypass (dev control until the app's XPC lands in
// Phase 6). Async-signal-safe: bump a counter; the control loop translates.
volatile sig_atomic_t g_bypassToggles = 0;
void handleSigUsr1(int) { g_bypassToggles = g_bypassToggles + 1; }

// Flush subnormals (denormals) to zero on the calling (render) thread. The IIR
// filter state in the EQ / parametric / spatial / compressor / limiter decays
// into the subnormal range during silence and quiet passages; subnormal
// arithmetic runs 10-100x slower, which is exactly what spikes the engine's CPU.
// No thread_local storage: a new HAL thread must not trigger lazy TLS allocation.
static inline void enableFlushToZero() {
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    if ((fpcr & (1ull << 24)) == 0) {
        __asm__ volatile("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
    }
#elif defined(__x86_64__) || defined(__i386__)
    const unsigned int csr = _mm_getcsr();
    if ((csr & 0x8040u) != 0x8040u) _mm_setcsr(csr | 0x8040u);
#endif
}

// The render pipeline owns resampling and shortfall concealment. This adapter
// only reads the currently published shared ring.
uint32_t ringInput(void* vctx, float* dst, uint32_t inFrames, uint32_t channels) {
    auto* ctx = static_cast<EngineContext*>(vctx);
    auto lease = ctx->ring.borrow();
    RoomcutRingHeader* h = lease.get();
    return h != nullptr && h->channels == channels
        ? roomcut_ring_read(h, dst, inFrames) : 0;
}

// CoreAudio adapter: apply control updates, render, then publish meters/taps.
void pullRender(void* vctx, float* dst, uint32_t frames, uint32_t channels) {
    enableFlushToZero();
    auto* ctx = static_cast<EngineContext*>(vctx);
    if (channels != RenderPipeline::kChannels) {
        std::memset(dst, 0, (size_t)frames * channels * sizeof(float));
        return;
    }

    ComparisonSettings params;
    if (ctx->params.readLatest(params)) ctx->render.setComparison(params);
    ctx->render.setBypass(ctx->bypassRequested.load(std::memory_order_relaxed));
    const RenderMetrics metrics = ctx->render.render(
        dst, frames, &ringInput, ctx, ctx->volume.renderGain());
    ctx->safeBypass.store(metrics.safeBypass, std::memory_order_relaxed);
    uint32_t bits;
    std::memcpy(&bits, &metrics.peak, sizeof(bits));
    ctx->renderPeakBits.store(bits, std::memory_order_relaxed);
    std::memcpy(&bits, &metrics.limiterReductionDb, sizeof(bits));
    ctx->limiterGRBits.store(bits, std::memory_order_relaxed);
    ctx->framesRendered.fetch_add(frames, std::memory_order_relaxed);
    ctx->renderUnderruns.fetch_add(metrics.shortfallFrames, std::memory_order_relaxed);
    ctx->comparisonMeters.publish(ctx->render.comparisonMetrics());

    ctx->analysis.push(dst, frames);

    // --dump tap: copy the post-DSP block (exactly what reaches the hardware)
    // into the preallocated capture buffer. RT-safe: memcpy + relaxed counter.
    if (ctx->dumpCapFrames != 0) {
        uint32_t at = ctx->dumpFrames.load(std::memory_order_relaxed);
        if (at < ctx->dumpCapFrames) {
            uint32_t n = frames;
            if (at + n > ctx->dumpCapFrames) n = ctx->dumpCapFrames - at;
            std::memcpy(ctx->dumpBuf.data() + (size_t)at * channels, dst,
                        (size_t)n * channels * sizeof(float));
            ctx->dumpFrames.store(at + n, std::memory_order_relaxed);
        }
    }
}

// Open the output on `device` and prepare the DSP chain + resampler for its
// rate. Control thread only, with the render callback NOT running (called
// before start() / after stop()).
// Fill `out` (up to `cap`) with the standard nominal rates the device supports,
// low→high. Used to tell the driver which rates the REAL output can run at, so
// it advertises only those and coreaudiod picks a passthrough-friendly rate.
uint32_t deviceAvailableSampleRates(AudioDeviceID dev, uint32_t* out, uint32_t cap) {
    if (dev == kAudioObjectUnknown || out == nullptr || cap == 0) return 0;
    AudioObjectPropertyAddress addr{kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &addr, 0, nullptr, &size) != noErr || size == 0) {
        return 0;
    }
    std::vector<AudioValueRange> ranges(size / sizeof(AudioValueRange));
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, ranges.data()) != noErr) {
        return 0;
    }
    ranges.resize(size / sizeof(AudioValueRange));
    // Recognize the device's ACTUAL rates — no hardcoded ceiling. Discrete rates
    // (min == max) are taken verbatim, whatever the device reports (44.1k …
    // 768k). The reference list is only used to enumerate a CONTINUOUS range
    // (the rare device that advertises a min/max band instead of points).
    static const uint32_t kExpand[] = {
        ROOMCUT_SR_44100, ROOMCUT_SR_48000, ROOMCUT_SR_88200, ROOMCUT_SR_96000,
        ROOMCUT_SR_176400, ROOMCUT_SR_192000, 352800u, 384000u, 705600u, 768000u};
    uint32_t n = 0;
    auto append = [&](uint32_t sr) {
        if (sr == 0u || n >= cap) return;
        for (uint32_t i = 0; i < n; ++i) if (out[i] == sr) return; // dedupe
        out[n++] = sr;
    };
    for (const auto& r : ranges) {
        if (n >= cap) break;
        if (std::fabs(r.mMaximum - r.mMinimum) < 1.0) {
            append((uint32_t)std::llround(r.mMinimum));            // discrete → verbatim
        } else {
            for (uint32_t sr : kExpand) {                          // continuous → expand
                if ((double)sr >= r.mMinimum - 1.0 && (double)sr <= r.mMaximum + 1.0) append(sr);
            }
        }
    }
    std::sort(out, out + n);
    return n;
}

OSStatus openOutputOn(EngineContext& ctx, OutputDevice& output,
                      AudioDeviceID device, uint32_t ringSR,
                      const ComparisonSettings& params, bool matchRingRate = true) {
    // Ask the device to run at the ring rate so the render path is bit-exact
    // (ratio 1.0) rather than resampled — the fix for the soft,
    // low-resolution sound vs. listening to the device directly. With
    // matchRingRate=false we open the device wherever it already is: some devices
    // accept the rate change and then fall back to their own, and insisting on
    // every reopen just restarts that fight. Resampling to a stable device beats
    // bit-exact to one that keeps moving.
    OSStatus err = output.open(&pullRender, &ctx, ROOMCUT_MVP_CHANNELS, device,
                               matchRingRate ? (double)ringSR : 0.0);
    if (err != noErr) return err;

    // Allocate and reset all sample-path state before starting the callback.
    ctx.comparisonMeters.readLatest(ctx.comparisonSnapshot);
    ctx.comparisonSnapshot = {};
    try {
        ctx.render.prepare((double)ringSR, output.sampleRate(), params);
        ctx.analysis.prepare((uint32_t)std::lround(output.sampleRate()));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[engine] cannot prepare output processing: %s\n", error.what());
        output.close();
        return kAudioHardwareUnspecifiedError;
    }
    if (ctx.render.ratio() != 1.0) {
        std::fprintf(stderr,
            "[engine] device sr=%.2f != ring sr=%u; band-limited resampling ring->device (ratio=%.4f, delay=%.2fms)\n",
            output.sampleRate(), ringSR, ctx.render.ratio(), ctx.render.resamplerLatencySeconds() * 1000.0);
    } else {
        std::fprintf(stderr,
            "[engine] device sr=%u == ring sr; bit-exact passthrough (no resampling)\n", ringSR);
    }
    return noErr;
}

// If the system default output is Roomcut's own device, point it back at a
// real device (saved real → builtin → any). Returns true if it flipped the
// default. Used by startup recovery (driver never came) and clean shutdown.
bool restoreDefaultIfRoomcut(const std::string& savedRealUID) {
    AudioDeviceID def = defaultOutputDevice();
    if (def == kAudioObjectUnknown || !isRoomcutDeviceUID(deviceUID(def))) {
        return false;
    }
    AudioDeviceID pick = pickRenderDevice(listOutputDevices(),
                                          kAudioObjectUnknown, savedRealUID);
    if (pick == kAudioObjectUnknown) {
        std::fprintf(stderr, "[engine] restore: no real output device available\n");
        return false;
    }
    OSStatus err = setDefaultOutputDevice(pick);
    if (err != noErr) {
        std::fprintf(stderr, "[engine] restore: set default failed: %d\n", (int)err);
        return false;
    }
    std::fprintf(stderr, "[engine] restored system default output -> '%s'\n",
                 deviceName(pick).c_str());
    return true;
}

// Acquire the receive right for ROOMCUT_MACH_SERVICE_NAME. Returns
// MACH_PORT_NULL on failure. `outRegistered` is set true if we had to register
// the name ourselves (dev path) vs. checking in with launchd (production path).
// Resolve the Roomcut Output device we own (re-resolved on device-world changes;
// the id is cached for cheap per-tick volume reads).
AudioDeviceID findRoomcutDevice() {
    for (const auto& d : listOutputDevices()) {
        if (isRoomcutDeviceUID(d.uid)) return d.id;
    }
    return kAudioObjectUnknown;
}

} // namespace

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

    // The region + output device must outlive the render callback; owned here.
    RingRegion    region;
    OutputDevice  output;
    DeviceWatcher watcher;
    uint32_t      dumpSR = 0;  // device SR at capture time (output.close() zeroes it)
    uint32_t      ringSR = 0;  // negotiated ring SR (0 until first HELLO)
    std::string&  savedRealUID = pstate.realOutputUID; // survives engine death
    std::string&  preferredOutputUID = pstate.preferredOutputUID; // user-pinned device
    bool&         keepRoomcutDefault = pstate.keepRoomcutDefault;  // reclaim default toggle

    // Resolve the render target: a user-pinned device (if present and not our own
    // virtual device) wins; otherwise the automatic policy. Keeps the manual
    // override in one place for both the HELLO open and the recovery reopen.
    auto pickOutput = [&]() -> AudioDeviceID {
        auto devs = listOutputDevices();
        if (!preferredOutputUID.empty()) {
            for (const auto& d : devs) {
                if (d.uid == preferredOutputUID && !isRoomcutDeviceUID(d.uid)) {
                    return d.id;
                }
            }
            // Pinned device is gone — fall through to policy until it returns.
        }
        return pickRenderDevice(devs, defaultOutputDevice(), savedRealUID);
    };

    // Persist the real device write-through, so a crashed engine's successor
    // knows where to point the default.
    auto setSavedReal = [&](const std::string& uid) {
        if (uid.empty() || isRoomcutDeviceUID(uid) || uid == savedRealUID) return;
        savedRealUID = uid;
        stateStore.save(pstate);
    };

    // The render-target policy can only route back to "the device the user was
    // using before Roomcut" if someone recorded it. Record it here: whenever
    // the system default IS a real device, persist it — so the moment the user
    // flips the default to Roomcut, the state file already names the right
    // target. Without this, a fresh engine whose default is already Roomcut
    // had to guess and fell back to AirPods → built-in speakers (the
    // 2026-06-12 "quiet, from the right" bug: rendering to the Mac mini's
    // internal speaker instead of the user's DAC).
    auto trackRealDefault = [&]() {
        AudioDeviceID def = defaultOutputDevice();
        if (def == kAudioObjectUnknown) return;
        std::string uid = deviceUID(def);
        if (!uid.empty() && !isRoomcutDeviceUID(uid)) {
            setSavedReal(uid);
            // "Keep Roomcut as output": macOS auto-switched the default to a
            // real device (e.g. AirPods connected). Reclaim Roomcut as default
            // and route through the device that just took over — but only when
            // Roomcut is genuinely a live path (driver handed off a region),
            // never at startup. setSavedReal above already recorded the target;
            // recoverOutput (run right after this in the dirty handler) reopens
            // onto it. Setting the default back to Roomcut yields default ==
            // Roomcut next pass, so this never loops.
            if (keepRoomcutDefault && region.valid()) {
                AudioDeviceID rcDev = findRoomcutDevice();
                if (rcDev != kAudioObjectUnknown) {
                    setDefaultOutputDevice(rcDev);
                    std::fprintf(stderr,
                        "[engine] keep-default: reclaimed Roomcut (default had moved to '%s')\n",
                        uid.c_str());
                }
            }
        }
    };
    trackRealDefault();

    // Publish immutable snapshots; the render side owns its current slot.
    ctx.params.publish(sound.settings());
    auto persistSound = [&] {
        ctx.params.publish(sound.settings());
        capturePersistentSound(pstate, sound.parameters(), sound.presetID());
        if (!stateStore.save(pstate))
            std::fprintf(stderr, "[engine] sound state save failed; current session remains active\n");
    };

    watcher.install();
    ctx.analysis.start();

    // Startup recovery (docs/05-recovery): if we come up with Roomcut as the
    // system default (typical after a crash — launchd respawned us under the
    // user's last selection) and the driver does not hand us audio within the
    // window, point the default back at a real device so sound returns. A
    // HELLO disarms this: when the driver is alive, Roomcut-as-default is
    // exactly the healthy production state.
    int  lastBypassToggles = 0;
    bool safeBypassLogged  = false;
    DriverFeedWatchdog driverFeed(std::chrono::steady_clock::now());
    AudioDeviceID roomcutDev  = findRoomcutDevice(); // for system-volume mirroring
    ctx.volume.setSource(roomcutDev);
    bool restoreArmed = false;
    auto restoreDeadline = std::chrono::steady_clock::now();
    if (isRoomcutDeviceUID(deviceUID(defaultOutputDevice()))) {
        restoreArmed = true;
        restoreDeadline += std::chrono::seconds(3);
        std::fprintf(stderr,
            "[engine] startup: default output is Roomcut and no driver yet; "
            "restoring a real device in 3 s unless the driver connects\n");
    }

    OutputRecovery recovery;
    RenderProgressWatchdog renderWatchdog(std::chrono::steady_clock::now());

    // Recovery policy owns retry timing; this bridge orders lifecycle, HAL,
    // render preparation, watcher and persisted-device updates on the control thread.
    auto recoverOutput = [&](bool force = false) {
        if (!region.valid() || ringSR == 0) { recovery.reset(); return; }
        const AudioDeviceID pick = pickOutput();
        const bool sameDevice = pick == output.deviceID();
        const auto decision = recovery.decide({pick, ringSR, output.deviceID(), output.running(),
            output.sampleRate(), sameDevice && output.running() ? output.currentHardwareRate() : 0.0},
            std::chrono::steady_clock::now(), force);
        if (!decision.reopen) return;
        std::fprintf(stderr, "[engine] device change: reopening output (%s)\n",
                     force ? "render runaway"
                           : (sameDevice ? "sample rate changed" : "target device changed"));
        ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputLost),
                            std::memory_order_relaxed);
        output.stop();
        watcher.watchSR(kAudioObjectUnknown);
        output.close();

        OSStatus oerr = openOutputOn(ctx, output, pick, ringSR, sound.settings(),
                                     decision.matchRingRate);
        if (oerr == noErr) oerr = output.start();
        const auto completedAt = std::chrono::steady_clock::now();
        recovery.completed(completedAt);
        if (oerr != noErr) {
            std::fprintf(stderr,
                "[engine] output reopen failed: %d (retry scheduled)\n", (int)oerr);
            output.close();
            return;
        }
        renderWatchdog.opened(output.deviceID(), completedAt);
        watcher.watchSR(output.deviceID());
        setSavedReal(deviceUID(output.deviceID()));
        ctx.volume.setTarget(output.deviceID());
        // While the driver feed is stalled the output alone doesn't make us
        // streaming; the watchdog's DriverReturned promotes when it resumes.
        if (!driverFeed.stalled()) {
            ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputReopened),
                                std::memory_order_relaxed);
        }
        std::fprintf(stderr, "[engine] output -> '%s' @ %.0f Hz\n",
                     deviceName(output.deviceID()).c_str(), output.sampleRate());
    };

    while (ctx.running.load(std::memory_order_relaxed)) {
        RxBuffer rx;
        std::memset(&rx, 0, sizeof(rx));

        // Receive with a timeout so the signal-driven `running` flag is polled.
        kern_return_t kr = mach_msg(&rx.hello.request.header,
                                    MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                    0, sizeof(rx), service,
                                    500 /* ms */, MACH_PORT_NULL);

        // Device-world changes are handled here, between messages, so all
        // output/DSP mutation stays on this one control thread.
        const bool devicesChanged = watcher.takeChanges();
        if (devicesChanged) trackRealDefault();
        if (devicesChanged || recovery.retryDue(std::chrono::steady_clock::now())) recoverOutput();
        if (devicesChanged) {
            AudioDeviceID rc = findRoomcutDevice(); // id may change on driver reload
            if (rc != roomcutDev) {
                ctx.volume.setSource(rc);
                roomcutDev = rc;
            }
            ctx.volume.refresh(); // output may have changed → re-apply
        }

        // Retry missing listeners and recover missed volume notifications.
        watcher.install();
        if (output.running()) watcher.watchSR(output.deviceID());
        ctx.volume.poll();

        // Startup-recovery deadline: driver never connected → free the user's
        // audio from the dead route.
        if (restoreArmed && std::chrono::steady_clock::now() >= restoreDeadline) {
            restoreArmed = false;
            restoreDefaultIfRoomcut(savedRealUID);
        }

        // Manual bypass toggle (SIGUSR1) → render thread applies it.
        if (int t = g_bypassToggles; t != lastBypassToggles) {
            lastBypassToggles = t;
            bool nb = !ctx.bypassRequested.load(std::memory_order_relaxed);
            ctx.bypassRequested.store(nb, std::memory_order_relaxed);
            std::fprintf(stderr, "[engine] manual bypass %s\n", nb ? "ON" : "OFF");
        }
        // Safe-bypass latch is set on the render thread; log it once here.
        if (!safeBypassLogged && ctx.safeBypass.load(std::memory_order_relaxed)) {
            safeBypassLogged = true;
            std::fprintf(stderr, "[engine] SAFE BYPASS latched: DSP produced "
                                 "non-finite output; passthrough until restart\n");
        }

        // Driver-stall watchdog (docs/05: "the engine monitors the driver
        // connection"): the driver's transport worker heartbeats ~1/s even
        // while IO is idle, so a frozen writeIndex alone just means silence
        // (nothing playing / default routed elsewhere). Declare the driver
        // lost only when the feed is frozen AND the heartbeat went quiet;
        // announce the return when the feed advances again.
        if (RoomcutRingHeader* h = ctx.ring.current()) {
            const uint64_t w = __atomic_load_n(&h->writeIndex, __ATOMIC_ACQUIRE);
            switch (driverFeed.observe(w, std::chrono::steady_clock::now())) {
                case DriverFeedWatchdog::Event::Returned:
                    if (output.running()) {
                        ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::DriverReturned),
                                            std::memory_order_relaxed);
                        std::fprintf(stderr, "[engine] driver feed resumed\n");
                    } else {
                        // Feed is back but the output is still down: stay in
                        // RECOVERING; recoverOutput's OutputReopened promotes
                        // to STREAMING once the device is actually live.
                        std::fprintf(stderr,
                            "[engine] driver feed resumed; waiting for output reopen\n");
                    }
                    break;
                case DriverFeedWatchdog::Event::Lost:
                    ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::DriverLost),
                                        std::memory_order_relaxed);
                    std::fprintf(stderr, "[engine] driver feed stalled and heartbeat lost; "
                                         "rendering silence (wire: RECOVER)\n");
                    break;
                case DriverFeedWatchdog::Event::None:
                    break;
            }
        }

        // Render-runaway watchdog. The output unit pulls us at its client stream
        // format's rate, so framesRendered must advance at exactly
        // output.sampleRate() frames/s — whatever is playing, silence included.
        // A boot-time device re-rate can leave the render path pulling several
        // times real time (2026-08-01: iFi opened at 48 kHz, reopened at 384 kHz
        // moments later, then ran at 3.37x with the ring draining and underruns
        // climbing ~128k/s — the dropout warble). Nothing else caught it:
        // recoverOutput compares device+rate, both of which look unchanged, so
        // the engine stayed broken until the user restarted it by hand. Rebuild
        // the unit instead — a fresh AU comes back at 1.00x.
        if (output.running() && ringSR != 0) {
            const auto repair = renderWatchdog.observe(ctx.framesRendered.load(std::memory_order_relaxed),
                output.sampleRate(), std::chrono::steady_clock::now());
            if (repair.requested) {
                std::fprintf(stderr,
                    "[engine] render runaway: %.0f frames/s vs device %.0f Hz; rebuilding output (attempt %u/3)\n",
                    repair.framesPerSecond, output.sampleRate(), repair.attempt);
                recoverOutput(true);
            }
        }

        if (kr == MACH_RCV_TIMED_OUT) {
            continue;
        }
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] recv error: %d\n", kr);
            continue;
        }

        const auto messageID = rx.hello.request.header.msgh_id;
        if (messageID != ROOMCUT_MSG_HELLO && messageID != ROOMCUT_MSG_HEALTH_CHECK
            && !normalizeControlRequest(rx.control)) {
            std::fprintf(stderr, "[engine] malformed control message id=%d size=%u\n",
                         messageID, rx.control.raw.header.msgh_size);
            mach_msg_destroy(&rx.control.raw.header);
            continue;
        }
        switch (messageID) {
            case ROOMCUT_MSG_HELLO: {
                const RoomcutHelloRequest& req = rx.hello.request;
                RoomcutFormatNegotiation granted{};
                if (!engineNegotiateHello(req, granted)) {
                    mach_msg_destroy(&rx.hello.request.header);
                    break;
                }
                std::fprintf(stderr, "[engine] HELLO v%u req sr=%u ch=%u cap=%u\n",
                             req.protocolVersion, req.requested.sampleRate,
                             req.requested.channels, req.requested.capacityFrames);

                ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::HelloReceived),
                                    std::memory_order_relaxed);
                driverFeed.heartbeat(std::chrono::steady_clock::now());
                restoreArmed = false; // driver is alive — Roomcut-as-default is healthy

                const uint32_t sr = granted.sampleRate;
                const uint32_t cap = granted.capacityFrames;

                if (region.valid()) {
                    // Detach the old ring and wait for its actual borrower,
                    // rather than estimating callback lifetime with a delay.
                    std::fprintf(stderr, "[engine] reconnect: retiring previous ring region\n");
                    ctx.ring.clear();
                    region.destroy();
                }
                if (output.running() && sr != ringSR) {
                    // Retire once; the common open/start path below prepares
                    // the new rate and handles any failure through normal retry.
                    output.stop();
                    watcher.watchSR(kAudioObjectUnknown);
                    output.close();
                }

                if (region.create(cap, ROOMCUT_MVP_CHANNELS, sr) != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] region.create failed\n");
                    mach_msg_destroy(&rx.hello.request.header);
                    break;
                }

                // Open (but don't start) the real output device BEFORE replying:
                // the producer starts writing the moment the reply lands, and
                // AudioUnit open/initialize is slow enough (~100ms+) that an
                // after-reply open let the 4096-frame ring fill and drop the
                // first chunks (observed: dropped=2560 in the first audible
                // test). Prepare the DSP chain at the DEVICE rate (what we
                // actually output); a device/ring SR mismatch is handled by the
                // band-limited resampler (ring SR -> device SR).
                ringSR = sr;
                bool outputReady = output.running();
                if (!output.running()) {
                    // Phase 5: never render to Roomcut's own device — a pinned
                    // device wins, else the policy target (real default → saved
                    // real → builtin → any).
                    AudioDeviceID pick = pickOutput();
                    OSStatus oerr = (pick == kAudioObjectUnknown)
                        ? (OSStatus)kAudioHardwareBadDeviceError
                        : openOutputOn(ctx, output, pick, sr, sound.settings());
                    if (oerr != noErr) {
                        std::fprintf(stderr, "[engine] output.open failed: %d (transfer ok, no audible out)\n",
                                     (int)oerr);
                        watcher.markChanged();
                    } else {
                        if (dumpPath != nullptr && ctx.dumpCapFrames == 0) {
                            dumpSR = (uint32_t)output.sampleRate();
                            ctx.dumpBuf.assign((size_t)kDumpMaxSeconds * dumpSR
                                               * ROOMCUT_MVP_CHANNELS, 0.0f);
                            ctx.dumpCapFrames = kDumpMaxSeconds * dumpSR;
                        }
                        outputReady = true;
                    }
                }

                // Publish the region and start rendering BEFORE replying, then
                // wait (bounded) for the first real render pull. The reply is
                // the producer's green light: if it lands before the consumer
                // is actually draining, the producer fills the ring and splices
                // its head (observed dropped=2048 even with the device
                // pre-opened — AudioOutputUnitStart's first callback lags by
                // tens of ms).
                ctx.ring.publish(region.header());
                bool startedNow = false;
                const uint64_t framesBeforeStart = ctx.framesRendered.load(std::memory_order_relaxed);
                if (outputReady && !output.running()) {
                    OSStatus oerr = output.start();
                    if (oerr != noErr) {
                        std::fprintf(stderr, "[engine] output.start failed: %d\n", (int)oerr);
                        watcher.markChanged();
                    } else {
                        startedNow = true;
                    }
                }
                if (output.running()) {
                    bool renderLive = false;
                    for (int i = 0; i < 200; ++i) {                    // <= 400 ms
                        if (ctx.framesRendered.load(std::memory_order_relaxed) > framesBeforeStart) {
                            renderLive = true;
                            break;
                        }
                        usleep(2000);
                    }
                    if (!renderLive) {
                        std::fprintf(stderr,
                            "[engine] render callback not live after 400ms; replying anyway\n");
                    }
                }

                // Tell the driver which rates the REAL output device supports so
                // it can advertise exactly those — coreaudiod then settles on a
                // rate the device runs natively and the render path is a
                // passthrough (no muffling from a mismatched ring rate).
                uint32_t devRates[ROOMCUT_MAX_RATES];
                AudioDeviceID outDev = output.running() ? output.deviceID() : pickOutput();
                uint32_t devRateCount =
                    deviceAvailableSampleRates(outDev, devRates, ROOMCUT_MAX_RATES);
                {
                    std::string rl;
                    for (uint32_t i = 0; i < devRateCount; ++i)
                        rl += (i ? "," : "") + std::to_string(devRates[i]);
                    std::fprintf(stderr, "[engine] HELLO reply: forwarding %u device rate(s) [%s]\n",
                                 devRateCount, rl.c_str());
                }

                kr = engineReplyHello(req, region, granted, devRates, devRateCount);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] engineReplyHello failed: %d\n", kr);
                    if (kr == KERN_INVALID_ARGUMENT) mach_msg_destroy(&rx.hello.request.header);
                    // Retirement waits for any outstanding ring borrow.
                    ctx.ring.clear();
                    region.destroy();
                    break;
                }

                ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::RegionCreated),
                                    std::memory_order_relaxed);
                std::fprintf(stderr, "[engine] handed off region (sr=%u cap=%u)\n", sr, cap);

                if (startedNow) {
                    recovery.reset();
                    renderWatchdog.opened(output.deviceID(), std::chrono::steady_clock::now());
                    watcher.watchSR(output.deviceID());
                    setSavedReal(deviceUID(output.deviceID()));
                    ctx.volume.setTarget(output.deviceID());
                }
                if (output.running()) {
                    // Promote on EVERY successful handoff, not only on startedNow:
                    // when the driver's first HELLO reply loses its timeout race
                    // and it retries, the output is already open, and skipping the
                    // promotions parked the lifecycle at BUFFER_MAPPED — a state
                    // with no OutputLost/OutputReopened exits, so the engine
                    // streamed audio forever while the wire reported STOPPED
                    // (menu-bar icon off, the app's default-output claim gated on
                    // RUNNING never fired). Both events are no-ops when already
                    // STREAMING.
                    ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputOpened),
                                        std::memory_order_relaxed);
                    // OUTPUT_READY → STREAMING (both region + output live).
                    ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputReopened),
                                        std::memory_order_relaxed);
                    std::fprintf(stderr, "[engine] output device open '%s' @ %.0f Hz, %u ch; STREAMING\n",
                                 deviceName(output.deviceID()).c_str(),
                                 output.sampleRate(), ROOMCUT_MVP_CHANNELS);
                }
                break;
            }

            case ROOMCUT_MSG_HEALTH_CHECK: {
                const RoomcutHealthRequest& hreq = rx.health.request;
                if (!roomcut_health_request_valid(&hreq)) {
                    mach_msg_destroy(&rx.health.request.header);
                    break;
                }
                driverFeed.heartbeat(std::chrono::steady_clock::now());
                const auto coarse = presentedEngineState(ctx.lifecycle.load(),
                    ctx.bypassRequested.load(std::memory_order_relaxed), ctx.safeBypass.load(std::memory_order_relaxed));
                // Carry the real output device's rates so the driver can correct
                // its nominal rate after a live device switch (which doesn't
                // re-HELLO) → keeps the ring at the device's native rate.
                uint32_t hbRates[ROOMCUT_MAX_RATES];
                AudioDeviceID hbDev = output.running() ? output.deviceID() : pickOutput();
                uint32_t hbRateCount = deviceAvailableSampleRates(hbDev, hbRates, ROOMCUT_MAX_RATES);
                kr = heartbeatRespond(hreq, static_cast<uint32_t>(coarse), hbRates, hbRateCount);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] heartbeatRespond failed: %d\n", kr);
                }
                // Piggyback a render progress line so the passthrough can be
                // observed headlessly (peak of the last rendered block, total
                // frames out, and any output-side underruns). Opt-in only
                // (ROOMCUT_LOG_RENDER) — it fires every heartbeat, so leaving it
                // on grows the daemon log without bound.
                static const bool kLogRender = std::getenv("ROOMCUT_LOG_RENDER") != nullptr;
                if (output.running() && kLogRender) {
                    uint32_t bits = ctx.renderPeakBits.load(std::memory_order_relaxed);
                    float peak;
                    std::memcpy(&peak, &bits, sizeof(peak));
                    std::fprintf(stderr,
                        "[engine] render: peak=%.5f framesOut=%llu underruns=%llu %s\n",
                        peak,
                        (unsigned long long)ctx.framesRendered.load(std::memory_order_relaxed),
                        (unsigned long long)ctx.renderUnderruns.load(std::memory_order_relaxed),
                        peak > 1e-5f ? "NON-SILENCE" : "silence");
                }
                break;
            }

            case ROOMCUT_MSG_SET_PRESET:
            case ROOMCUT_MSG_SET_PARAMS:
            case ROOMCUT_MSG_SET_COMPARISON: {
                const auto status = applySoundCommand(rx.control, sound);
                if (status == 0) persistSound();
                else std::fprintf(stderr, "[engine] sound command %d rejected: %u\n", messageID, status);
                kr = controlReplyAck(rx.control.raw.header, static_cast<uint32_t>(messageID), status);
                if (kr != KERN_SUCCESS)
                    std::fprintf(stderr, "[engine] sound command ack failed: %d\n", kr);
                break;
            }

            case ROOMCUT_MSG_SET_OUTPUT_DEV: {
                const RoomcutSetDeviceRequest& dreq = rx.control.setDevice;
                char uid[ROOMCUT_DEVICE_UID_MAX];
                std::memcpy(uid, dreq.deviceUID, sizeof(uid));
                uid[sizeof(uid) - 1] = '\0';
                preferredOutputUID = uid; // "" = back to automatic policy
                stateStore.save(pstate);
                std::fprintf(stderr, "[engine] output device pinned -> '%s'\n",
                             preferredOutputUID.empty() ? "(auto)" : preferredOutputUID.c_str());
                // Reopen on the new target via the control loop's recoverOutput.
                watcher.markChanged();
                kr = controlReplyAck(dreq.header, ROOMCUT_MSG_SET_OUTPUT_DEV, 0);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] set-device ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_SET_BYPASS: {
                const RoomcutSetBypassRequest& breq = rx.control.setBypass;
                const bool nb = breq.bypass != 0;
                ctx.bypassRequested.store(nb, std::memory_order_relaxed);
                std::fprintf(stderr, "[engine] manual bypass %s (ctl)\n", nb ? "ON" : "OFF");
                kr = controlReplyAck(breq.header, ROOMCUT_MSG_SET_BYPASS, 0);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] bypass ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_SET_KEEP_DEFAULT: {
                const RoomcutSetKeepDefaultRequest& kreq = rx.control.setKeepDefault;
                keepRoomcutDefault = kreq.on != 0;
                stateStore.save(pstate);
                std::fprintf(stderr, "[engine] keep-default %s (ctl)\n",
                             keepRoomcutDefault ? "ON" : "OFF");
                // Apply immediately: if it's now on and the default has already
                // drifted off Roomcut, reclaim on the next device tick.
                if (keepRoomcutDefault) watcher.markChanged();
                kr = controlReplyAck(kreq.header, ROOMCUT_MSG_SET_KEEP_DEFAULT, 0);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] keep-default ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_SET_VOLUME_BOOST: {
                const RoomcutSetVolumeBoostRequest& vreq = rx.control.setVolumeBoost;
                const double boost = clampVolumeBoost(vreq.boost);
                ctx.volume.setBoost((float)boost);
                pstate.volumeBoost = boost;
                stateStore.save(pstate);
                kr = controlReplyAck(vreq.header, ROOMCUT_MSG_SET_VOLUME_BOOST, 0);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] volume boost ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_GET_COMPARISON: {
                ctx.comparisonMeters.readLatest(ctx.comparisonSnapshot);
                const auto reply = makeComparisonReply(sound, ctx.comparisonSnapshot, output.running(),
                    ctx.bypassRequested.load(std::memory_order_relaxed));
                kr = controlReplyComparison(rx.control.getComparison, reply);
                break;
            }

            case ROOMCUT_MSG_STATE: {
                EngineStatusSnapshot snapshot;
                snapshot.lifecycle = ctx.lifecycle.load();
                snapshot.manualBypass = ctx.bypassRequested.load(std::memory_order_relaxed);
                snapshot.safeBypass = ctx.safeBypass.load(std::memory_order_relaxed);
                snapshot.outputRunning = output.running();
                const uint32_t grBits = ctx.limiterGRBits.load(std::memory_order_relaxed);
                const uint32_t pkBits = ctx.renderPeakBits.load(std::memory_order_relaxed);
                std::memcpy(&snapshot.limiterReductionDb, &grBits, sizeof(float));
                std::memcpy(&snapshot.renderPeak, &pkBits, sizeof(float));
                snapshot.framesRendered = ctx.framesRendered.load(std::memory_order_relaxed);
                snapshot.ringUnderruns = ctx.renderUnderruns.load(std::memory_order_relaxed);
                if (snapshot.outputRunning) snapshot.outputDeviceUID = deviceUID(output.deviceID());
                snapshot.keepDefault = keepRoomcutDefault;
                snapshot.volumeBoost = ctx.volume.boost();
                kr = controlReplyState(rx.control.stateRequest, makeStateReply(sound, snapshot));
                if (kr != KERN_SUCCESS)
                    std::fprintf(stderr, "[engine] state reply failed: %d\n", kr);
                break;
            }

            case ROOMCUT_MSG_GET_ANALYSIS: {
                kr = controlReplyAnalysis(rx.control.analysisRequest, makeAnalysisReply(ctx.analysis.snapshot()));
                if (kr != KERN_SUCCESS)
                    std::fprintf(stderr, "[engine] analysis reply failed: %d\n", kr);
                break;
            }

            case ROOMCUT_MSG_GET_PARAMS: {
                kr = controlReplyParams(rx.control.getParams, makeParamsReply(sound));
                if (kr != KERN_SUCCESS)
                    std::fprintf(stderr, "[engine] params reply failed: %d\n", kr);
                break;
            }

            default:
                std::fprintf(stderr, "[engine] unknown msgh_id %d\n",
                             rx.hello.request.header.msgh_id);
                // Drain any rights to avoid leaking them.
                mach_msg_destroy(&rx.hello.request.header);
                break;
        }
    }

    std::fprintf(stderr, "[engine] shutting down\n");
    ctx.running.store(false, std::memory_order_relaxed);
    ctx.analysis.stop();
    ctx.volume.stop();
    watcher.remove();
    // Stop the render callback BEFORE tearing down the ring it consumes.
    output.stop();
    output.close();
    ctx.ring.clear();

    if (dumpPath != nullptr) {
        const uint32_t got = ctx.dumpFrames.load(std::memory_order_relaxed);
        if (got > 0 && writeWavF32(dumpPath, ctx.dumpBuf.data(), got,
                                   ROOMCUT_MVP_CHANNELS, dumpSR)) {
            std::fprintf(stderr, "[engine] dump: wrote %u frames @ %u Hz to %s\n",
                         got, dumpSR, dumpPath);
        } else {
            std::fprintf(stderr, "[engine] dump: nothing captured (%s)\n", dumpPath);
        }
    }

    // Clean exit (user quit / uninstall via launchctl bootout → SIGTERM): we
    // are about to stop existing, so never leave the system pointed at the
    // soon-to-be-silent Roomcut device.
    restoreDefaultIfRoomcut(savedRealUID);

    servicePort.release();
    return 0;
}
