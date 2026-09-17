// Build: clang++ -std=c++17 -O3 -DNDEBUG -Icore/dsp scripts/benchmark-dsp.cpp -o build/benchmark-dsp
// With AUSpatialMixer attached for the headphone bed, as the engine runs it:
//        clang++ -std=c++17 -O3 -DNDEBUG -DROOMCUT_BENCH_SYSTEM_BED -Icore -Icore/dsp -Iengine/include \
//          scripts/benchmark-dsp.cpp engine/src/SpatialMixerBedRenderer.cpp \
//          -framework AudioToolbox -framework CoreFoundation -o build/benchmark-dsp-system-bed
// Run: build/benchmark-dsp <sample-rate> <block-frames> <update-ms,0=steady> <full-effects,0|1>
//      [dynamic-bands] — optional, how many parametric bands run their dynamic side
//      [room-type]     — optional, virtual room 0=off 1=Studio 2=Living 3=Hall
//      [surround-type] — optional, upmix 0=off 2=5.1 3=7.1 (headphones)
//      [bed-renderer]  — optional, 0 = the attached system renderer, 1 = built-in bed
//                        (only differs in the ROOMCUT_BENCH_SYSTEM_BED build)
#include "DSPChain.hpp"
#ifdef ROOMCUT_BENCH_SYSTEM_BED
#include "SpatialMixerBedRenderer.hpp"
#include <string>
#endif
#ifdef ROOMCUT_BENCH_COMPARISON_MODE
#include "ComparisonProcessor.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <new>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <vector>
#ifdef __APPLE__
#include <pthread.h>
#endif
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

// Counts heap traffic on the render path. The chain must not allocate once it
// is prepared, whatever the parameters do. Only this thread counts: with
// AUSpatialMixer attached, CoreAudio's HAL property-listener queue allocates on
// its own thread whenever a device property changes, and that is not ours.
static bool recordingAllocations = false;
static std::size_t allocatedBytes = 0;
#ifdef __APPLE__
static pthread_t renderThread = nullptr;
#endif
void* operator new(std::size_t bytes) {
#ifdef __APPLE__
    if (recordingAllocations && pthread_equal(pthread_self(), renderThread)) allocatedBytes += bytes;
#else
    if (recordingAllocations) allocatedBytes += bytes;
#endif
    if (auto* pointer = std::malloc(bytes ? bytes : 1)) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }

