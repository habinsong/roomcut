#ifndef ROOMCUT_OUTPUT_QUARANTINE_HPP
#define ROOMCUT_OUTPUT_QUARANTINE_HPP

#include <chrono>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace roomcut {

// What the engine has learned this session about each real output: it ran, or it
// would not start. A device is judged only by what it did, never by its name or
// transport. The HAL lists outputs that cannot play (2026-10-05, macOS 27: a
// nameless DisplayPort sink held AudioOutputUnitStart for 10 s and failed with
// 'stop'), and every in-place attempt runs on the control thread, long enough for
// the driver to retire the connection and for the app to lose the engine. A
// benched device is left out of routing until it is picked again or replugged.
// Pure policy; the caller passes the time an attempt took.
class OutputQuarantine {
public:
    using Clock = std::chrono::steady_clock;

    // A failed attempt that blocked this long is the HAL waiting out its own
    // IO-start timeout, not a device still settling, and it has already outlasted
    // the driver's heartbeat budget (3 x 500 ms). One is enough to stop asking.
    static constexpr auto kHungStart = std::chrono::seconds(3);
    // Quick failures are given the normal retries before the device is skipped.
    static constexpr unsigned kQuickFailures = 3;

    struct Verdict {
        bool benched = false; // skip this device until it is re-picked or replugged
        bool hung = false;    // the attempt itself cost the connection
    };

    // A routing attempt (open + start) on the control thread failed.
    Verdict failed(const std::string& uid, Clock::duration spent) {
        if (uid.empty()) return {};
        Entry& entry = entries_[uid];
        entry.verified = false;
        entry.failures += 1;
        const bool hung = spent >= kHungStart;
        if (hung || entry.failures >= kQuickFailures) entry.benched = true;
        return {entry.benched, hung};
    }

    // A check run off the control thread could not get the device going. That is
    // the whole question it asks, so its answer stands at once.
    void bench(const std::string& uid) {
        if (uid.empty()) return;
        entries_[uid] = Entry{0, true, false};
    }

    void succeeded(const std::string& uid) {
        if (uid.empty()) return;
        entries_[uid] = Entry{0, false, true};
    }

    // The listener asked for this device again: give it a fresh attempt.
    void release(const std::string& uid) { entries_.erase(uid); }

    bool benched(const std::string& uid) const {
        const auto found = entries_.find(uid);
        return found != entries_.end() && found->second.benched;
    }

    // It ran this session, so routing to it needs no check first.
    bool verified(const std::string& uid) const {
        const auto found = entries_.find(uid);
        return found != entries_.end() && found->second.verified;
    }

    std::vector<std::string> benchedUIDs() const {
        std::vector<std::string> out;
        for (const auto& [uid, entry] : entries_)
            if (entry.benched) out.push_back(uid);
        return out;
    }

    // A device that left the device list starts clean when it comes back.
    void retain(const std::vector<std::string>& present) {
        for (auto it = entries_.begin(); it != entries_.end();) {
            bool found = false;
            for (const auto& uid : present) {
                if (uid == it->first) { found = true; break; }
            }
            it = found ? std::next(it) : entries_.erase(it);
        }
    }

private:
    struct Entry { unsigned failures = 0; bool benched = false; bool verified = false; };
    std::map<std::string, Entry> entries_;
};

} // namespace roomcut
#endif
