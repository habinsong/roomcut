#ifndef ROOMCUT_ENGINE_CONNECTION_HPP
#define ROOMCUT_ENGINE_CONNECTION_HPP

#include <mach/mach.h>
#include <memory>
#include <mutex>
#include <utility>

namespace roomcut {

// Non-realtime client connection. The cache and each in-flight request share
// one owned send right. Cache invalidation cannot revoke another request's use.
class EngineConnection {
    struct SendRight {
        mach_port_t port = MACH_PORT_NULL;
        ~SendRight();
    };

public:
    class Lease {
    public:
        Lease() = default;
        mach_port_t port() const { return right_ ? right_->port : MACH_PORT_NULL; }
        explicit operator bool() const { return right_ != nullptr; }
    private:
        friend class EngineConnection;
        explicit Lease(std::shared_ptr<SendRight> right) : right_(std::move(right)) {}
        std::shared_ptr<SendRight> right_;
    };

    // A resolver transfers one owned send right, or returns MACH_PORT_NULL.
    using Lookup = mach_port_t (*)(void*);
    explicit EngineConnection(Lookup lookup = lookupService, void* context = nullptr)
        : lookup_(lookup), context_(context) {}
    EngineConnection(const EngineConnection&) = delete;
    EngineConnection& operator=(const EngineConnection&) = delete;

    Lease acquire();
    void invalidate(const Lease& failed);

    // Keep the established C API convention and one retry. Never hold the
    // cache mutex while a request waits for its reply.
    template<class Function> int perform(Function&& function) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto lease = acquire();
            if (!lease) return -1;
            uint32_t status = 0;
            if (function(lease.port(), &status) == KERN_SUCCESS) return static_cast<int>(status);
            invalidate(lease);
        }
        return -2;
    }

private:
    static mach_port_t lookupService(void*);
    Lookup lookup_;
    void* context_;
    std::mutex mutex_;
    std::shared_ptr<SendRight> cached_;
};
} // namespace roomcut
#endif
