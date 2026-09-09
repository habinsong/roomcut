#ifndef ROOMCUT_OUTPUT_ROUTER_HPP
#define ROOMCUT_OUTPUT_ROUTER_HPP

#include "OutputRecovery.hpp"
#include "RenderProgressWatchdog.hpp"

#include <chrono>
#include <cstdint>
#include <functional>

namespace roomcut {

// The order a render output is put back together: decide whether to move at all,
// take the old one down, open and start the new one, then restore the watcher,
// the volume target, the persisted device and the lifecycle. The HAL calls stay
// with the caller; this owns the sequence, the retry policy and the runaway
// watchdog, which is what makes the sequence testable without a device.
class OutputRouter {
public:
    using Clock = std::chrono::steady_clock;

    struct Devices {
        uint32_t target = 0;      // where the policy says the render should go
        uint32_t opened = 0;      // where it is now (0 when nothing is open)
        bool running = false;
        double sampleRate = 0;
        // The device's real rate, or 0 when it does not apply. The caller reads it
        // only for a device that is both the target and already running, because
        // asking a closed unit is meaningless.
        double hardwareRate = 0;
    };

    struct Operations {
        std::function<void(const char* reason)> lost;   // log + lifecycle: output going away
        std::function<void()> stop;
        std::function<void()> unwatchRate;
        std::function<void()> close;
        std::function<int(uint32_t device, bool matchRingRate)> open;   // 0 = opened
        std::function<int()> start;                                     // 0 = started
        std::function<void(int error)> failed;                          // log + close
        std::function<void(uint32_t device)> restored;  // watcher, volume, persistence, lifecycle
    };

    enum class Result { Idle, Reopened, Failed };

    // `ringValid` and `ringSR` describe the driver's feed: without one there is
    // nothing to render, so the retry timer is cleared rather than left armed.
    Result reopen(const Devices& devices, uint32_t ringSR, bool ringValid,
                  Clock::time_point now, bool force, const Operations& operations) {
        if (!ringValid || ringSR == 0) { recovery_.reset(); return Result::Idle; }
        const bool sameDevice = devices.target == devices.opened;
        const auto decision = recovery_.decide({devices.target, ringSR, devices.opened, devices.running,
                                                devices.sampleRate, devices.hardwareRate}, now, force);
        if (!decision.reopen) return Result::Idle;
        operations.lost(force ? "render runaway"
                              : (sameDevice ? "sample rate changed" : "target device changed"));
        operations.stop();
        operations.unwatchRate();
        operations.close();

        int error = operations.open(devices.target, decision.matchRingRate);
        if (error == 0) error = operations.start();
        const auto completedAt = Clock::now();
        recovery_.completed(completedAt);
        if (error != 0) {
            operations.failed(error);
            return Result::Failed;
        }
        watchdog_.opened(devices.target, completedAt);
        operations.restored(devices.target);
        return Result::Reopened;
    }

    // The output was opened outside a reopen — the driver's first handoff. Same
    // bookkeeping the reopen path does, without the teardown that did not happen.
    void adopt(uint32_t device, Clock::time_point now) {
        recovery_.reset();
        watchdog_.opened(device, now);
    }

    bool retryDue(Clock::time_point now) const { return recovery_.retryDue(now); }
    void reset() { recovery_.reset(); }

    // Runaway detection lives here so the reopen it asks for goes through the
    // same sequence as any other.
    RenderProgressWatchdog::Repair observeRender(uint64_t frames, double sampleRate, Clock::time_point now) {
        return watchdog_.observe(frames, sampleRate, now);
    }

    explicit OutputRouter(Clock::time_point now) : watchdog_(now) {}

private:
    OutputRecovery recovery_;
    RenderProgressWatchdog watchdog_;
};

} // namespace roomcut
#endif
