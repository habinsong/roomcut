/*
 * main.cpp — RoomcutAudioEngine entry point (Phase 4 passthrough milestone).
 *
 * This is the helper process the driver (inside coreaudiod) hands its audio to.
 * It owns the Mach service `com.roomcut.engine` and the shared ring region:
 *
 *   1. Acquire the service port. Production path: launchd holds the receive
 *      right (declared in the system launchd plist's MachServices) and we
 *      `bootstrap_check_in` to claim it. Dev path (no launchd): fall back to
 *      `bootstrap_register` on an allocated port so a manually-launched engine +
 *      driver-sim can rendezvous in the same bootstrap namespace.
 *   2. Receive loop on that one port; dispatch by msgh_id:
 *        ROOMCUT_MSG_HELLO        → create RingRegion, open the real output
 *                                   device, engineReplyHello (hands the driver
 *                                   the memory-entry send-right), start render.
 *        ROOMCUT_MSG_HEALTH_CHECK → heartbeatRespond with the coarse state.
 *   3. The output device's render callback is the SINGLE ring consumer: it
 *      drains the ring, zero-fills any shortfall, and runs the MVP DSP chain
 *      (Flat) before the samples reach the speakers. (No EQ params/UI yet — that
 *      is Phase 6; here the chain proves the wiring + the always-on limiter.)
 *
 * The lifecycle state machine (engineNext) drives the coarse state we report on
 * the heartbeat so the wire projection is exercised end-to-end (STREAMING →
 * RUNNING once output is live). SR conversion (device SR != ring SR) and
 * device-change recovery are later steps (docs/03 AudioFormatManager / Phase 5).
 */
#include "Control.hpp"
#include "DeviceSelection.hpp"
#include "Handshake.hpp"
#include "Heartbeat.hpp"
#include "Lifecycle.hpp"
#include "CubicResampler.hpp"
#include "OutputDevice.hpp"
#include "RingRegion.hpp"

#include "dsp/DSPChain.hpp"
#include "dsp/Analyzer.hpp"
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
#include <mutex>
#include <thread>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>   // _mm_getcsr / _mm_setcsr for FTZ/DAZ
#endif

#include <sys/stat.h>

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

constexpr uint32_t kAnalysisRingFrames = 8192;
constexpr uint32_t kAnalysisWindowFrames = 2048;
constexpr int64_t kAnalysisInterestTtlMs = 2500;
constexpr int kAnalysisIdleSleepMs = 100;
constexpr int kAnalysisLoopSleepMs = 500;

// Shared between the control (main) thread and the render thread. The control
// thread creates the region on HELLO and publishes its header here; the render
// callback (the single ring consumer) picks it up. The RingRegion and DSPChain
// outlive both (owned by main()), so the bare header pointer is safe to share
// once published, and the render thread is the only writer of the counters.
struct EngineContext {
    EngineContext()
        : analysisRing((size_t)kAnalysisRingFrames * ROOMCUT_MVP_CHANNELS, 0.0f) {}

    std::atomic<bool>               running{true};
    std::atomic<RoomcutRingHeader*> header{nullptr};
    std::atomic<RoomcutEngineLifecycle> lifecycle{ROOMCUT_ENGINE_STARTING};

    DSPChain               dsp;                 // render-thread only after prepare()
    CubicResampler         resampler;           // ring SR -> device SR (render-thread)
    std::atomic<uint32_t>  renderPeakBits{0};   // float bits of the last block's peak
    std::atomic<uint64_t>  framesRendered{0};
    std::atomic<uint64_t>  renderUnderruns{0};  // output pulled more than the ring had
    std::atomic<bool>      ringPrimed{false};   // first audio seen; gates underrun counting
    std::atomic<bool>      devicesDirty{false}; // HAL listeners → control loop (Phase 5)
    // System master volume. The macOS slider / app set the Roomcut device's
    // volume; the engine mirrors it to the real output device's HARDWARE volume
    // (full range) and leaves systemVolume at 1.0. Only devices without a
    // settable hardware volume fall back to this digital render gain.
    std::atomic<float>     systemVolume{1.0f};      // control → render (digital fallback)
    std::atomic<float>     volumeBoost{1.0f};
    std::atomic<AudioDeviceID> realOutputDevice{kAudioObjectUnknown}; // mirror target

    // Phase 5 bypass (docs/05-recovery). Manual bypass crossfades via
    // DSPChain::setBypass, applied ON the render thread so the chain is only
    // ever touched from one thread. Safe bypass is a hard latch: the render
    // thread detected non-finite DSP output and skips the chain entirely from
    // then on (a poisoned chain must never reach the speakers; crossfading
    // through it would emit the very NaNs we are escaping).
    std::atomic<bool>      bypassRequested{false}; // control → render (manual)
    std::atomic<bool>      safeBypass{false};      // render → control (latched)
    bool                   bypassApplied = false;  // render-thread only

    // Phase 6 live params: the control thread writes the INACTIVE slot, then
    // release-stores the bumped epoch; the render thread acquire-loads the
    // epoch and applies slot[epoch & 1] via dsp.setParams (the chain's own
    // click-free crossfade). Single writer per slot — no tearing.
    ChainParams            paramsSlots[2];
    std::atomic<uint32_t>  paramsEpoch{0};
    uint32_t               paramsApplied = 0;      // render-thread only
    std::atomic<uint32_t>  limiterGRBits{0};       // float bits, render → control

    // Volume ramp state (render-thread only). Per-sample linear interpolation
    // eliminates zipper noise when systemVolume changes between blocks. The
    // ramp time (~10ms) is perceptually instant but smooths out the step
    // discontinuity at block boundaries (ENGINE_AUDIT.md #6).
    float                  volCurrent = 1.0f;    // render-thread only
    float                  volTarget  = 1.0f;    // render-thread only

    // Underrun concealment state (render-thread only, ENGINE_AUDIT.md #7).
    // When the ring runs dry mid-block, zero-fill creates a hard discontinuity
    // (pop). Instead we apply a short fade-out on the tail of the last good
    // block, and a fade-in when data returns. The fade length (~3ms) is
    // imperceptible but eliminates the click.
    bool                   fadeOutActive = false;  // currently fading/faded to silence
    float                  fadeGain = 1.0f;        // current concealment gain (0..1)
    uint32_t               lastRingGot = 0;        // frames actually read by ringInput (render-thread only)
    // Last good frame, held into any ring shortfall instead of zeros so an
    // underrun never produces a hard 0-jump (the pop). ringInput updates it from
    // the freshest frame actually read; the render-thread fade then ramps the
    // held value to silence if the gap persists. (render-thread only)
    float                  holdFrame[ROOMCUT_MVP_CHANNELS] = {0.0f, 0.0f};

    // Pre-allocated render scratch buffers (moved from thread_local in
    // pullRender to eliminate lazy TLS page-fault / malloc on new IOProc
    // threads — RT-safety fix, see ENGINE_AUDIT.md #4). Allocated in
    // openOutputOn() before output.start(); the render thread only reads
    // the .data() pointer (stable after resize, never reallocated while
    // the callback is live).
    static constexpr uint32_t kMaxOut = 8192;
    std::vector<float>     scratchBuf;           // resampler input staging
    std::vector<float>     dryBuf;               // NaN-detection fallback copy

    // --dump diagnostic: the control thread allocates before output.start();
    // the render thread is the only writer of the contents + frame count.
    std::vector<float>     dumpBuf;
    uint32_t               dumpCapFrames = 0;
    std::atomic<uint32_t>  dumpFrames{0};

    std::vector<float>     analysisRing;
    std::atomic<uint64_t>  analysisWriteFrames{0};
    std::atomic<uint32_t>  analysisSampleRate{0};
    std::atomic<uint32_t>  analysisChannels{0};
    std::atomic<int64_t>   analysisInterestUntilMs{0};
    std::mutex             analysisMutex;
    AnalysisSnapshot       latestAnalysis;
};

EngineContext* g_ctx = nullptr;

void handleSignal(int) {
    if (g_ctx) {
        g_ctx->running.store(false, std::memory_order_relaxed);
    }
}

int64_t steadyMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// SIGUSR1 toggles manual bypass (dev control until the app's XPC lands in
// Phase 6). Async-signal-safe: bump a counter; the control loop translates.
volatile sig_atomic_t g_bypassToggles = 0;
void handleSigUsr1(int) { g_bypassToggles = g_bypassToggles + 1; }

// Flush subnormals (denormals) to zero on the calling (render) thread. The IIR
// filter state in the EQ / parametric / spatial / compressor / limiter decays
// into the subnormal range during silence and quiet passages; subnormal
// arithmetic runs 10-100x slower, which is exactly what spikes the engine's CPU.
// One register write, done once per render thread.
static inline void enableFlushToZero() {
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));  // FZ: flush-to-zero
#elif defined(__x86_64__) || defined(__i386__)
    _mm_setcsr(_mm_getcsr() | 0x8040u);   // FTZ (bit 15) | DAZ (bit 6)
#endif
}

