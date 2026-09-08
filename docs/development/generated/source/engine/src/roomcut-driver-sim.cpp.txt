/*
 * roomcut-driver-sim.cpp — a standalone driver-side simulator for verifying the
 * Phase 3 transfer across a REAL process boundary.
 *
 * The actual producer is Roomcut.driver inside coreaudiod, which cannot be run
 * as a normal process. This tool stands in for it OUTSIDE the sandbox so we can
 * prove the cross-process half the in-process unit tests can't reach:
 *   - bootstrap_look_up(com.roomcut.engine) finds the engine's registered service
 *   - driverSendHelloAndReceive maps the engine's memory entry into a DIFFERENT
 *     process's address space (a real port-descriptor transfer across tasks)
 *   - frames written here are seen by the engine's reader thread
 *   - heartbeatProbe round-trips against the live engine
 *
 * It generates a sine ramp so the engine's RMS/peak prints prove parity. Not
 * shipped — a developer verification tool (built only with ROOMCUT_BUILD_TESTS).
 */
#include "Handshake.hpp"
#include "Heartbeat.hpp"
#include "RingRegion.hpp"

#include "dsp/GraphicEQ.hpp" // kCenters: --tones puts one sine on each EQ band

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

#include <mach/mach.h>

#include <bootstrap.h>

extern "C" {
#include "roomcut_handshake.h"
#include "roomcut_ring.h"
}

using namespace roomcut;

