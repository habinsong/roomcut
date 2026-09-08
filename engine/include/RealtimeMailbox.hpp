#ifndef ROOMCUT_REALTIME_MAILBOX_HPP
#define ROOMCUT_REALTIME_MAILBOX_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace roomcut {

// One producer and one consumer, each with an owned slot. Publishing can
// replace an unread snapshot but never the one the consumer is copying.
template <typename T>
class RealtimeMailbox {
public:
    static_assert(std::is_trivially_copyable<T>::value);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

    // Producer only. The reference expires at the next publish().
    T& writable() { return slots_[back_]; }

    void publish() {
        back_ = middle_.exchange(back_ | kDirty, std::memory_order_acq_rel) & kIndex;
    }

    void publish(const T& value) {
        writable() = value;
        publish();
    }

    bool readLatest(T& value) {
        if ((middle_.load(std::memory_order_acquire) & kDirty) == 0) return false;
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndex;
        value = slots_[front_];
        return true;
    }

private:
    static constexpr uint32_t kDirty = 4;
    static constexpr uint32_t kIndex = 3;
    std::array<T, 3> slots_{};
    std::atomic<uint32_t> middle_{1};
    uint32_t back_ = 0;
    uint32_t front_ = 2;
};

} // namespace roomcut
#endif