// Resampler input source: pull N input frames from the ring. Runs inside the
// render callback (RT-safe). Records the per-call shortfall as an underrun —
// but only after the first audio has arrived (ringPrimed): the render thread
// may start a beat before the producer's first write, and counting that warmup
// silence would bury the signal the counter exists for (gaps in real audio).
uint32_t ringInput(void* vctx, float* dst, uint32_t inFrames, uint32_t channels) {
    auto* ctx = static_cast<EngineContext*>(vctx);
    RoomcutRingHeader* h = ctx->header.load(std::memory_order_acquire);
    uint32_t got = 0;
    if (h != nullptr && h->channels == channels) {
        got = roomcut_ring_read(h, dst, inFrames);
    }
    if (got > 0) {
        ctx->ringPrimed.store(true, std::memory_order_relaxed);
    }
    // got == 0 means the producer stopped (paused / between tracks) — that's
    // silence, not a glitch, so don't count it. Counting it buried the meter under
    // stopped-stream frames (a paused engine read as ~100% "underrun"). A real
    // shortfall is 0 < got < inFrames: the producer is active but the ring fell behind.
    if (got > 0 && got < inFrames && ctx->ringPrimed.load(std::memory_order_relaxed)) {
        ctx->renderUnderruns.fetch_add(inFrames - got, std::memory_order_relaxed);
    }

    // Last-value hold (ENGINE_AUDIT.md #7): fill any shortfall with the last good
    // frame instead of leaving zeros. A hard 0-jump on underrun is the pop; holding
    // the last sample keeps the waveform continuous, and the render-thread fade
    // (pullRender) then ramps the held value to silence if the gap persists. We
    // return inFrames so the resampler treats the buffer as full and does NOT
    // overwrite the held tail with zeros — this makes the fade-out effective even
    // on the bit-exact passthrough path, and conceals partial underruns too.
    if (got > 0) {
        const uint32_t last = (got - 1) * channels;
        for (uint32_t c = 0; c < channels; ++c) ctx->holdFrame[c] = dst[last + c];
    }
    for (uint32_t f = got; f < inFrames; ++f) {
        for (uint32_t c = 0; c < channels; ++c) dst[f * channels + c] = ctx->holdFrame[c];
    }
    ctx->lastRingGot = got;
    return inFrames;
}

// The output device's render callback — the SINGLE consumer of the ring.
// RT-safe: no alloc/lock/log. Resamples ring (input SR) -> device SR into the
// output buffer, runs the DSP chain in place, and records a peak for the
// control thread to print. Scratch buffers live in EngineContext (pre-allocated
// in openOutputOn before start).
void pullRender(void* vctx, float* dst, uint32_t frames, uint32_t channels) {
    // Set flush-to-zero once on this render thread so denormal filter state never
    // tanks the CPU (the engine's idle/quiet-passage spike).
    static thread_local bool ftzReady = false;
    if (!ftzReady) { enableFlushToZero(); ftzReady = true; }

    auto* ctx = static_cast<EngineContext*>(vctx);

    // Scratch buffers live in EngineContext (pre-allocated in openOutputOn,
    // RT-safe: no lazy TLS fault). kMaxOut matches EngineContext::kMaxOut.
    constexpr uint32_t kMaxOut = EngineContext::kMaxOut;

    uint32_t out = frames > kMaxOut ? kMaxOut : frames;
    ctx->resampler.produce(dst, out, &ringInput, ctx, ctx->scratchBuf.data(), kMaxOut);
    if (out < frames) {
        std::memset(dst + (size_t)out * channels, 0,
                    (size_t)(frames - out) * channels * sizeof(float));
    }

    const uint32_t n = frames * channels;

    // Underrun concealment (ENGINE_AUDIT.md #7): when the ring runs dry
    // (lastRingGot == 0 after priming), ringInput has already held the last good
    // sample into the buffer (no 0-jump), so here we ramp that held, continuous
    // waveform down to silence over ~3ms instead of freezing on a DC value. On
    // data return, fade back in. Normal playback (fadeGain == 1.0, no underrun)
    // costs one branch.
    {
        constexpr float kFadeMs = 3.0f;
        // Approximate fade step assuming ~48kHz; exact SR doesn't matter much
        // for a 3ms cosmetic fade. Step per sample = 1/(SR*fadeMs/1000).
        // At 48k: 1/(48000*0.003) = ~0.00694. At 96k it's half that (gentler).
        const float fadeStep = 1.0f / (48.0f * kFadeMs); // ~0.00694

        const bool ringDry = (ctx->lastRingGot == 0 &&
                              ctx->ringPrimed.load(std::memory_order_relaxed));

        if (ringDry && !ctx->fadeOutActive) {
            ctx->fadeOutActive = true;  // start fading out
        } else if (!ringDry && ctx->fadeOutActive) {
            ctx->fadeOutActive = false; // data returned, start fading in
        }

        if (ctx->fadeOutActive && ctx->fadeGain > 0.0f) {
            // Fade out: ramp gain from current toward 0.
            for (uint32_t i = 0; i < n; ++i) {
                dst[i] *= ctx->fadeGain;
                ctx->fadeGain -= fadeStep;
                if (ctx->fadeGain < 0.0f) ctx->fadeGain = 0.0f;
            }
        } else if (!ctx->fadeOutActive && ctx->fadeGain < 1.0f) {
            // Fade in: ramp gain from current toward 1.
            for (uint32_t i = 0; i < n; ++i) {
                ctx->fadeGain += fadeStep;
                if (ctx->fadeGain > 1.0f) ctx->fadeGain = 1.0f;
                dst[i] *= ctx->fadeGain;
            }
        }
        // else: fadeGain == 1.0 and no underrun → normal path, no multiply.
    }

    // Live params (Phase 6): pick up a newly published parameter set. The
    // chain crossfades to it, so preset switches are click-free.
    const uint32_t pe = ctx->paramsEpoch.load(std::memory_order_acquire);
    if (pe != ctx->paramsApplied) {
        ctx->dsp.setParams(ctx->paramsSlots[pe & 1u]);
        ctx->paramsApplied = pe;
    }

    // Manual bypass: apply the (click-free, crossfaded) toggle here so the
    // DSP chain is only ever touched from this thread.
    const bool manual = ctx->bypassRequested.load(std::memory_order_relaxed);
    if (manual != ctx->bypassApplied) {
        ctx->dsp.setBypass(manual);
        ctx->bypassApplied = manual;
    }

    if (!ctx->safeBypass.load(std::memory_order_relaxed)) {
        std::memcpy(ctx->dryBuf.data(), dst, (size_t)n * sizeof(float));

        ctx->dsp.processInterleaved(dst, frames);

        // NaN/inf detector: any non-finite sample poisons the sum. On trip,
        // emit the dry block instead and latch safe bypass (docs/05: "DSP
        // throws / NaN storm → switch to bypass, log, continue").
        double sumsq = 0.0;
        for (uint32_t i = 0; i < n; ++i) sumsq += (double)dst[i] * dst[i];
        if (!std::isfinite(sumsq)) {
            std::memcpy(dst, ctx->dryBuf.data(), (size_t)n * sizeof(float));
            ctx->safeBypass.store(true, std::memory_order_relaxed);
        }

        // Limiter gain reduction → control thread (UI clipping indicator).
        float gr = (float)ctx->dsp.limiterGainReductionDb();
        uint32_t grBits;
        std::memcpy(&grBits, &gr, sizeof(grBits));
        ctx->limiterGRBits.store(grBits, std::memory_order_relaxed);
    }

    // System master volume: the final attenuation before the real output,
    // mirroring the macOS volume slider on the Roomcut Output device. Applied
    // even under bypass (volume must always work) and after the limiter so it
    // never defeats clip protection.
    //
    // Per-sample linear ramp (ENGINE_AUDIT.md #6): eliminates zipper noise by
    // smoothly interpolating from the current gain to the new target over ~10ms.
    // Pattern mirrors DSPChain::mix_ ramp (mixStep_/mixTarget_).
    const float targetVol = ctx->systemVolume.load(std::memory_order_relaxed);
    if (targetVol != ctx->volTarget) {
        ctx->volTarget = targetVol;
    }

    if (ctx->volCurrent == ctx->volTarget) {
        // Steady state: apply constant gain (skip ramp math).
        if (ctx->volCurrent != 1.0f) {
            const float v = ctx->volCurrent;
            for (uint32_t i = 0; i < n; ++i) dst[i] *= v;
        }
    } else {
        // Ramp toward target over ~10ms worth of samples (480 @ 48kHz).
        // Use frames as a conservative approximation of available samples/channel;
        // the step is recomputed each block so any SR is handled correctly.
        const float rampSamples = (float)frames > 480.0f ? (float)frames : 480.0f;
        const float step = (ctx->volTarget - ctx->volCurrent) / rampSamples;
        const uint32_t ch = channels;
        float vol = ctx->volCurrent;
        for (uint32_t f = 0; f < frames; ++f) {
            // Advance toward target; clamp to exact arrival.
            if (step > 0.0f)      vol = std::min(ctx->volTarget, vol + step);
            else if (step < 0.0f) vol = std::max(ctx->volTarget, vol + step);
            for (uint32_t c = 0; c < ch; ++c) {
                dst[f * ch + c] *= vol;
            }
        }
        ctx->volCurrent = vol;
    }

    float peak = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float a = dst[i] < 0.0f ? -dst[i] : dst[i];
        if (a > peak) peak = a;
    }
    uint32_t bits;
    std::memcpy(&bits, &peak, sizeof(bits));
    ctx->renderPeakBits.store(bits, std::memory_order_relaxed);
    ctx->framesRendered.fetch_add(frames, std::memory_order_relaxed);

    if (!ctx->analysisRing.empty() && channels == ROOMCUT_MVP_CHANNELS) {
        const uint32_t cap = kAnalysisRingFrames;
        uint64_t at = ctx->analysisWriteFrames.load(std::memory_order_relaxed);
        const float* src = dst;
        uint32_t left = frames;
        if (left > cap) {
            src += (size_t)(left - cap) * channels;
            at += left - cap;
            left = cap;
        }
        const uint32_t pos = (uint32_t)(at & (cap - 1u));
        const uint32_t first = std::min(left, cap - pos);
        std::memcpy(ctx->analysisRing.data() + (size_t)pos * channels, src,
                    (size_t)first * channels * sizeof(float));
        if (first < left) {
            std::memcpy(ctx->analysisRing.data(), src + (size_t)first * channels,
                        (size_t)(left - first) * channels * sizeof(float));
        }
        ctx->analysisWriteFrames.store(at + left, std::memory_order_release);
    }

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
                      const ChainParams& params) {
    // Ask the device to run at the ring rate so the render path is bit-exact
    // (ratio 1.0) rather than linear-interpolated — the fix for the soft,
    // low-resolution sound vs. listening to the device directly.
    OSStatus err = output.open(&pullRender, &ctx, ROOMCUT_MVP_CHANNELS, device, (double)ringSR);
    if (err != noErr) return err;

    // Pre-allocate render scratch buffers BEFORE the callback fires (RT-safety:
    // eliminates thread_local lazy page-fault on new IOProc threads). The assign()
    // zero-fills, which also pre-faults every page into the resident set.
    ctx.scratchBuf.assign((size_t)EngineContext::kMaxOut * ROOMCUT_MVP_CHANNELS, 0.0f);
    ctx.dryBuf.assign((size_t)EngineContext::kMaxOut * ROOMCUT_MVP_CHANNELS, 0.0f);

    ctx.dsp.prepare(output.sampleRate(), ROOMCUT_MVP_CHANNELS);
    ctx.dsp.setParams(params);
    ctx.resampler.prepare((double)ringSR, output.sampleRate(), ROOMCUT_MVP_CHANNELS);
    ctx.analysisSampleRate.store(0, std::memory_order_release);
    ctx.analysisChannels.store(0, std::memory_order_release);
    ctx.analysisWriteFrames.store(0, std::memory_order_release);
    ctx.analysisSampleRate.store((uint32_t)std::lround(output.sampleRate()), std::memory_order_release);
    ctx.analysisChannels.store(ROOMCUT_MVP_CHANNELS, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(ctx.analysisMutex);
        ctx.latestAnalysis = AnalysisSnapshot{};
    }
    if ((uint32_t)output.sampleRate() != ringSR) {
        std::fprintf(stderr,
            "[engine] device sr=%.0f != ring sr=%u; linear-resampling ring->device (ratio=%.4f)\n",
            output.sampleRate(), ringSR, ctx.resampler.ratio());
    } else {
        std::fprintf(stderr,
            "[engine] device sr=%u == ring sr; bit-exact passthrough (no resampling)\n", ringSR);
    }
    return noErr;
}

