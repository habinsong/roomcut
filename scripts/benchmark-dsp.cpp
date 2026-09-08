// Build: clang++ -std=c++17 -O3 -DNDEBUG -Icore/dsp scripts/benchmark-dsp.cpp -o build/benchmark-dsp
// Run: build/benchmark-dsp <sample-rate> <block-frames> <update-ms,0=steady> <full-effects,0|1>
#include "DSPChain.hpp"
#ifdef ROOMCUT_BENCH_COMPARISON_MODE
#include "ComparisonProcessor.hpp"
#endif

#include <algorithm>
#include <chrono>
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

int main(int argc, char** argv) {
    if (argc != 5) return 64;
    const double fs = std::atof(argv[1]);
    const auto block = static_cast<std::size_t>(std::atoi(argv[2]));
    const double updateMs = std::atof(argv[3]);
    const bool full = std::atoi(argv[4]) != 0;
    if (!(fs >= 44100 && fs <= 768000) || block == 0 || block > 8192 || updateMs < 0) return 64;
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
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
#ifdef ROOMCUT_BENCH_COMPARISON_MODE
    roomcut::ComparisonProcessor chain;
    roomcut::ComparisonSettings comparison{params, params, ROOMCUT_BENCH_COMPARISON_MODE != 0, 1};
    comparison.reference.preampDb -= 6;
    chain.prepare(fs, 2, comparison);
    if (ROOMCUT_BENCH_COMPARISON_MODE == 2) chain.setBypass(true);
#else
    roomcut::DSPChain chain;
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
    std::sort(costs.begin(), costs.end());
    std::printf("%.0f,%zu,%.3f,%d,%.3f,%.3f,%.3f,%.3f,%zu,%.9f\n",
                fs, block, updateMs, full, total / (blocks * block), cpuCost / (blocks * block),
                costs[static_cast<std::size_t>(blocks * 0.99)] / 1000,
                costs.back() / 1000, sizeof(chain), checksum);
}
