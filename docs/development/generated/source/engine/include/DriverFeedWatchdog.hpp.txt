#ifndef ROOMCUT_DRIVER_FEED_WATCHDOG_HPP
#define ROOMCUT_DRIVER_FEED_WATCHDOG_HPP

#include <chrono>
#include <cstdint>

namespace roomcut {

// Is the driver still feeding us, or is it gone? A frozen write index alone
// only means silence — nothing is playing, or the default moved elsewhere — so
// the driver counts as lost only when the feed is frozen AND its ~1 s heartbeat
// went quiet. Time and the observed write index are inputs; this owns no ring,
// no device and no lifecycle.
class DriverFeedWatchdog {
public:
    using Clock = std::chrono::steady_clock;
    enum class Event { None, Lost, Returned };

    explicit DriverFeedWatchdog(Clock::time_point now) : advancedAt_(now), beatAt_(now) {}

    // A HELLO or HEALTH_CHECK message from the driver.
    void heartbeat(Clock::time_point now) { beatAt_ = now; }

    // Call once per control-loop pass, only while a ring is published.
    Event observe(uint64_t writeIndex, Clock::time_point now) {
        if (writeIndex != writeIndex_) {
            writeIndex_ = writeIndex;
            advancedAt_ = now;
            if (!stalled_) return Event::None;
            stalled_ = false;
            return Event::Returned;
        }
        if (stalled_) return Event::None;
        // 3500 ms ≈ three missed beats of the driver's ~1 s cadence.
        if (now - advancedAt_ <= kFeedIdle || now - beatAt_ <= kBeatLost) return Event::None;
        stalled_ = true;
        return Event::Lost;
    }

    bool stalled() const { return stalled_; }

private:
    static constexpr std::chrono::seconds kFeedIdle{2};
    static constexpr std::chrono::milliseconds kBeatLost{3500};
    uint64_t writeIndex_ = 0;
    Clock::time_point advancedAt_, beatAt_;
    bool stalled_ = false;
};

} // namespace roomcut
#endif