namespace {

std::atomic<bool> g_running{true};
void handleSignal(int) { g_running.store(false, std::memory_order_relaxed); }

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Args: [seconds] [--tones] [--stereo]. seconds default 5, 0 = until signalled.
    // --tones swaps the 440 Hz sine for one sine per GraphicEQ band center so
    // an EQ curve is measurable per band from the engine's --dump capture
    // (scripts/analyze-dump.py --tones eq10).
    // --stereo writes a 440 Hz tone ANTI-PHASE across L/R (R = -L): mid is zero,
    // so the signal is pure SIDE. Spatial width/room then scale the whole output
    // amplitude (sideGain), making the Phase 7 mid/side stage measurable from the
    // capture peak alone (scripts/analyze-dump.py --stereo). Mutually exclusive
    // with --tones; --stereo wins if both are given.
    double seconds = 5.0;
    bool tones = false;
    bool stereo = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tones") == 0) tones = true;
        else if (std::strcmp(argv[i], "--stereo") == 0) stereo = true;
        else seconds = std::atof(argv[i]);
    }

    // 1. Look up the engine's service in our bootstrap namespace.
    mach_port_t bp = MACH_PORT_NULL;
    if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS) {
        std::fprintf(stderr, "[driver-sim] no bootstrap port\n");
        return 1;
    }
    mach_port_t service = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_look_up(bp, ROOMCUT_MACH_SERVICE_NAME, &service);
    if (kr != KERN_SUCCESS || service == MACH_PORT_NULL) {
        std::fprintf(stderr, "[driver-sim] bootstrap_look_up('%s') failed: %s\n",
                     ROOMCUT_MACH_SERVICE_NAME, bootstrap_strerror(kr));
        return 1;
    }
    std::fprintf(stderr, "[driver-sim] found engine service\n");

    // 2. HELLO + receive the memory entry, mapping it into THIS process.
    RoomcutFormatNegotiation requested;
    std::memset(&requested, 0, sizeof(requested));
    requested.sampleRate     = ROOMCUT_SR_48000;
    requested.channels       = ROOMCUT_MVP_CHANNELS;
    requested.channelLayout  = ROOMCUT_LAYOUT_STEREO;
    requested.internalFormat = ROOMCUT_INTERNAL_FORMAT_F32;
    requested.capacityFrames = 4096;

    RingRegion region;
    RoomcutFormatNegotiation granted;
    std::memset(&granted, 0, sizeof(granted));
    kr = driverSendHelloAndReceive(service, requested, region, &granted);
    if (kr != KERN_SUCCESS || !region.valid()) {
        std::fprintf(stderr, "[driver-sim] handshake failed: %d\n", kr);
        return 1;
    }
    std::fprintf(stderr, "[driver-sim] handshake OK; granted sr=%u ch=%u cap=%u; ring mapped cross-process\n",
                 granted.sampleRate, granted.channels, granted.capacityFrames);

    // 3. Stream a sine ramp. Write in driver-callback-sized chunks at the SR
    //    cadence so the engine sees a realistic producer.
    const uint32_t sr = granted.sampleRate ? granted.sampleRate : ROOMCUT_SR_48000;
    const uint32_t ch = granted.channels ? granted.channels : ROOMCUT_MVP_CHANNELS;
    constexpr uint32_t kFrames = 512;
    roomcut_sample_t chunk[kFrames * ROOMCUT_MVP_CHANNELS];

    const double freq = 440.0;
    const double twoPiF = 2.0 * M_PI * freq / static_cast<double>(sr);
    double phase = 0.0;
    uint64_t genFrame = 0; // absolute frame index (phase source for --tones)
    uint64_t totalWritten = 0;
    uint64_t totalDropped = 0;

    // 0.05 per tone keeps the worst-case sum (10 x 0.05 = 0.5, plus headroom
    // for ±6 dB test curves) under the limiter's -1 dB ceiling, so EQ
    // measurements aren't colored by limiting.
    constexpr double kToneAmp = 0.05;

    const uint64_t targetFrames = seconds > 0.0
        ? static_cast<uint64_t>(seconds * sr) : 0;

    auto fillChunk = [&] {
        for (uint32_t f = 0; f < kFrames; ++f) {
            if (stereo) {
                // Anti-phase 440 Hz: L = +s, R = -s → pure side, zero mid.
                const float s = static_cast<float>(0.25 * std::sin(phase));
                phase += twoPiF;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
                for (uint32_t c = 0; c < ch; ++c) {
                    chunk[f * ch + c] = (c % 2 == 0) ? s : -s;
                }
                continue;
            }
            float s;
            if (tones) {
                const double t = static_cast<double>(genFrame + f) / sr;
                double acc = 0.0;
                for (double fc : GraphicEQ::kCenters) {
                    acc += kToneAmp * std::sin(2.0 * M_PI * fc * t);
                }
                s = static_cast<float>(acc);
            } else {
                s = static_cast<float>(0.25 * std::sin(phase));
                phase += twoPiF;
                if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
            }
            for (uint32_t c = 0; c < ch; ++c) chunk[f * ch + c] = s;
        }
        genFrame += kFrames;
    };

    // Prefill half the ring unpaced so the consumer starts with a jitter
    // cushion and the steady-state fill hovers at ~cap/2 (headroom against both
    // overrun and underrun) instead of near empty.
    const uint32_t cap = granted.capacityFrames ? granted.capacityFrames : 4096;
    for (uint32_t pre = 0; pre + kFrames <= cap / 2; pre += kFrames) {
        fillChunk();
        uint32_t wrote = roomcut_ring_write(region.header(), chunk, kFrames, 0);
        if (wrote == kFrames) totalWritten += kFrames;
        else                  totalDropped += kFrames;
    }

    // Paced loop on ABSOLUTE deadlines (sleep_until from a fixed origin), not
    // relative sleep_for: sleep_for's per-iteration oversleep (kernel timer
    // leeway) accumulates and starved the consumer ~10% in the first audible
    // test — engine logged underruns=25856, heard as rapid gaps/crackle. Each
    // deadline is computed from the total paced frame count, so rounding error
    // never accumulates either.
    const auto origin = std::chrono::steady_clock::now();
    uint64_t pacedFrames = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        fillChunk();
        uint32_t wrote = roomcut_ring_write(region.header(), chunk, kFrames, 0);
        if (wrote == kFrames) totalWritten += kFrames;
        else                  totalDropped += kFrames;

        if (targetFrames && totalWritten >= targetFrames) break;
        pacedFrames += kFrames;
        std::this_thread::sleep_until(origin + std::chrono::nanoseconds(
            pacedFrames * 1000000000ull / sr));
    }

    // 4. Probe the engine's liveness over the same service port.
    uint32_t peerState = 0xffffffffu;
    kr = heartbeatProbe(service, 1, 500, &peerState);
    std::fprintf(stderr, "[driver-sim] heartbeat: %s peerState=%u\n",
                 kr == KERN_SUCCESS ? "alive" : "no reply", peerState);

    std::fprintf(stderr, "[driver-sim] done: wrote=%llu dropped=%llu over=%llu\n",
                 static_cast<unsigned long long>(totalWritten),
                 static_cast<unsigned long long>(totalDropped),
                 static_cast<unsigned long long>(
                     __atomic_load_n(&region.header()->overruns, __ATOMIC_RELAXED)));

    region.destroy();
    mach_port_deallocate(mach_task_self(), service);
    return 0;
}
