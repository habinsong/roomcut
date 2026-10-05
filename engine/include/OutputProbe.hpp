/*
 * OutputProbe.hpp — asks one real output whether it can run, before the render
 * path is moved onto it.
 *
 * The HAL lists outputs that cannot play: a display that advertises audio it has
 * no sink for, a dock with nothing behind its port. Whether a device is one of
 * them shows only when IO is started on it, and that start can block for the
 * HAL's whole IO-start timeout (10 s measured on macOS 27). Run on the control
 * thread, that stall costs the driver connection. So the check runs here instead:
 * a throwaway HAL output unit on a worker thread, a silent render callback, and
 * the answer is whether the callback ever fires. The device's rate and format are
 * left exactly as they are, and the engine's own output keeps playing meanwhile.
 */
#ifndef ROOMCUT_OUTPUT_PROBE_HPP
#define ROOMCUT_OUTPUT_PROBE_HPP

#include <CoreAudio/CoreAudio.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace roomcut {

class OutputProbe {
public:
    using Clock = std::chrono::steady_clock;

    // Past the HAL's own IO-start timeout (10 s measured), so the HAL decides: a
    // Bluetooth headset waking from standby can take seconds to start and must
    // not be called unusable for it. Nothing waits on the check meanwhile — the
    // current output keeps playing — so being generous costs only the switch.
    static constexpr auto kTimeout = std::chrono::seconds(12);

    struct Result {
        std::string uid;
        bool ran = false;
        int error = 0;
        std::chrono::milliseconds spent{0};
    };

    // One check at a time; false while one is in flight.
    bool start(AudioDeviceID device, const std::string& uid);
    // The finished or timed-out check, reported once. Control thread only.
    std::optional<Result> poll(Clock::time_point now = Clock::now());
    bool busy() const { return state_ != nullptr; }

private:
    struct State;
    std::shared_ptr<State> state_;
    std::string uid_;
    Clock::time_point startedAt_{};
};

} // namespace roomcut
#endif