void copyLatestAnalysisWindow(EngineContext& ctx, std::vector<float>& window,
                              uint64_t endFrame, uint32_t channels) {
    const uint64_t startFrame = endFrame - kAnalysisWindowFrames;
    const uint32_t cap = kAnalysisRingFrames;
    window.resize((size_t)kAnalysisWindowFrames * channels);
    const uint32_t pos = (uint32_t)(startFrame & (cap - 1u));
    const uint32_t first = std::min(kAnalysisWindowFrames, cap - pos);
    std::memcpy(window.data(), ctx.analysisRing.data() + (size_t)pos * channels,
                (size_t)first * channels * sizeof(float));
    if (first < kAnalysisWindowFrames) {
        std::memcpy(window.data() + (size_t)first * channels, ctx.analysisRing.data(),
                    (size_t)(kAnalysisWindowFrames - first) * channels * sizeof(float));
    }
}

float blendFloat(float previous, float next, float amount) {
    return previous + (next - previous) * amount;
}

void smoothAnalysis(AnalysisSnapshot& snap, const AnalysisSnapshot& previous) {
    constexpr float kScalarAmount = 0.22f;
    constexpr float kSpectrumAmount = 0.30f;
    snap.peakDb = blendFloat(previous.peakDb, snap.peakDb, kScalarAmount);
    snap.rmsDb = blendFloat(previous.rmsDb, snap.rmsDb, kScalarAmount);
    snap.crestFactor = blendFloat(previous.crestFactor, snap.crestFactor, kScalarAmount);
    snap.lowEnergy = blendFloat(previous.lowEnergy, snap.lowEnergy, kScalarAmount);
    snap.lowMidEnergy = blendFloat(previous.lowMidEnergy, snap.lowMidEnergy, kScalarAmount);
    snap.midEnergy = blendFloat(previous.midEnergy, snap.midEnergy, kScalarAmount);
    snap.highEnergy = blendFloat(previous.highEnergy, snap.highEnergy, kScalarAmount);
    snap.spectralCentroid = blendFloat(previous.spectralCentroid, snap.spectralCentroid, kScalarAmount);
    snap.stereoWidth = blendFloat(previous.stereoWidth, snap.stereoWidth, kScalarAmount);
    snap.midSideRatio = blendFloat(previous.midSideRatio, snap.midSideRatio, kScalarAmount);
    snap.correlation = blendFloat(previous.correlation, snap.correlation, kScalarAmount);
    snap.muddiness = blendFloat(previous.muddiness, snap.muddiness, kScalarAmount);
    snap.harshness = blendFloat(previous.harshness, snap.harshness, kScalarAmount);
    snap.sibilance = blendFloat(previous.sibilance, snap.sibilance, kScalarAmount);
    snap.voicePresence = blendFloat(previous.voicePresence, snap.voicePresence, kScalarAmount);
    snap.reverbEstimate = blendFloat(previous.reverbEstimate, snap.reverbEstimate, kScalarAmount);
    snap.dynamicRange = blendFloat(previous.dynamicRange, snap.dynamicRange, kScalarAmount);
    for (std::size_t i = 0; i < snap.spectrum.size(); ++i) {
        snap.spectrum[i] = blendFloat(previous.spectrum[i], snap.spectrum[i], kSpectrumAmount);
    }
}

void analysisLoop(EngineContext* ctx) {
    Analyzer analyzer;
    std::vector<float> window;
    uint64_t lastAnalyzed = 0;
    AnalysisSnapshot previous;
    bool hasPrevious = false;
    while (ctx->running.load(std::memory_order_relaxed)) {
        const int64_t nowMs = steadyMillis();
        if (ctx->analysisInterestUntilMs.load(std::memory_order_acquire) < nowMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kAnalysisIdleSleepMs));
            continue;
        }
        const uint32_t sr = ctx->analysisSampleRate.load(std::memory_order_acquire);
        const uint32_t channels = ctx->analysisChannels.load(std::memory_order_acquire);
        const uint64_t written = ctx->analysisWriteFrames.load(std::memory_order_acquire);
        if (sr == 0 || channels == 0 || written < kAnalysisWindowFrames || written == lastAnalyzed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kAnalysisIdleSleepMs));
            continue;
        }
        if (ctx->analysisRing.size() < (size_t)kAnalysisRingFrames * channels) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kAnalysisIdleSleepMs));
            continue;
        }
        copyLatestAnalysisWindow(*ctx, window, written, channels);
        AnalysisSnapshot snap = analyzer.analyzeInterleaved(
            window.data(), kAnalysisWindowFrames, channels, (double)sr);
        snap.framesAnalyzed = written;
        if (snap.valid && hasPrevious && previous.valid) {
            smoothAnalysis(snap, previous);
        }
        {
            std::lock_guard<std::mutex> lock(ctx->analysisMutex);
            ctx->latestAnalysis = snap;
        }
        previous = snap;
        hasPrevious = snap.valid;
        lastAnalyzed = written;
        std::this_thread::sleep_for(std::chrono::milliseconds(kAnalysisLoopSleepMs));
    }
}

// ---- Phase 5 persistent state (docs/05-recovery) ----
//
// Facts that survive engine death: the UID of the last REAL output device we
// rendered to (the respawned engine needs it to restore the system default
// when Roomcut is selected but the driver never reconnects, and on clean exit
// to leave the system on a real device), plus the last applied preset/params
// so a respawned or rebooted engine resumes the user's curve instead of flat
// (2026-06-13).

struct PersistentState {
    std::string realOutputUID;
    std::string presetId;        // builtin id or "custom"; empty = nothing saved
    std::string paramsLine;      // serializeParamsLine() when presetId == "custom"
    std::string parametricLine;  // serializeParametricLine() when any band is set
    std::string preferredOutputUID; // user-pinned output device; empty = auto policy
    bool        keepRoomcutDefault = false; // reclaim Roomcut as system default
    double      volumeBoost = 1.0;
};

