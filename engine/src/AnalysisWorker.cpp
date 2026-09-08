#include "AnalysisWorker.hpp"
#include <chrono>

namespace roomcut {
namespace {
int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
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

} // namespace

void AnalysisWorker::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&AnalysisWorker::run, this);
}

void AnalysisWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        running_.store(false, std::memory_order_release);
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void AnalysisWorker::wait(uint32_t milliseconds) {
    std::unique_lock<std::mutex> lock(waitMutex_);
    wake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] {
        return !running_.load(std::memory_order_acquire);
    });
}

AnalysisSnapshot AnalysisWorker::snapshot() {
    interestUntilMs_.store(nowMillis() + 2500, std::memory_order_release);
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return latestGeneration_ == tap_.generation() ? latest_ : AnalysisSnapshot{};
}

void AnalysisWorker::run() {
    Analyzer analyzer;
    AnalysisWindow window;
    AnalysisSnapshot previous;
    uint64_t previousGeneration = 0;
    while (running_.load(std::memory_order_acquire)) {
        if (interestUntilMs_.load(std::memory_order_acquire) < nowMillis()
            || !tap_.readLatest(window) || window.sampleRate == 0
            || window.generation != tap_.generation()) {
            wait(100);
            continue;
        }
        AnalysisSnapshot next = analyzer.analyzeInterleaved(
            window.samples.data(), AnalysisWindow::kFrames, AnalysisWindow::kChannels,
            (double)window.sampleRate);
        next.framesAnalyzed = window.endFrame;
        if (next.valid && previous.valid && window.generation == previousGeneration) {
            smoothAnalysis(next, previous);
        }
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            latest_ = next;
            latestGeneration_ = window.generation;
        }
        previous = next;
        previousGeneration = window.generation;
        wait(500);
    }
}

} // namespace roomcut
