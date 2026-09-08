#ifndef ROOMCUT_PUBLISHED_RING_HPP
#define ROOMCUT_PUBLISHED_RING_HPP

#include <atomic>
#include <chrono>
#include <thread>
#include "roomcut_ring.h"

namespace roomcut {

// One control-thread owner and one audio-thread borrower. Publication does not
// transfer ownership: clear() must finish before the owner destroys the region.
class PublishedRing {
public:
    class Lease {
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() { if (owner_) owner_->borrowed_.store(nullptr, std::memory_order_seq_cst); }
        RoomcutRingHeader* get() const { return header_; }
    private:
        friend class PublishedRing;
        Lease(PublishedRing* owner, RoomcutRingHeader* header) : owner_(owner), header_(header) {}
        PublishedRing* owner_;
        RoomcutRingHeader* header_;
    };

    // One bounded attempt, with no waits or retries on the audio thread.
    Lease borrow() {
        auto* header = published_.load(std::memory_order_seq_cst);
        borrowed_.store(header, std::memory_order_seq_cst);
        if (header != published_.load(std::memory_order_seq_cst)) {
            borrowed_.store(nullptr, std::memory_order_seq_cst);
            return Lease(nullptr, nullptr);
        }
        return Lease(this, header);
    }

    void publish(RoomcutRingHeader* header) {
        clear();
        published_.store(header, std::memory_order_seq_cst);
    }

    void clear() {
        auto* old = published_.exchange(nullptr, std::memory_order_seq_cst);
        if (old == nullptr) return;
        while (borrowed_.load(std::memory_order_seq_cst) == old) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    // Control thread only: that thread also owns all retirement/destruction.
    RoomcutRingHeader* current() const { return published_.load(std::memory_order_seq_cst); }

private:
    static_assert(std::atomic<RoomcutRingHeader*>::is_always_lock_free);
    std::atomic<RoomcutRingHeader*> published_{nullptr};
    std::atomic<RoomcutRingHeader*> borrowed_{nullptr};
};

} // namespace roomcut
#endif