int main(int argc, char** argv) {
    if (argc < 5 || argc > 9) return 64;
    const double fs = std::atof(argv[1]);
    const auto block = static_cast<std::size_t>(std::atoi(argv[2]));
    const double updateMs = std::atof(argv[3]);
    const bool full = std::atoi(argv[4]) != 0;
    const int dynamicBands = argc >= 6 ? std::atoi(argv[5]) : 0;
    const int roomType = argc >= 7 ? std::atoi(argv[6]) : 0;
    const int surroundType = argc >= 8 ? std::atoi(argv[7]) : 0;
    const int bedRenderer = argc >= 9 ? std::atoi(argv[8]) : 0;
    if (!(fs >= 44100 && fs <= 768000) || block == 0 || block > 8192 || updateMs < 0) return 64;
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    renderThread = pthread_self();
#endif
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
#elif defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif

    roomcut::ChainParams params;
    if (full) {
        params.preampDb = -12;
        params.highpassHz = 45;
        params.spatialWidth = 35;
        params.crossfeed = 25;
        params.spatialMode = 1;
        params.compAmount = 30;
        for (std::size_t b = 0; b < params.eqGainsDb.size(); ++b)
            params.eqGainsDb[b] = b % 2 ? 3 : -3;
        for (std::size_t b = 0; b < params.parametric.size(); ++b)
            params.parametric[b] = {true, 0, 100.0 * std::pow(2.0, b), 3, 1};
    }
    if (roomType > 0) {
        params.spatialMode = 1;   // the virtual room is headphone-only
        params.roomType = roomType;
        params.roomAmount = 50;
    }
    if (surroundType >= 2) {
        params.spatialMode = 1;   // the upmix renders virtual speakers, headphones only
        params.surroundType = surroundType;
    }
    params.bedRenderer = bedRenderer != 0 ? 1.0 : 0.0;
#ifdef ROOMCUT_BENCH_SYSTEM_BED
    roomcut::SpatialMixerBedRenderer bedCurrent, bedReference;
    {
        std::string error;
        if (!bedCurrent.prepare(fs, error) || !bedReference.prepare(fs, error)) {
            std::fprintf(stderr, "AUSpatialMixer did not open: %s\n", error.c_str());
            return 69;
        }
    }
#endif
    for (int b = 0; b < dynamicBands && b < (int)params.parametric.size(); ++b) {
        auto& band = params.parametric[b];
        band.enabled = true;
        band.type = 0;
        if (!full) { band.freqHz = 100.0 * std::pow(2.0, b); band.q = 1; band.gainDb = 3; }
        band.dynamic = true;
        band.thresholdDb = -30;
        band.rangeDb = 9;
        band.attackMs = 20;
        band.releaseMs = 200;
    }
#ifdef ROOMCUT_BENCH_COMPARISON_MODE
    roomcut::ComparisonProcessor chain;
#ifdef ROOMCUT_BENCH_SYSTEM_BED
    chain.attachBedRenderers(&bedCurrent, &bedReference);
#endif
    roomcut::ComparisonSettings comparison{params, params, ROOMCUT_BENCH_COMPARISON_MODE != 0, 1};
    comparison.reference.preampDb -= 6;
    chain.prepare(fs, 2, comparison);
    if (ROOMCUT_BENCH_COMPARISON_MODE == 2) chain.setBypass(true);
#else
    roomcut::DSPChain chain;
#ifdef ROOMCUT_BENCH_SYSTEM_BED
    chain.attachBedRenderer(&bedCurrent);
#endif
    chain.prepare(fs);
    chain.setParams(params);
    chain.reset();
#endif
    std::vector<float> source(block * 2), output(block * 2);
    for (std::size_t f = 0; f < block; ++f) {
        source[2 * f] = 0.2f * std::sin(2 * M_PI * 997 * f / fs);
        source[2 * f + 1] = 0.2f * std::cos(2 * M_PI * 601 * f / fs);
    }
    for (std::size_t n = 0; n < static_cast<std::size_t>(fs / block); ++n) {
        output = source;
        chain.processInterleaved(output.data(), block);
    }
    const std::size_t blocks = (262144 + block - 1) / block;
    const auto updateBlocks = std::max<std::size_t>(1, std::lround(updateMs * 0.001 * fs / block));
    std::vector<double> costs;
    costs.reserve(blocks);
    double total = 0, checksum = 0;
    const auto threadTime = [] {
        timespec value{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) std::abort();
        return value.tv_sec * 1e9 + value.tv_nsec;
    };
    recordingAllocations = true;
    allocatedBytes = 0;
    const double cpuStart = threadTime();
    for (std::size_t n = 0; n < blocks; ++n) {
        output = source;
        const auto start = std::chrono::steady_clock::now();
        if (updateMs > 0 && n % updateBlocks == 0) {
            params.eqGainsDb[5] = params.eqGainsDb[5] > 0 ? -3 : 3;
            params.parametric[0].freqHz = params.parametric[0].freqHz == 100 ? 150 : 100;
#ifdef ROOMCUT_BENCH_COMPARISON_MODE
            comparison.current = params;
            ++comparison.revision;
            chain.setComparison(comparison);
#else
            chain.setParams(params);
#endif
        }
        chain.processInterleaved(output.data(), block);
        const auto end = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(end - start).count();
        total += ns;
        costs.push_back(ns);
        checksum += output.back();
    }
    const double cpuCost = threadTime() - cpuStart;
    recordingAllocations = false;
    std::sort(costs.begin(), costs.end());
    std::printf("%.0f,%zu,%.3f,%d,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%.9f\n",
                fs, block, updateMs, full, total / (blocks * block), cpuCost / (blocks * block),
                costs[static_cast<std::size_t>(blocks * 0.99)] / 1000,
                costs.back() / 1000, sizeof(chain), allocatedBytes, checksum);
}
