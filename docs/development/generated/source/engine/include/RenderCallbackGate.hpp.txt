#ifndef ROOMCUT_RENDER_CALLBACK_GATE_HPP
#define ROOMCUT_RENDER_CALLBACK_GATE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

namespace roomcut {

using PullFn = void (*)(void*, float*, uint32_t, uint32_t);

// Process-lifetime callback router. HAL receives only an integer token, never
// a pointer to a disposable device/context. One control thread activates or
// retires targets; render threads make one non-blocking admission attempt.
class RenderCallbackGate {
public:
    void activate(uintptr_t token, PullFn pull, void* context, uint32_t channels) {
        deactivate(active_.load(std::memory_order_acquire));
        pull_ = pull;
        context_ = context;
        channels_ = channels;
        active_.store(token, std::memory_order_release);
        state_.store(0, std::memory_order_release);
    }

    void deactivate(uintptr_t token) {
        if (token == 0 || !active_.compare_exchange_strong(token, 0, std::memory_order_acq_rel)) return;
        state_.fetch_or(kClosed, std::memory_order_acq_rel);
        // Only the control thread waits. A callback already using the target
        // must finish before its context or audio buffers may be reclaimed.
        while ((state_.load(std::memory_order_acquire) & kBusy) != 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    bool render(uintptr_t token, float* output, uint32_t frames, uint32_t bytes) {
        uint32_t expected = 0;
        bool admitted = token != 0 && token == active_.load(std::memory_order_acquire)
            && state_.compare_exchange_strong(expected, kBusy, std::memory_order_acquire);
        bool rendered = false;
        if (admitted) {
            // Recheck after admission: a delayed callback may have observed the
            // previous token before a complete retire/activate cycle.
            if (token == active_.load(std::memory_order_acquire) && pull_ != nullptr
                && output != nullptr && channels_ != 0
                && (uint64_t)frames * channels_ * sizeof(float) <= bytes) {
                pull_(context_, output, frames, channels_);
                rendered = true;
            }
            if (!rendered && output != nullptr) std::memset(output, 0, bytes);
            state_.fetch_and(~kBusy, std::memory_order_release);
            return rendered;
        }
        if (output != nullptr) std::memset(output, 0, bytes);
        return false;
    }

    uintptr_t activeToken() const { return active_.load(std::memory_order_acquire); }

private:
    static constexpr uint32_t kBusy = 1;
    static constexpr uint32_t kClosed = 2;
    static_assert(std::atomic<uintptr_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    std::atomic<uintptr_t> active_{0};
    std::atomic<uint32_t> state_{kClosed};
    PullFn pull_ = nullptr;
    void* context_ = nullptr;
    uint32_t channels_ = 0;
};

} // namespace roomcut
#endif
