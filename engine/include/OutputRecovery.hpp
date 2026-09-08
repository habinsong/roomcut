#ifndef ROOMCUT_OUTPUT_RECOVERY_HPP
#define ROOMCUT_OUTPUT_RECOVERY_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

namespace roomcut {

// Control-thread policy only. Time and observed HAL state are explicit inputs;
// choosing a recovery never touches hardware, DSP state or persistence.
class OutputRecovery {
public:
    using Clock = std::chrono::steady_clock;
    struct Snapshot {
        uint32_t targetDevice = 0;
        uint32_t ringRate = 0;
        uint32_t openedDevice = 0;
        bool running = false;
        double configuredRate = 0;
        double hardwareRate = 0;
    };
    struct Decision { bool reopen = false; bool matchRingRate = true; };

    Decision decide(const Snapshot& output, Clock::time_point now, bool force = false) {
        if (!output.ringRate) { reset(); return {}; }
        if (target_ != output.targetDevice || ringRate_ != output.ringRate) {
            reset(); target_ = output.targetDevice; ringRate_ = output.ringRate;
        }
        if (!target_) {
            pending_ = true; nextCheck_ = now + std::chrono::milliseconds(500);
            return {};
        }
        const bool stable = output.running && output.openedDevice == target_ &&
            std::isfinite(output.hardwareRate) && output.hardwareRate > 0 &&
            output.hardwareRate == output.configuredRate;
        if (stable && !force) {
            if (attempts_ && !stableSince_) stableSince_ = now;
            if (!attempts_ || now - *stableSince_ >= std::chrono::seconds(2)) {
                attempts_ = 0; pending_ = false;
            } else {
                pending_ = true; nextCheck_ = now + std::chrono::milliseconds(500);
            }
            return {};
        }
        stableSince_.reset();
        if (!force && pending_ && now < nextCheck_) return {};
        const bool match = attempts_ < 2;
        attempts_ = std::min(attempts_ + 1u, 5u);
        completed(now);
        return {true, match};
    }

    // Apply the delay from completion, so a slow HAL open cannot consume it.
    // Failed attempts count too; a later query of a configured-but-stopped unit
    // must never be mistaken for a healthy output.
    void completed(Clock::time_point now) {
        pending_ = true;
        nextCheck_ = now + (attempts_ >= 5 ? std::chrono::milliseconds(5000)
            : attempts_ >= 3 ? std::chrono::milliseconds(2000) : std::chrono::milliseconds(500));
    }
    bool retryDue(Clock::time_point now) const { return pending_ && now >= nextCheck_; }
    void reset() {
        target_ = ringRate_ = attempts_ = 0;
        pending_ = false; stableSince_.reset(); nextCheck_ = {};
    }
private:
    uint32_t target_ = 0, ringRate_ = 0;
    unsigned attempts_ = 0;
    bool pending_ = false;
    Clock::time_point nextCheck_{};
    std::optional<Clock::time_point> stableSince_;
};
} // namespace roomcut
#endif
