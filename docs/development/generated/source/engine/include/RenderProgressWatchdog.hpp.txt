#ifndef ROOMCUT_RENDER_PROGRESS_WATCHDOG_HPP
#define ROOMCUT_RENDER_PROGRESS_WATCHDOG_HPP

#include <chrono>
#include <cmath>
#include <cstdint>

namespace roomcut {
// Detect runaway callback cadence, not silence or missing input. A reopened
// unit gets its own measurement window; repeated failed repairs are bounded.
class RenderProgressWatchdog {
public:
    using Clock = std::chrono::steady_clock;
    struct Repair { bool requested = false; double framesPerSecond = 0; unsigned attempt = 0; };
    explicit RenderProgressWatchdog(Clock::time_point now) : watchFrom_(now + std::chrono::seconds(5)) {}
    void opened(uint32_t device, Clock::time_point now) {
        if (device_ != device) { device_ = device; repairs_ = 0; }
        hits_ = 0; hasSample_ = false; watchFrom_ = now + std::chrono::seconds(3);
    }
    Repair observe(uint64_t frames, double sampleRate, Clock::time_point now) {
        if (!std::isfinite(sampleRate) || sampleRate <= 0) { hasSample_ = false; hits_ = 0; return {}; }
        if (now < watchFrom_ || !hasSample_ || frames < lastFrames_) {
            lastFrames_ = frames; sampledAt_ = now; hasSample_ = true; hits_ = 0;
            return {};
        }
        const double elapsed = std::chrono::duration<double>(now - sampledAt_).count();
        if (elapsed < 1.0) return {};
        const double rate = static_cast<double>(frames - lastFrames_) / elapsed;
        lastFrames_ = frames; sampledAt_ = now;
        if (rate <= sampleRate * 1.5) { hits_ = repairs_ = 0; return {}; }
        if (++hits_ < 2) return {};
        hits_ = 0;
        if (repairs_ >= 3) return {};
        return {true, rate, ++repairs_};
    }
private:
    uint32_t device_ = 0;
    uint64_t lastFrames_ = 0;
    unsigned hits_ = 0, repairs_ = 0;
    bool hasSample_ = false;
    Clock::time_point sampledAt_{}, watchFrom_;
};
} // namespace roomcut
#endif