std::string stateFilePath() {
    const char* overridePath = std::getenv("ROOMCUT_STATE_FILE");
    if (overridePath != nullptr && overridePath[0] != '\0') {
        return overridePath;
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return {};
    return std::string(home) + "/Library/Application Support/Roomcut/engine.state";
}

double clampVolumeBoost(double boost) {
    if (!std::isfinite(boost)) return 1.0;
    if (boost < 1.0) return 1.0;
    if (boost > 2.0) return 2.0;
    return boost;
}

// "<preamp> <g0..g9> <releaseMs> <outDb> [spatial...] [dynamics...]" — same
// order and meaning as `roomcutctl params`, human-readable in the state file.
// Extras past the base 13 values are optional (older files simply stop early
// and the missing fields stay 0), so the format only ever appends.
std::string serializeParamsLine(const ChainParams& p) {
    char buf[512];
    int n = std::snprintf(buf, sizeof(buf), "%.4f", p.preampDb);
    for (double g : p.eqGainsDb) {
        n += std::snprintf(buf + n, sizeof(buf) - (std::size_t)n, " %.4f", g);
    }
    std::snprintf(buf + n, sizeof(buf) - (std::size_t)n,
                  " %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f",
                  p.limiterReleaseMs, p.outputGainDb,
                  p.spatialWidth, p.centerFocus, p.crossfeed, p.roomReduce, p.spatialMode,
                  p.highpassHz, p.compAmount);
    return buf;
}

bool parseParamsLine(const std::string& s, ChainParams* out) {
    const char* p = s.c_str();
    constexpr std::size_t kBaseValues = GraphicEQ::kNumBands + 3;
    constexpr std::size_t kTotalValues = kBaseValues + 7;
    double v[kTotalValues]{};
    for (std::size_t i = 0; i < kBaseValues; ++i) {
        char* end = nullptr;
        v[i] = std::strtod(p, &end);
        if (end == p) return false;
        p = end;
    }
    for (std::size_t i = kBaseValues; i < kTotalValues; ++i) {
        char* end = nullptr;
        double x = std::strtod(p, &end);
        if (end == p) break;
        v[i] = x;
        p = end;
    }
    out->preampDb = v[0];
    for (std::size_t b = 0; b < GraphicEQ::kNumBands; ++b) {
        out->eqGainsDb[b] = v[1 + b];
    }
    out->limiterReleaseMs = v[GraphicEQ::kNumBands + 1];
    out->outputGainDb     = v[GraphicEQ::kNumBands + 2];
    out->spatialWidth     = v[GraphicEQ::kNumBands + 3];
    out->centerFocus      = v[GraphicEQ::kNumBands + 4];
    out->crossfeed        = v[GraphicEQ::kNumBands + 5];
    out->roomReduce       = v[GraphicEQ::kNumBands + 6];
    out->spatialMode      = v[GraphicEQ::kNumBands + 7];
    out->highpassHz       = v[GraphicEQ::kNumBands + 8];
    out->compAmount       = v[GraphicEQ::kNumBands + 9];
    return true;
}

// Parametric bands on one line: "<en> <type> <freq> <gain> <q>" × N. Separate
// from the params line so the existing 18-value parser is untouched. Empty when
// no band is set (a flat parametric bank persists nothing).
std::string serializeParametricLine(const ChainParams& p) {
    bool any = false;
    for (const auto& b : p.parametric) if (b.enabled) { any = true; break; }
    if (!any) return {};
    std::string s;
    char buf[96];
    for (const auto& b : p.parametric) {
        std::snprintf(buf, sizeof(buf), "%d %d %.4f %.4f %.4f ",
                      b.enabled ? 1 : 0, b.type, b.freqHz, b.gainDb, b.q);
        s += buf;
    }
    return s;
}

void parseParametricLine(const std::string& s, ChainParams* out) {
    const char* p = s.c_str();
    for (std::size_t i = 0; i < out->parametric.size(); ++i) {
        char* end = nullptr;
        long en = std::strtol(p, &end, 10); if (end == p) break; p = end;
        long ty = std::strtol(p, &end, 10); if (end == p) break; p = end;
        double fr = std::strtod(p, &end);   if (end == p) break; p = end;
        double gn = std::strtod(p, &end);   if (end == p) break; p = end;
        double q  = std::strtod(p, &end);   if (end == p) break; p = end;
        out->parametric[i].enabled = en != 0;
        out->parametric[i].type    = (int)ty;
        out->parametric[i].freqHz  = fr;
        out->parametric[i].gainDb  = gn;
        out->parametric[i].q       = q;
    }
}

PersistentState loadPersistentState() {
    PersistentState st;
    std::string path = stateFilePath();
    if (path.empty()) return st;
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) return st;
    char line[512];
    auto put = [](const char* v, std::string* dst) {
        *dst = v;
        while (!dst->empty() && (dst->back() == '\n' || dst->back() == '\r')) {
            dst->pop_back();
        }
    };
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (std::strncmp(line, "realOutputUID=", 14) == 0) {
            put(line + 14, &st.realOutputUID);
        } else if (std::strncmp(line, "preset=", 7) == 0) {
            put(line + 7, &st.presetId);
        } else if (std::strncmp(line, "params=", 7) == 0) {
            put(line + 7, &st.paramsLine);
        } else if (std::strncmp(line, "parametric=", 11) == 0) {
            put(line + 11, &st.parametricLine);
        } else if (std::strncmp(line, "preferredOutputUID=", 19) == 0) {
            put(line + 19, &st.preferredOutputUID);
        } else if (std::strncmp(line, "keepDefault=", 12) == 0) {
            st.keepRoomcutDefault = (line[12] == '1');
        } else if (std::strncmp(line, "volumeBoost=", 12) == 0) {
            st.volumeBoost = clampVolumeBoost(std::strtod(line + 12, nullptr));
        }
    }
    std::fclose(f);
    return st;
}

void savePersistentState(const PersistentState& st) {
    std::string path = stateFilePath();
    if (path.empty()) return;
    std::string dir = path.substr(0, path.rfind('/'));
    ::mkdir(dir.c_str(), 0755); // EEXIST is fine; Application Support exists
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        std::fprintf(stderr, "[engine] state: cannot write %s\n", path.c_str());
        return;
    }
    if (!st.realOutputUID.empty()) {
        std::fprintf(f, "realOutputUID=%s\n", st.realOutputUID.c_str());
    }
    if (!st.preferredOutputUID.empty()) {
        std::fprintf(f, "preferredOutputUID=%s\n", st.preferredOutputUID.c_str());
    }
    if (st.keepRoomcutDefault) {
        std::fprintf(f, "keepDefault=1\n");
    }
    if (st.volumeBoost > 1.0) {
        std::fprintf(f, "volumeBoost=%.4f\n", clampVolumeBoost(st.volumeBoost));
    }
    if (!st.presetId.empty()) {
        std::fprintf(f, "preset=%s\n", st.presetId.c_str());
        if (!st.paramsLine.empty()) {
            std::fprintf(f, "params=%s\n", st.paramsLine.c_str());
        }
    }
    if (!st.parametricLine.empty()) {
        std::fprintf(f, "parametric=%s\n", st.parametricLine.c_str());
    }
    std::fclose(f);
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

// Phase 5 device watcher (docs/05-recovery): HAL property listeners funnel
// into one dirty flag the control loop polls. The listener proc runs on a HAL
// thread — it does an atomic store and nothing else.
struct DeviceWatcher {
    EngineContext* ctx = nullptr;
    AudioDeviceID  srDevice = kAudioObjectUnknown; // open device whose SR we watch

    static OSStatus onChange(AudioObjectID, UInt32,
                             const AudioObjectPropertyAddress*, void* client) {
        static_cast<EngineContext*>(client)->devicesDirty.store(
            true, std::memory_order_relaxed);
        return noErr;
    }
    static AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel) {
        return { sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    }

    void install(EngineContext* c) {
        ctx = c;
        AudioObjectPropertyAddress a1 = addr(kAudioHardwarePropertyDefaultOutputDevice);
        AudioObjectPropertyAddress a2 = addr(kAudioHardwarePropertyDevices);
        AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a1, &onChange, ctx);
        AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a2, &onChange, ctx);
    }
    void remove() {
        if (ctx == nullptr) return;
        watchSR(kAudioObjectUnknown);
        AudioObjectPropertyAddress a1 = addr(kAudioHardwarePropertyDefaultOutputDevice);
        AudioObjectPropertyAddress a2 = addr(kAudioHardwarePropertyDevices);
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &a1, &onChange, ctx);
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &a2, &onChange, ctx);
        ctx = nullptr;
    }
    // Follow the open output device: its nominal-SR changes don't fire the
    // system-object listeners, so they need their own registration.
    void watchSR(AudioDeviceID dev) {
        AudioObjectPropertyAddress a = addr(kAudioDevicePropertyNominalSampleRate);
        if (srDevice != kAudioObjectUnknown && ctx != nullptr) {
            AudioObjectRemovePropertyListener(srDevice, &a, &onChange, ctx);
        }
        srDevice = dev;
        if (srDevice != kAudioObjectUnknown && ctx != nullptr) {
            AudioObjectAddPropertyListener(srDevice, &a, &onChange, ctx);
        }
    }
};

