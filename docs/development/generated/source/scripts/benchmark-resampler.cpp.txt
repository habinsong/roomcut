// clang++ -std=c++17 -O3 -DNDEBUG -Iengine/include scripts/benchmark-resampler.cpp -o build/benchmark-resampler
// Add -DROOMCUT_BENCH_CUBIC to measure the former engine implementation.
// Arguments: input-rate output-rate block-frames
#ifdef ROOMCUT_BENCH_CUBIC
#include "CubicResampler.hpp"
using Resampler = roomcut::CubicResampler;
#else
#include "SincResampler.hpp"
using Resampler = roomcut::SincResampler;
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <new>
#include <vector>
#ifdef __APPLE__
#include <pthread.h>
#endif

static bool recordingAllocations = false;
static std::size_t allocatedBytes = 0;
void* operator new(std::size_t bytes) {
    if (recordingAllocations) allocatedBytes += bytes;
    if (auto* pointer = std::malloc(bytes ? bytes : 1)) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }

static uint32_t source(void*, float* out, uint32_t frames, uint32_t channels) {
    std::fill_n(out, static_cast<std::size_t>(frames) * channels, 0.25f);
    return frames;
}

static double threadTime() {
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) std::abort();
    return value.tv_sec * 1e9 + value.tv_nsec;
}

int main(int argc, char** argv) {
    if (argc != 4) return 64;
    const double inputRate = std::atof(argv[1]), outputRate = std::atof(argv[2]);
    const auto block = static_cast<uint32_t>(std::atoi(argv[3]));
    if (!(inputRate >= 8000 && inputRate <= 1536000) || !(outputRate >= 8000 && outputRate <= 1536000)
        || block == 0 || block > 8192) return 64;
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    Resampler resampler;
    recordingAllocations = true;
    const double preparationStart = threadTime();
    resampler.prepare(inputRate, outputRate, 2);
    const double preparationNs = threadTime() - preparationStart;
    recordingAllocations = false;
    const auto scratchFrames = static_cast<uint32_t>(std::ceil(block * resampler.ratio())) + 8;
    std::vector<float> output(block * 2), scratch(scratchFrames * 2);
    for (uint32_t n = 0; n < static_cast<uint32_t>(outputRate * 0.02 / block) + 1; ++n)
        resampler.produce(output.data(), block, source, nullptr, scratch.data(), scratchFrames);
    const auto blocks = static_cast<uint32_t>(std::ceil(outputRate * 0.2 / block));
    std::vector<double> times;
    times.reserve(blocks);
    double checksum = 0;
    const double start = threadTime();
    for (uint32_t n = 0; n < blocks; ++n) {
        const auto begin = std::chrono::steady_clock::now();
        resampler.produce(output.data(), block, source, nullptr, scratch.data(), scratchFrames);
        times.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
        for (float sample : output) checksum += sample;
    }
    const double cpuNs = threadTime() - start;
    std::sort(times.begin(), times.end());
    std::printf("%.3f,%.3f,%u,%.3f,%.3f,%.3f,%zu,%.9f\n", inputRate, outputRate, block,
                cpuNs / (blocks * block), times[static_cast<std::size_t>(blocks * 0.99)],
                preparationNs / 1e6, allocatedBytes, checksum);
}
