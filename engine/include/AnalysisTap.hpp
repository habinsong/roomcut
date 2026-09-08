#ifndef ROOMCUT_ANALYSIS_TAP_HPP
#define ROOMCUT_ANALYSIS_TAP_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "RealtimeMailbox.hpp"

namespace roomcut {

struct AnalysisWindow {
    static constexpr uint32_t kFrames = 2048;
    static constexpr uint32_t kChannels = 2;
    std::array<float, kFrames * kChannels> samples{};
    uint64_t endFrame = 0;
    uint64_t generation = 0;
    uint32_t sampleRate = 0;
};

// Audio-thread producer / analyzer-thread consumer. A slow analyzer receives
// the latest COMPLETE window; its samples and format are a single snapshot.
class AnalysisTap {
public:
    // Control thread, after output callbacks have been quiesced. The analyzer
    // may keep reading its own slot while the producer starts a new format.
    void prepare(uint32_t sampleRate) {
        rate_ = sampleRate;
        filled_ = 0;
        frames_ = 0;
        generation_.fetch_add(1, std::memory_order_release);
    }

    void push(const float* samples, uint32_t frames) {
        while (frames > 0) {
            auto& window = mailbox_.writable();
            const uint32_t count = std::min(frames, AnalysisWindow::kFrames - filled_);
            std::memcpy(window.samples.data() + filled_ * AnalysisWindow::kChannels, samples,
                        (size_t)count * AnalysisWindow::kChannels * sizeof(float));
            filled_ += count;
            frames_ += count;
            samples += count * AnalysisWindow::kChannels;
            frames -= count;
            if (filled_ == AnalysisWindow::kFrames) {
                window.endFrame = frames_;
                window.sampleRate = rate_;
                window.generation = generation_.load(std::memory_order_relaxed);
                mailbox_.publish();
                filled_ = 0;
            }
        }
    }

    bool readLatest(AnalysisWindow& window) { return mailbox_.readLatest(window); }
    uint64_t generation() const { return generation_.load(std::memory_order_acquire); }

private:
    RealtimeMailbox<AnalysisWindow> mailbox_;
    std::atomic<uint64_t> generation_{0};
    uint32_t rate_ = 0;
    uint32_t filled_ = 0;
    uint64_t frames_ = 0;
};

} // namespace roomcut
#endif