// Acquire the receive right for ROOMCUT_MACH_SERVICE_NAME. Returns
// MACH_PORT_NULL on failure. `outRegistered` is set true if we had to register
// the name ourselves (dev path) vs. checking in with launchd (production path).
mach_port_t acquireServicePort(bool* outRegistered) {
    *outRegistered = false;

    mach_port_t bp = MACH_PORT_NULL;
    if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS ||
        bp == MACH_PORT_NULL) {
        std::fprintf(stderr, "[engine] no bootstrap port\n");
        return MACH_PORT_NULL;
    }

    // Production path: launchd already owns the receive right; claim it.
    mach_port_t service = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_check_in(bp, ROOMCUT_MACH_SERVICE_NAME, &service);
    if (kr == KERN_SUCCESS && service != MACH_PORT_NULL) {
        std::fprintf(stderr, "[engine] bootstrap_check_in OK (launchd-provided)\n");
        return service;
    }
    std::fprintf(stderr, "[engine] bootstrap_check_in failed (%s); trying dev register\n",
                 bootstrap_strerror(kr));

    // Dev path: allocate a receive right + a send right and register the name so
    // a driver-sim in the same session can bootstrap_look_up it. bootstrap_register
    // is deprecated (launchd MachServices check-in is the production path) but
    // still works in a login session for local testing, so suppress the warning
    // for this one intentional dev-only call.
    mach_port_t self = mach_task_self();
    if (mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &service) != KERN_SUCCESS) {
        return MACH_PORT_NULL;
    }
    if (mach_port_insert_right(self, service, service, MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) {
        mach_port_mod_refs(self, service, MACH_PORT_RIGHT_RECEIVE, -1);
        return MACH_PORT_NULL;
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    kr = bootstrap_register(bp, const_cast<char*>(ROOMCUT_MACH_SERVICE_NAME), service);
#pragma clang diagnostic pop
    if (kr != KERN_SUCCESS) {
        std::fprintf(stderr, "[engine] bootstrap_register failed (%s)\n",
                     bootstrap_strerror(kr));
        mach_port_mod_refs(self, service, MACH_PORT_RIGHT_RECEIVE, -1);
        return MACH_PORT_NULL;
    }
    std::fprintf(stderr, "[engine] bootstrap_register OK (dev path)\n");
    *outRegistered = true;
    return service;
}

// Capture cap for the --dump diagnostic (covers the 30 s soak with margin).
constexpr uint32_t kDumpMaxSeconds = 35;

// Minimal RIFF/WAVE writer for --dump: 32-bit float PCM (wFormatTag=3),
// interleaved. Little-endian host assumed (macOS).
bool writeWavF32(const char* path, const float* samples, uint32_t frames,
                 uint32_t channels, uint32_t sampleRate) {
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const uint32_t dataBytes  = frames * channels * (uint32_t)sizeof(float);
    const uint32_t riffBytes  = 36u + dataBytes;
    const uint32_t byteRate   = sampleRate * channels * (uint32_t)sizeof(float);
    const uint16_t blockAlign = (uint16_t)(channels * sizeof(float));
    const uint32_t fmtSize    = 16u;
    const uint16_t fmtFloat   = 3u;
    const uint16_t bits       = 32u;
    const uint16_t ch16       = (uint16_t)channels;
    bool ok = std::fwrite("RIFF", 1, 4, f) == 4
           && std::fwrite(&riffBytes, 4, 1, f) == 1
           && std::fwrite("WAVE", 1, 4, f) == 4
           && std::fwrite("fmt ", 1, 4, f) == 4
           && std::fwrite(&fmtSize, 4, 1, f) == 1
           && std::fwrite(&fmtFloat, 2, 1, f) == 1
           && std::fwrite(&ch16, 2, 1, f) == 1
           && std::fwrite(&sampleRate, 4, 1, f) == 1
           && std::fwrite(&byteRate, 4, 1, f) == 1
           && std::fwrite(&blockAlign, 2, 1, f) == 1
           && std::fwrite(&bits, 2, 1, f) == 1
           && std::fwrite("data", 1, 4, f) == 4
           && std::fwrite(&dataBytes, 4, 1, f) == 1;
    if (ok && dataBytes > 0) {
        ok = std::fwrite(samples, 1, dataBytes, f) == dataBytes;
    }
    return std::fclose(f) == 0 && ok;
}

// Resolve the Roomcut Output device we own (re-resolved on device-world changes;
// the id is cached for cheap per-tick volume reads).
AudioDeviceID findRoomcutDevice() {
    for (const auto& d : listOutputDevices()) {
        if (isRoomcutDeviceUID(d.uid)) return d.id;
    }
    return kAudioObjectUnknown;
}

// The Roomcut device's master volume as a linear gain (the macOS slider sets
// it there): muted → 0, else the 0..1 VolumeScalar. Returns 1.0 on any read
// failure so audio is never silenced by a missing property.
float roomcutMasterGain(AudioDeviceID rc) {
    if (rc == kAudioObjectUnknown) return 1.0f;

    AudioObjectPropertyAddress muteAddr{
        kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain};
    UInt32 muted = 0, msz = sizeof(muted);
    if (AudioObjectHasProperty(rc, &muteAddr)
        && AudioObjectGetPropertyData(rc, &muteAddr, 0, nullptr, &msz, &muted) == noErr
        && muted != 0) {
        return 0.0f;
    }

    AudioObjectPropertyAddress volAddr{
        kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain};
    Float32 scalar = 1.0f;
    UInt32 vsz = sizeof(scalar);
    if (AudioObjectHasProperty(rc, &volAddr)
        && AudioObjectGetPropertyData(rc, &volAddr, 0, nullptr, &vsz, &scalar) == noErr) {
        if (scalar < 0.0f) scalar = 0.0f;
        if (scalar > 1.0f) scalar = 1.0f;
        return scalar;
    }
    return 1.0f;
}

// Number of output channels the device exposes (for per-channel volume).
UInt32 outputChannelCount(AudioDeviceID dev) {
    AudioObjectPropertyAddress a{kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &sz) != noErr || sz == 0) return 0;
    std::vector<char> buf(sz);
    AudioBufferList* bl = reinterpret_cast<AudioBufferList*>(buf.data());
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &sz, bl) != noErr) return 0;
    UInt32 n = 0;
    for (UInt32 i = 0; i < bl->mNumberBuffers; ++i) n += bl->mBuffers[i].mNumberChannels;
    return n;
}

// Set the real output device's volume to `scalar` (0..1). Prefer the Main
// element; if the device exposes volume only PER-CHANNEL (Bluetooth devices like
// AirPods have no Main volume — only ch1/ch2), set every output channel instead.
// Returns true if any element was actually set (→ hardware carries the level).
bool setRealDeviceVolume(AudioDeviceID real, Float32 scalar) {
    if (real == kAudioObjectUnknown) return false;
    auto trySet = [&](UInt32 elem) -> bool {
        AudioObjectPropertyAddress a{kAudioDevicePropertyVolumeScalar,
            kAudioObjectPropertyScopeOutput, elem};
        Boolean settable = false;
        if (AudioObjectHasProperty(real, &a)
            && AudioObjectIsPropertySettable(real, &a, &settable) == noErr && settable) {
            Float32 v = scalar;
            return AudioObjectSetPropertyData(real, &a, 0, nullptr, sizeof(v), &v) == noErr;
        }
        return false;
    };
    if (trySet(kAudioObjectPropertyElementMain)) return true;   // element 0 (master)
    bool any = false;
    UInt32 nch = outputChannelCount(real);
    for (UInt32 ch = 1; ch <= nch; ++ch) any = trySet(ch) || any; // per-channel L/R/…
    return any;
}

// Push the Roomcut volume (set by the macOS slider / app) onto the REAL output
// device's hardware volume — full range, what the user expects. Devices without
// any settable hardware volume fall back to a digital render gain. The engine is
// the sole writer of the real device's volume, so there's no tug-of-war.
void mirrorVolumeToReal(EngineContext* ctx, AudioDeviceID roomcutDev) {
    if (ctx == nullptr) return;
    const float scalar = roomcutMasterGain(roomcutDev);
    const float boost = ctx->volumeBoost.load(std::memory_order_relaxed);
    const AudioDeviceID real = ctx->realOutputDevice.load(std::memory_order_relaxed);
    if (setRealDeviceVolume(real, scalar)) {
        ctx->systemVolume.store(boost, std::memory_order_relaxed);          // hardware carries it
    } else {
        ctx->systemVolume.store(scalar * boost, std::memory_order_relaxed); // digital fallback
    }
}

// Roomcut volume/mute changed (macOS slider or app) → mirror immediately so the
// response is instant rather than waiting for the control-loop poll.
OSStatus roomcutVolumeListener(AudioObjectID inObjectID, UInt32,
                               const AudioObjectPropertyAddress*, void* clientData) {
    mirrorVolumeToReal(static_cast<EngineContext*>(clientData), inObjectID);
    return noErr;
}

