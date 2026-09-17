/*
 * EngineRuntime.hpp — the engine's shared runtime: the state the control thread
 * and the render thread both touch, the CoreAudio render callback that walks it,
 * and the small helpers around them.
 *
 * Split out of main.cpp, which had grown to a thousand lines holding three
 * separate things at once: this runtime, the process's start-up, and the control
 * message pump. Only the first of those is shared with anything else.
 *
 * The one rule that matters here: everything the render callback reads from the
 * control thread goes through an atomic or a lock-free mailbox. Nothing on this
 * path allocates, locks, or blocks.
 */
#ifndef ROOMCUT_ENGINE_RUNTIME_HPP
#define ROOMCUT_ENGINE_RUNTIME_HPP

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
#include "SpatialMixerBedRenderer.hpp"
#include "OutputDevice.hpp"
#include "OutputRecovery.hpp"
#include "DriverFeedWatchdog.hpp"
#include "OutputRouter.hpp"
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
    // One AUSpatialMixer per A/B chain, opened with the output (control thread,
    // IO stopped) unless --bed-renderer builtin turned it off.
    bool                   useSystemBedRenderer = true;
    SpatialMixerBedRenderer bedCurrent;
    SpatialMixerBedRenderer bedReference;
    bool                   bedAttached = false;          // control thread
    std::atomic<uint32_t>  bedExternalGainBits{0};       // float bits, render → control
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
    // Live head orientation (float bits, so the atomic stays lock-free) and
    // whether the tracker is still delivering. Control thread writes, render
    // thread reads; never persisted.
    std::atomic<uint32_t>  headYawBits{0};
    std::atomic<bool>      headPoseActive{false};
    // Listening-test burst (PROBE_CHANNEL). The control thread writes the three
    // values, then bumps the generation; the render thread starts the burst when
    // the generation moves. probeSeen is render-thread only.
    std::atomic<int32_t>   probeChannel{-1};
    std::atomic<uint32_t>  probeSecondsBits{0};   // float bits
    std::atomic<uint32_t>  probeLevelBits{0};     // float bits
    std::atomic<uint32_t>  probeGeneration{0};
    uint32_t               probeSeen = 0;
    RealtimeMailbox<ComparisonMetrics> comparisonMeters; // render → control
    ComparisonMetrics comparisonSnapshot;        // control-owned last received metrics
    std::atomic<uint32_t>  limiterGRBits{0};       // float bits, render → control

    // Latency the engine adds on purpose, in ms. Written by the control thread
    // when the render pipeline is prepared; read by the same thread for replies.
    double                 engineLatencyMs = 0;

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
    // Head orientation is read straight off an atomic each block: it changes
    // tens of times a second and must never take the parameter path, which
    // crossfades the whole chain on every change.
    {
        const uint32_t yawBits = ctx->headYawBits.load(std::memory_order_relaxed);
        float yaw = 0.0f;
        std::memcpy(&yaw, &yawBits, sizeof(yaw));
        ctx->render.setHeadPose(yaw, ctx->headPoseActive.load(std::memory_order_relaxed));
    }
    if (const uint32_t generation = ctx->probeGeneration.load(std::memory_order_relaxed); generation != ctx->probeSeen) {
        ctx->probeSeen = generation;
        const uint32_t secondsBits = ctx->probeSecondsBits.load(std::memory_order_relaxed);
        const uint32_t levelBits = ctx->probeLevelBits.load(std::memory_order_relaxed);
        float seconds = 0.0f, level = 0.0f;
        std::memcpy(&seconds, &secondsBits, sizeof(seconds));
        std::memcpy(&level, &levelBits, sizeof(level));
        ctx->render.startChannelProbe(ctx->probeChannel.load(std::memory_order_relaxed), seconds, level);
    }
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
    std::memcpy(&bits, &metrics.bedExternalGain, sizeof(bits));
    ctx->bedExternalGainBits.store(bits, std::memory_order_relaxed);
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
    BedRenderer* bedCurrent = nullptr;
    BedRenderer* bedReference = nullptr;
    ctx.bedAttached = false;
    ctx.bedExternalGainBits.store(0, std::memory_order_relaxed);
    if (ctx.useSystemBedRenderer) {
        std::string bedError;
        if (ctx.bedCurrent.prepare(output.sampleRate(), bedError) && ctx.bedReference.prepare(output.sampleRate(), bedError)) {
            bedCurrent = &ctx.bedCurrent;
            bedReference = &ctx.bedReference;
            ctx.bedAttached = true;
            std::fprintf(stderr, "[engine] bed renderer: AUSpatialMixer at %.0f Hz, units at %.0f Hz (+%zu frames), personalized HRTF %s\n",
                         output.sampleRate(), ctx.bedCurrent.unitRate(),
                         ctx.bedCurrent.blockFrames() + ctx.bedCurrent.latencyFrames(),
                         ctx.bedCurrent.personalizedHrtfInUse() || ctx.bedReference.personalizedHrtfInUse() ? "in use" : "not in use");
        } else {
            ctx.bedCurrent.release();
            ctx.bedReference.release();
            std::fprintf(stderr, "[engine] bed renderer: built-in (%s)\n", bedError.c_str());
        }
    } else {
        std::fprintf(stderr, "[engine] bed renderer: built-in (--bed-renderer builtin)\n");
    }
    ctx.render.attachBedRenderers(bedCurrent, bedReference);
    try {
        ctx.render.prepare((double)ringSR, output.sampleRate(), params);
        ctx.analysis.prepare((uint32_t)std::lround(output.sampleRate()));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[engine] cannot prepare output processing: %s\n", error.what());
        output.close();
        return kAudioHardwareUnspecifiedError;
    }
    ctx.engineLatencyMs = ctx.render.latencySeconds() * 1000.0;
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

#endif // ROOMCUT_ENGINE_RUNTIME_HPP
