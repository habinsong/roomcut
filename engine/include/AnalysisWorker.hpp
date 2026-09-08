#ifndef ROOMCUT_ANALYSIS_WORKER_HPP
#define ROOMCUT_ANALYSIS_WORKER_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "AnalysisTap.hpp"
#include "dsp/Analyzer.hpp"

namespace roomcut {

// Owns analysis scheduling and the control-thread snapshot. Only push() runs
// on the audio thread; it never locks, allocates, or wakes a worker.
class AnalysisWorker {
public:
    ~AnalysisWorker() { stop(); }
    void start();
    void stop();
    void prepare(uint32_t sampleRate) { tap_.prepare(sampleRate); }
    void push(const float* samples, uint32_t frames) { tap_.push(samples, frames); }
    AnalysisSnapshot snapshot();

private:
    void run();
    void wait(uint32_t milliseconds);
    AnalysisTap tap_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> interestUntilMs_{0};
    std::thread thread_;
    std::mutex snapshotMutex_;
    AnalysisSnapshot latest_;
    uint64_t latestGeneration_ = 0;
    std::mutex waitMutex_;
    std::condition_variable wake_;
};

} // namespace roomcut
#endif