void setVolumeListener(AudioDeviceID dev, EngineContext* ctx, bool add) {
    if (dev == kAudioObjectUnknown) return;
    const AudioObjectPropertySelector sels[] = {
        kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyMute};
    for (AudioObjectPropertySelector sel : sels) {
        AudioObjectPropertyAddress addr{sel, kAudioObjectPropertyScopeOutput,
                                        kAudioObjectPropertyElementMain};
        if (add) AudioObjectAddPropertyListener(dev, &addr, &roomcutVolumeListener, ctx);
        else     AudioObjectRemovePropertyListener(dev, &addr, &roomcutVolumeListener, ctx);
    }
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
    const char* dumpPath = nullptr;
    ChainParams chainParams = ChainParams::flat();
    bool eqGiven = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            dumpPath = argv[++i];
        } else if (std::strcmp(argv[i], "--eq") == 0 && i + 1 < argc) {
            const char* s = argv[++i];
            char* end = nullptr;
            bool ok = true;
            for (std::size_t b = 0; b < GraphicEQ::kNumBands && ok; ++b) {
                chainParams.eqGainsDb[b] = std::strtod(s, &end);
                ok = end != s
                  && (b + 1 == GraphicEQ::kNumBands ? *end == '\0' : *end == ',');
                s = end + 1;
            }
            if (!ok) {
                std::fprintf(stderr, "bad --eq: need %zu comma-separated dB values\n",
                             GraphicEQ::kNumBands);
                return 2;
            }
            eqGiven = true;
        } else {
            std::fprintf(stderr, "usage: %s [--dump out.wav] [--eq g0,g1,...,g9]\n",
                         argv[0]);
            return 2;
        }
    }
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
    PersistentState pstate = loadPersistentState();
    if (!eqGiven && !pstate.presetId.empty()) {
        bool resumed = false;
        if (pstate.presetId == "custom") {
            resumed = parseParamsLine(pstate.paramsLine, &chainParams);
            if (resumed && !pstate.parametricLine.empty()) {
                parseParametricLine(pstate.parametricLine, &chainParams);
            }
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
    ChainParams currentParams = PresetValidator::clamp(chainParams);
    uint32_t paramsRevision = 0;

    EngineContext ctx;
    ctx.volumeBoost.store((float)clampVolumeBoost(pstate.volumeBoost), std::memory_order_relaxed);
    g_ctx = &ctx;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGUSR1, handleSigUsr1);

    bool registered = false;
    mach_port_t service = acquireServicePort(&registered);
    if (service == MACH_PORT_NULL) {
        std::fprintf(stderr, "[engine] could not acquire service port; exiting\n");
        return 1;
    }
    ctx.lifecycle.store(engineNext(ROOMCUT_ENGINE_STARTING, EngineEvent::ServicePublished),
                        std::memory_order_relaxed);
    std::fprintf(stderr, "[engine] service '%s' up; waiting for driver HELLO\n",
                 ROOMCUT_MACH_SERVICE_NAME);

    // The region + output device must outlive the render callback; owned here.
    RingRegion    region;
    OutputDevice  output;
    DeviceWatcher watcher;
    bool          outputStarted = false;
    uint32_t      dumpSR = 0;  // device SR at capture time (output.close() zeroes it)
    uint32_t      ringSR = 0;  // negotiated ring SR (0 until first HELLO)
    std::string   savedRealUID = pstate.realOutputUID; // survives engine death
    std::string   preferredOutputUID = pstate.preferredOutputUID; // user-pinned device
    bool          keepRoomcutDefault = pstate.keepRoomcutDefault;  // reclaim default toggle

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
        pstate.realOutputUID = uid;
        savePersistentState(pstate);
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

    // Phase 6: publish a parameter set to the render thread (write the
    // inactive slot, then bump the epoch). The render side crossfades.
    ctx.paramsSlots[0] = currentParams;
    auto publishParams = [&](const ChainParams& p) {
        uint32_t e = ctx.paramsEpoch.load(std::memory_order_relaxed);
        ctx.paramsSlots[(e + 1u) & 1u] = p;
        ctx.paramsEpoch.store(e + 1u, std::memory_order_release);
    };
    auto applyParams = [&](const ChainParams& p, const char* presetId) {
        currentParams = PresetValidator::clamp(p);
        std::snprintf(currentPreset, sizeof(currentPreset), "%s", presetId);
        publishParams(currentParams);
        ++paramsRevision;
        // Persist so the next engine (respawn/reboot/reinstall) resumes it.
        pstate.presetId   = currentPreset;
        pstate.paramsLine = (pstate.presetId == "custom")
            ? serializeParamsLine(currentParams) : std::string();
        // Parametric bands persist on their own line; a builtin preset that sets
        // them keeps them too, a flat bank writes nothing.
        pstate.parametricLine = serializeParametricLine(currentParams);
        savePersistentState(pstate);
    };

    watcher.install(&ctx);
    std::thread analysisThread(analysisLoop, &ctx);

    // Startup recovery (docs/05-recovery): if we come up with Roomcut as the
    // system default (typical after a crash — launchd respawned us under the
    // user's last selection) and the driver does not hand us audio within the
    // window, point the default back at a real device so sound returns. A
    // HELLO disarms this: when the driver is alive, Roomcut-as-default is
    // exactly the healthy production state.
    int  lastBypassToggles = 0;
    bool safeBypassLogged  = false;
    uint64_t lastWriteIndex   = 0;
    auto     lastWriteAdvance = std::chrono::steady_clock::now();
    auto     lastDriverBeat   = std::chrono::steady_clock::now(); // last HELLO/HEALTH_CHECK
    bool     driverStalled    = false;
    AudioDeviceID roomcutDev  = findRoomcutDevice(); // for system-volume mirroring
    float    lastMirroredScalar = -1.0f;             // edge-gate for the poll fallback
    setVolumeListener(roomcutDev, &ctx, true);       // instant volume response
    bool restoreArmed = false;
    auto restoreDeadline = std::chrono::steady_clock::now();
    if (isRoomcutDeviceUID(deviceUID(defaultOutputDevice()))) {
        restoreArmed = true;
        restoreDeadline += std::chrono::seconds(3);
        std::fprintf(stderr,
            "[engine] startup: default output is Roomcut and no driver yet; "
            "restoring a real device in 3 s unless the driver connects\n");
    }

    // Reopen the output on the policy-picked device after any device-world
    // change (default flipped, device un/plugged, SR changed). Idempotent: a
    // spurious event that resolves to the same device+rate is a no-op. On
    // failure the dirty flag is re-armed so the next 500 ms tick retries.
    auto recoverOutput = [&]() {
        if (!outputStarted || ringSR == 0) return;
        AudioDeviceID pick = pickOutput();
        if (pick == kAudioObjectUnknown) {
            std::fprintf(stderr,
                "[engine] device change: no usable real output; keeping current\n");
            return;
        }
        const bool sameDevice = (pick == output.deviceID());
        if (sameDevice && deviceNominalSampleRate(pick) == output.sampleRate()) {
            return; // nothing actually changed for us
        }
        std::fprintf(stderr, "[engine] device change: reopening output (%s)\n",
                     sameDevice ? "sample rate changed" : "target device changed");
        ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputLost),
                            std::memory_order_relaxed);
        output.stop();
        watcher.watchSR(kAudioObjectUnknown);
        output.close();

        OSStatus oerr = openOutputOn(ctx, output, pick, ringSR, currentParams);
        if (oerr == noErr) oerr = output.start();
        if (oerr != noErr) {
            std::fprintf(stderr,
                "[engine] output reopen failed: %d (retrying on next tick)\n", (int)oerr);
            ctx.devicesDirty.store(true, std::memory_order_relaxed);
            return;
        }
        watcher.watchSR(output.deviceID());
        setSavedReal(deviceUID(output.deviceID()));
        ctx.realOutputDevice.store(output.deviceID(), std::memory_order_relaxed);
        // While the driver feed is stalled the output alone doesn't make us
        // streaming; the watchdog's DriverReturned promotes when it resumes.
        if (!driverStalled) {
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
        if (ctx.devicesDirty.exchange(false, std::memory_order_relaxed)) {
            trackRealDefault();
            recoverOutput();
            AudioDeviceID rc = findRoomcutDevice(); // id may change on driver reload
            if (rc != roomcutDev) {
                setVolumeListener(roomcutDev, &ctx, false);
                setVolumeListener(rc, &ctx, true);
                roomcutDev = rc;
            }
            mirrorVolumeToReal(&ctx, roomcutDev); // output may have changed → re-apply
        }

        // The Roomcut-volume listener mirrors changes instantly; this poll is a
        // safety net (missed events / first tick) and is edge-gated so it never
        // fights the listener or re-writes the device every loop.
        {
            float v = roomcutMasterGain(roomcutDev);
            if (v != lastMirroredScalar) {
                lastMirroredScalar = v;
                mirrorVolumeToReal(&ctx, roomcutDev);
            }
        }

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
        if (RoomcutRingHeader* h = ctx.header.load(std::memory_order_acquire)) {
            uint64_t w = __atomic_load_n(&h->writeIndex, __ATOMIC_ACQUIRE);
            auto now = std::chrono::steady_clock::now();
            if (w != lastWriteIndex) {
                lastWriteIndex = w;
                lastWriteAdvance = now;
                if (driverStalled) {
                    driverStalled = false;
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
                }
            } else if (!driverStalled &&
                       now - lastWriteAdvance > std::chrono::seconds(2) &&
                       now - lastDriverBeat > std::chrono::milliseconds(3500)) {
                // 3500 ms ≈ three missed beats of the driver's ~1 s cadence.
                driverStalled = true;
                ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::DriverLost),
                                    std::memory_order_relaxed);
                std::fprintf(stderr, "[engine] driver feed stalled and heartbeat lost; "
                                     "rendering silence (wire: RECOVER)\n");
            }
        }

        if (kr == MACH_RCV_TIMED_OUT) {
            continue;
        }
        if (kr != KERN_SUCCESS) {
            std::fprintf(stderr, "[engine] recv error: %d\n", kr);
            continue;
        }

        switch (rx.hello.request.header.msgh_id) {
            case ROOMCUT_MSG_HELLO: {
                const RoomcutHelloRequest& req = rx.hello.request;
                std::fprintf(stderr, "[engine] HELLO v%u req sr=%u ch=%u cap=%u\n",
                             req.protocolVersion, req.requested.sampleRate,
                             req.requested.channels, req.requested.capacityFrames);

                ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::HelloReceived),
                                    std::memory_order_relaxed);
                lastDriverBeat = std::chrono::steady_clock::now();
                restoreArmed = false; // driver is alive — Roomcut-as-default is healthy

                // Negotiate: the engine is the authority on the format.
                uint32_t sr = roomcut_sr_supported(req.requested.sampleRate)
                                  ? req.requested.sampleRate
                                  : ROOMCUT_SR_48000;

                // Honor a sane requested capacity, else default. THEN ensure the
                // ring holds ~85 ms regardless of rate: the driver always asks
                // for 4096 frames, which is only ~21 ms at 192 kHz and starves
                // the consumer (observed underruns climbing at hi-res). Scale up
                // (pow2) so hi-res gets the same jitter cushion as 48 kHz.
                uint32_t cap = req.requested.capacityFrames;
                if (!roomcut_is_pow2(cap) || cap < 256 || cap > 65536) {
                    cap = ROOMCUT_DEFAULT_CAPACITY_FRAMES;
                }
                while (cap < sr / 12u && cap < 65536u) cap <<= 1; // ~85 ms floor, pow2

                if (region.valid()) {
                    // Reconnect (docs/05: driver re-handshakes after an engine
                    // or driver restart while we kept running): retire the old
                    // region safely before mapping a new one. After the
                    // unpublish, the render callback can hold the old header
                    // for at most one cycle — wait that out, then destroy.
                    // The resampler keeps its (by now silent) state; no reset
                    // is needed and touching it here would race the render
                    // thread.
                    std::fprintf(stderr, "[engine] reconnect: retiring previous ring region\n");
                    ctx.header.store(nullptr, std::memory_order_release);
                    usleep(20000);
                    ctx.ringPrimed.store(false, std::memory_order_relaxed);
                    region.destroy();
                }
                if (outputStarted && sr != ringSR) {
                    // Ring SR changed across reconnects (e.g. the system moved
                    // Roomcut to 96 kHz for hi-res content): REOPEN the output at
                    // the new rate so it stays bit-exact (openOutputOn switches
                    // the device to the ring rate → ratio 1.0). Just re-preparing
                    // the resampler at the old device rate would silently fall
                    // back to linear resampling and never resume STREAMING.
                    output.stop();
                    watcher.watchSR(kAudioObjectUnknown);
                    output.close();
                    AudioDeviceID pick = pickOutput();
                    OSStatus oerr = (pick == kAudioObjectUnknown)
                        ? (OSStatus)kAudioHardwareBadDeviceError
                        : openOutputOn(ctx, output, pick, sr, currentParams);
                    if (oerr == noErr) oerr = output.start();
                    if (oerr == noErr) {
                        watcher.watchSR(output.deviceID());
                        ctx.realOutputDevice.store(output.deviceID(), std::memory_order_relaxed);
                        mirrorVolumeToReal(&ctx, roomcutDev);
                        ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::OutputReopened),
                                            std::memory_order_relaxed);
                        std::fprintf(stderr, "[engine] reconnect at new ring sr=%u; output -> '%s' @ %.0f Hz\n",
                                     sr, deviceName(output.deviceID()).c_str(), output.sampleRate());
                    } else {
                        std::fprintf(stderr,
                            "[engine] reopen at new ring sr=%u failed: %d (retry on next tick)\n",
                            sr, (int)oerr);
                        ctx.devicesDirty.store(true, std::memory_order_relaxed);
                    }
                }

                if (region.create(cap, ROOMCUT_MVP_CHANNELS, sr) != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] region.create failed\n");
                    break;
                }

                RoomcutFormatNegotiation granted;
                std::memset(&granted, 0, sizeof(granted));
                granted.sampleRate     = sr;
                granted.channels       = ROOMCUT_MVP_CHANNELS;
                granted.channelLayout  = ROOMCUT_LAYOUT_STEREO;
                granted.internalFormat = ROOMCUT_INTERNAL_FORMAT_F32;
                granted.bufferFrameSize = req.requested.bufferFrameSize;
                granted.latencyClass   = req.requested.latencyClass;
                granted.capacityFrames = cap;

                // Open (but don't start) the real output device BEFORE replying:
                // the producer starts writing the moment the reply lands, and
                // AudioUnit open/initialize is slow enough (~100ms+) that an
                // after-reply open let the 4096-frame ring fill and drop the
                // first chunks (observed: dropped=2560 in the first audible
                // test). Prepare the DSP chain at the DEVICE rate (what we
                // actually output); a device/ring SR mismatch is handled by the
                // linear resampler (ring SR -> device SR, ratio <= 1 for MVP).
                ringSR = sr;
                bool outputReady = outputStarted;
                if (!outputStarted) {
                    // Phase 5: never render to Roomcut's own device — a pinned
                    // device wins, else the policy target (real default → saved
                    // real → builtin → any).
                    AudioDeviceID pick = pickOutput();
                    OSStatus oerr = (pick == kAudioObjectUnknown)
                        ? (OSStatus)kAudioHardwareBadDeviceError
                        : openOutputOn(ctx, output, pick, sr, currentParams);
                    if (oerr != noErr) {
                        std::fprintf(stderr, "[engine] output.open failed: %d (transfer ok, no audible out)\n",
                                     (int)oerr);
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
                ctx.header.store(region.header(), std::memory_order_release);
                bool startedNow = false;
                if (outputReady && !outputStarted) {
                    OSStatus oerr = output.start();
                    if (oerr != noErr) {
                        std::fprintf(stderr, "[engine] output.start failed: %d\n", (int)oerr);
                    } else {
                        outputStarted = true;
                        startedNow = true;
                    }
                }
                if (outputStarted) {
                    bool renderLive = false;
                    for (int i = 0; i < 200; ++i) {                    // <= 400 ms
                        if (ctx.framesRendered.load(std::memory_order_relaxed) > 0) {
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
                AudioDeviceID outDev = outputStarted ? output.deviceID() : pickOutput();
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
                    // Unpublish before destroying: the render callback may hold
                    // the old header for one more cycle.
                    ctx.header.store(nullptr, std::memory_order_release);
                    usleep(20000);
                    region.destroy();
                    break;
                }

                ctx.lifecycle.store(engineNext(ctx.lifecycle.load(), EngineEvent::RegionCreated),
                                    std::memory_order_relaxed);
                std::fprintf(stderr, "[engine] handed off region (sr=%u cap=%u)\n", sr, cap);

                if (startedNow) {
                    watcher.watchSR(output.deviceID());
                    setSavedReal(deviceUID(output.deviceID()));
                    ctx.realOutputDevice.store(output.deviceID(), std::memory_order_relaxed);
                    mirrorVolumeToReal(&ctx, roomcutDev); // apply current volume to the new device
                }
                if (outputStarted) {
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
                lastDriverBeat = std::chrono::steady_clock::now();
                RoomcutEngineState coarse = engineWireState(ctx.lifecycle.load());
                // §4.5 projection: bypass (manual or safe) overrides RUNNING.
                if (coarse == ROOMCUT_STATE_RUNNING &&
                    (ctx.safeBypass.load(std::memory_order_relaxed) ||
                     ctx.bypassRequested.load(std::memory_order_relaxed))) {
                    coarse = ROOMCUT_STATE_BYPASS;
                }
                // Carry the real output device's rates so the driver can correct
                // its nominal rate after a live device switch (which doesn't
                // re-HELLO) → keeps the ring at the device's native rate.
                uint32_t hbRates[ROOMCUT_MAX_RATES];
                AudioDeviceID hbDev = outputStarted ? output.deviceID() : pickOutput();
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
                if (outputStarted && kLogRender) {
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

            case ROOMCUT_MSG_SET_PRESET: {
                const RoomcutSetPresetRequest& preq = rx.control.setPreset;
                char id[ROOMCUT_PRESET_ID_MAX];
                std::memcpy(id, preq.presetId, sizeof(id));
                id[sizeof(id) - 1] = '\0';

                uint32_t status = 1; // unknown preset
                for (const auto& bp : builtinPresets()) {
                    if (bp.id == id) {
                        // The validator gate: builtins are authored in range,
                        // but every params set passes clamp() before the
                        // render thread sees it (docs/04, PresetValidator).
                        applyParams(bp.params, id);
                        status = 0;
                        std::fprintf(stderr, "[engine] preset -> %s\n", id);
                        break;
                    }
                }
                if (status != 0) {
                    std::fprintf(stderr, "[engine] preset '%s' unknown\n", id);
                }
                kr = controlReplyAck(preq.header, ROOMCUT_MSG_SET_PRESET, status);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] preset ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_SET_OUTPUT_DEV: {
                const RoomcutSetDeviceRequest& dreq = rx.control.setDevice;
                char uid[ROOMCUT_DEVICE_UID_MAX];
                std::memcpy(uid, dreq.deviceUID, sizeof(uid));
                uid[sizeof(uid) - 1] = '\0';
                preferredOutputUID = uid; // "" = back to automatic policy
                pstate.preferredOutputUID = preferredOutputUID;
                savePersistentState(pstate);
                std::fprintf(stderr, "[engine] output device pinned -> '%s'\n",
                             preferredOutputUID.empty() ? "(auto)" : preferredOutputUID.c_str());
                // Reopen on the new target via the control loop's recoverOutput.
                ctx.devicesDirty.store(true, std::memory_order_relaxed);
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
                pstate.keepRoomcutDefault = keepRoomcutDefault;
                savePersistentState(pstate);
                std::fprintf(stderr, "[engine] keep-default %s (ctl)\n",
                             keepRoomcutDefault ? "ON" : "OFF");
                // Apply immediately: if it's now on and the default has already
                // drifted off Roomcut, reclaim on the next device tick.
                if (keepRoomcutDefault) ctx.devicesDirty.store(true, std::memory_order_relaxed);
                kr = controlReplyAck(kreq.header, ROOMCUT_MSG_SET_KEEP_DEFAULT, 0);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] keep-default ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_SET_VOLUME_BOOST: {
                const RoomcutSetVolumeBoostRequest& vreq = rx.control.setVolumeBoost;
                const double boost = clampVolumeBoost(vreq.boost);
                ctx.volumeBoost.store((float)boost, std::memory_order_relaxed);
                pstate.volumeBoost = boost;
                savePersistentState(pstate);
                mirrorVolumeToReal(&ctx, roomcutDev);
                kr = controlReplyAck(vreq.header, ROOMCUT_MSG_SET_VOLUME_BOOST, 0);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] volume boost ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_SET_PARAMS: {
                const RoomcutSetParamsRequest& qreq = rx.control.setParams;
                ChainParams cp = ChainParams::flat();
                cp.preampDb         = qreq.preampDb;
                for (std::size_t b = 0; b < cp.eqGainsDb.size(); ++b) {
                    cp.eqGainsDb[b] = qreq.eqGainsDb[b];
                }
                cp.limiterReleaseMs = qreq.limiterReleaseMs;
                cp.outputGainDb     = qreq.outputGainDb;
                cp.spatialWidth     = qreq.spatialWidth;
                cp.centerFocus      = qreq.centerFocus;
                cp.crossfeed        = qreq.crossfeed;
                cp.roomReduce       = qreq.roomReduce;
                cp.spatialMode      = qreq.spatialMode;
                cp.highpassHz       = qreq.highpassHz;
                cp.compAmount       = qreq.compAmount;
                for (std::size_t b = 0; b < cp.parametric.size(); ++b) {
                    cp.parametric[b].enabled = qreq.parametric[b].enabled != 0;
                    cp.parametric[b].type    = (int)qreq.parametric[b].type;
                    cp.parametric[b].freqHz  = qreq.parametric[b].freqHz;
                    cp.parametric[b].gainDb  = qreq.parametric[b].gainDb;
                    cp.parametric[b].q       = qreq.parametric[b].q;
                }
                // Same validator gate as SET_PRESET — clamp before the render
                // thread sees it (the AI recommender will lean on this too).
                applyParams(cp, "custom");
                std::fprintf(stderr, "[engine] params (custom) applied (ctl)\n");
                kr = controlReplyAck(qreq.header, ROOMCUT_MSG_SET_PARAMS, 0);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] params ack failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_STATE: {
                const RoomcutStateRequest& sreq = rx.control.stateRequest;
                RoomcutStateReply rep;
                std::memset(&rep, 0, sizeof(rep));

                const bool manual = ctx.bypassRequested.load(std::memory_order_relaxed);
                const bool safe   = ctx.safeBypass.load(std::memory_order_relaxed);
                RoomcutEngineState coarse = engineWireState(ctx.lifecycle.load());
                if (coarse == ROOMCUT_STATE_RUNNING && (manual || safe)) {
                    coarse = ROOMCUT_STATE_BYPASS;
                }
                rep.state        = (uint32_t)coarse;
                rep.manualBypass = manual ? 1u : 0u;
                rep.safeBypass   = safe ? 1u : 0u;
                std::snprintf(rep.presetId, sizeof(rep.presetId), "%s", currentPreset);
                rep.paramsRevision = paramsRevision;

                uint32_t grBits = ctx.limiterGRBits.load(std::memory_order_relaxed);
                std::memcpy(&rep.limiterGainReductionDb, &grBits, sizeof(float));
                uint32_t pkBits = ctx.renderPeakBits.load(std::memory_order_relaxed);
                std::memcpy(&rep.renderPeak, &pkBits, sizeof(float));
                rep.framesRendered = ctx.framesRendered.load(std::memory_order_relaxed);
                rep.ringUnderruns  = ctx.renderUnderruns.load(std::memory_order_relaxed);
                if (outputStarted) {
                    std::snprintf(rep.outputDeviceUID, sizeof(rep.outputDeviceUID),
                                  "%s", deviceUID(output.deviceID()).c_str());
                }
                rep.keepDefault = keepRoomcutDefault ? 1u : 0u;
                rep.capabilities = ROOMCUT_CAP_SPATIAL_PARAMS
                    | ROOMCUT_CAP_PARAMETRIC
                    | ROOMCUT_CAP_ANALYZER
                    | ROOMCUT_CAP_VOLUME_BOOST
                    | ROOMCUT_CAP_DYNAMICS;
                rep.volumeBoost = ctx.volumeBoost.load(std::memory_order_relaxed);

                kr = controlReplyState(sreq, rep);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] state reply failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_GET_ANALYSIS: {
                const RoomcutAnalysisRequest& areq = rx.control.analysisRequest;
                ctx.analysisInterestUntilMs.store(
                    steadyMillis() + kAnalysisInterestTtlMs,
                    std::memory_order_release);
                RoomcutAnalysisReply rep;
                std::memset(&rep, 0, sizeof(rep));
                AnalysisSnapshot snap;
                {
                    std::lock_guard<std::mutex> lock(ctx.analysisMutex);
                    snap = ctx.latestAnalysis;
                }
                rep.valid = snap.valid ? 1u : 0u;
                rep.sampleRate = snap.sampleRate;
                rep.channels = snap.channels;
                rep.framesAnalyzed = snap.framesAnalyzed;
                rep.peakDb = snap.peakDb;
                rep.rmsDb = snap.rmsDb;
                rep.crestFactor = snap.crestFactor;
                rep.lowEnergy = snap.lowEnergy;
                rep.lowMidEnergy = snap.lowMidEnergy;
                rep.midEnergy = snap.midEnergy;
                rep.highEnergy = snap.highEnergy;
                rep.spectralCentroid = snap.spectralCentroid;
                rep.stereoWidth = snap.stereoWidth;
                rep.midSideRatio = snap.midSideRatio;
                rep.correlation = snap.correlation;
                rep.muddiness = snap.muddiness;
                rep.harshness = snap.harshness;
                rep.sibilance = snap.sibilance;
                rep.voicePresence = snap.voicePresence;
                rep.reverbEstimate = snap.reverbEstimate;
                rep.dynamicRange = snap.dynamicRange;
                static_assert(AnalysisSnapshot::kSpectrumBins == ROOMCUT_ANALYSIS_SPECTRUM_BINS,
                              "analysis spectrum bin count drifted from the wire protocol");
                for (std::size_t i = 0; i < snap.spectrum.size(); ++i) {
                    rep.spectrum[i] = snap.spectrum[i];
                }

                kr = controlReplyAnalysis(areq, rep);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] analysis reply failed: %d\n", kr);
                }
                break;
            }

            case ROOMCUT_MSG_GET_PARAMS: {
                const RoomcutGetParamsRequest& greq = rx.control.getParams;
                RoomcutGetParamsReply rep;
                std::memset(&rep, 0, sizeof(rep));
                std::snprintf(rep.presetId, sizeof(rep.presetId), "%s", currentPreset);
                rep.paramsRevision = paramsRevision;
                rep.preampDb = currentParams.preampDb;
                for (std::size_t b = 0; b < currentParams.eqGainsDb.size(); ++b) {
                    rep.eqGainsDb[b] = currentParams.eqGainsDb[b];
                }
                rep.limiterReleaseMs = currentParams.limiterReleaseMs;
                rep.outputGainDb = currentParams.outputGainDb;
                rep.spatialWidth = currentParams.spatialWidth;
                rep.centerFocus = currentParams.centerFocus;
                rep.crossfeed = currentParams.crossfeed;
                rep.roomReduce = currentParams.roomReduce;
                rep.spatialMode = currentParams.spatialMode;
                rep.highpassHz = currentParams.highpassHz;
                rep.compAmount = currentParams.compAmount;
                for (std::size_t b = 0; b < currentParams.parametric.size(); ++b) {
                    rep.parametric[b].enabled = currentParams.parametric[b].enabled ? 1u : 0u;
                    rep.parametric[b].type    = (uint32_t)currentParams.parametric[b].type;
                    rep.parametric[b].freqHz  = currentParams.parametric[b].freqHz;
                    rep.parametric[b].gainDb  = currentParams.parametric[b].gainDb;
                    rep.parametric[b].q       = currentParams.parametric[b].q;
                }

                kr = controlReplyParams(greq, rep);
                if (kr != KERN_SUCCESS) {
                    std::fprintf(stderr, "[engine] params reply failed: %d\n", kr);
                }
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
    if (analysisThread.joinable()) {
        analysisThread.join();
    }
    setVolumeListener(roomcutDev, &ctx, false);
    watcher.remove();
    // Stop the render callback BEFORE tearing down the ring it consumes.
    output.stop();
    output.close();
    ctx.header.store(nullptr, std::memory_order_release);

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

    mach_port_t self = mach_task_self();
    if (registered) {
        // Dev path: we created the receive right.
        mach_port_mod_refs(self, service, MACH_PORT_RIGHT_RECEIVE, -1);
    }
    return 0;
}
