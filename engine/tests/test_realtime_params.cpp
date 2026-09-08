#include "RealtimeParams.hpp"
#include <cstdio>
#include <thread>

using namespace roomcut;

static ChainParams makeCurve(double value) {
    ChainParams params;
    params.preampDb = value;
    params.outputGainDb = value;
    params.eqGainsDb.fill(value);
    for (auto& band : params.parametric) {
        band.freqHz = value;
        band.gainDb = value;
        band.q = value;
    }
    return params;
}

static bool coherentCurve(const ChainParams& params) {
    const double value = params.preampDb;
    if (params.outputGainDb != value) return false;
    for (double gain : params.eqGainsDb) if (gain != value) return false;
    for (const auto& band : params.parametric) {
        if (band.freqHz != value || band.gainDb != value || band.q != value) return false;
    }
    return true;
}

static ComparisonSettings makeParams(double value) {
    return {makeCurve(value), makeCurve(-value), static_cast<int>(value) % 2 != 0, static_cast<uint64_t>(value)};
}

static bool coherent(const ComparisonSettings& value) {
    return coherentCurve(value.current) && coherentCurve(value.reference)
        && value.current.preampDb == -value.reference.preampDb
        && value.revision == value.current.preampDb && value.enabled == (value.revision % 2 != 0);
}

int main() {
    RealtimeParams exchange;
    ComparisonSettings result;
    if (exchange.readLatest(result)) return 1;
    for (int i = 1; i <= 1000; ++i) exchange.publish(makeParams(i));
    if (!exchange.readLatest(result) || result.current.preampDb != 1000 || !coherent(result)) return 1;
    if (exchange.readLatest(result)) return 1;

    constexpr int iterations = 200000;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 1001; i <= iterations; ++i) exchange.publish(makeParams(i));
        done.store(true, std::memory_order_release);
    });
    bool valid = true;
    double previous = result.current.preampDb;
    do {
        if (exchange.readLatest(result)) {
            valid &= coherent(result) && result.current.preampDb >= previous;
            previous = result.current.preampDb;
        }
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    if (exchange.readLatest(result)) valid &= coherent(result);
    valid &= result.current.preampDb == iterations;
    std::puts(valid ? "all realtime parameter tests passed" : "FAIL: torn or lost parameter snapshot");
    return valid ? 0 : 1;
}
