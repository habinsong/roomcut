#include "EngineConnection.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using roomcut::EngineConnection;
static std::atomic<int> failures{0};
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static unsigned references(mach_port_t port, mach_port_right_t kind = MACH_PORT_RIGHT_SEND) {
    mach_port_urefs_t result = 0;
    mach_port_get_refs(mach_task_self(), port, kind, &result);
    return result;
}

struct Endpoint {
    mach_port_t port = MACH_PORT_NULL;
    Endpoint() { CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) == KERN_SUCCESS, "allocate test endpoint"); }
    void close() {
        if (MACH_PORT_VALID(port)) mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
        port = MACH_PORT_NULL;
    }
    ~Endpoint() { close(); }
};

struct Resolver {
    std::atomic<mach_port_t> port{MACH_PORT_NULL};
    std::atomic<unsigned> calls{0};
    static mach_port_t lookup(void* context) {
        auto& self = *static_cast<Resolver*>(context);
        ++self.calls;
        const auto port = self.port.load();
        if (!MACH_PORT_VALID(port)) return MACH_PORT_NULL;
        return mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS
            ? port : MACH_PORT_NULL;
    }
};

static kern_return_t send(mach_port_t port) {
    mach_msg_header_t message{};
    message.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
    message.msgh_size = sizeof(message);
    message.msgh_remote_port = port;
    message.msgh_id = 99;
    return mach_msg(&message, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message), 0,
                    MACH_PORT_NULL, 100, MACH_PORT_NULL);
}
static void receive(mach_port_t port) {
    struct { mach_msg_header_t header; mach_msg_max_trailer_t trailer; } message{};
    CHECK(mach_msg(&message.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(message),
                   port, 100, MACH_PORT_NULL) == KERN_SUCCESS, "receive request on the intended endpoint");
}

static void aLeaseSurvivesInvalidationAndCacheDestruction() {
    Endpoint endpoint;
    Resolver resolver; resolver.port = endpoint.port;
    EngineConnection::Lease pending;
    {
        EngineConnection cache(Resolver::lookup, &resolver);
        pending = cache.acquire();
        auto second = cache.acquire();
        CHECK(resolver.calls == 1 && references(endpoint.port) == 1, "cache hits share one owned send right");
        cache.invalidate(second);
        CHECK(send(pending.port()) == KERN_SUCCESS, "an in-flight lease remains usable after invalidation");
        receive(endpoint.port);
    }
    CHECK(references(endpoint.port) == 1, "a lease outlives its cache owner");
    pending = {};
    CHECK(references(endpoint.port) == 0, "the last lease releases the kernel right");
}

static void concurrentColdRequestsDoNotLeakRights() {
    Endpoint endpoint;
    Resolver resolver; resolver.port = endpoint.port;
    {
        EngineConnection cache(Resolver::lookup, &resolver);
        std::mutex mutex;
        std::condition_variable changed;
        unsigned ready = 0;
        bool release = false;
        std::vector<std::thread> threads;
        for (int n = 0; n < 8; ++n) threads.emplace_back([&] {
            auto lease = cache.acquire();
            CHECK(lease.port() == endpoint.port, "concurrent request receives the shared endpoint");
            std::unique_lock<std::mutex> lock(mutex);
            ++ready; changed.notify_all();
            changed.wait(lock, [&] { return release; });
            CHECK(references(lease.port()) == 1, "pending requests retain their right together");
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            CHECK(changed.wait_for(lock, std::chrono::seconds(2), [&] { return ready == 8; }), "all requests acquire without waiting for another reply");
            CHECK(resolver.calls == 1 && references(endpoint.port) == 1, "only one cold lookup owns a right");
            release = true; changed.notify_all();
        }
        for (auto& thread : threads) thread.join();
    }
    CHECK(references(endpoint.port) == 0, "concurrent cold requests leave no service right behind");
}

static void anOldFailureCannotInvalidateANewerResolution() {
    Endpoint endpoint;
    Resolver resolver; resolver.port = endpoint.port;
    {
        EngineConnection cache(Resolver::lookup, &resolver);
        auto old = cache.acquire();
        cache.invalidate(old);
        auto replacement = cache.acquire();
        CHECK(old.port() == replacement.port() && references(endpoint.port) == 2,
              "two connection generations may have the same Mach name");
        cache.invalidate(old);
        auto current = cache.acquire();
        CHECK(resolver.calls == 2, "a stale failure cannot force another lookup of the replacement");
        old = {};
        CHECK(references(endpoint.port) == 1, "the superseded generation releases only its own right");
    }
    CHECK(references(endpoint.port) == 0, "replacement connection is released on destruction");
}

static void aBlockedRequestDoesNotLockTheCache() {
    Endpoint first, replacement;
    Resolver resolver; resolver.port = first.port;
    EngineConnection cache(Resolver::lookup, &resolver);
    auto original = cache.acquire();
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, release = false, replacementDone = false;
    int firstResult = -99, replacementResult = -99;
    std::thread pending([&] {
        firstResult = cache.perform([&](mach_port_t port, uint32_t*) {
            if (port != first.port) return KERN_SUCCESS;
            std::unique_lock<std::mutex> lock(mutex);
            entered = true; changed.notify_all();
            changed.wait(lock, [&] { return release; });
            return KERN_FAILURE;
        });
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(changed.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }), "first request is in flight");
    }
    std::thread other([&] {
        resolver.port = replacement.port;
        cache.invalidate(original);
        replacementResult = cache.perform([&](mach_port_t port, uint32_t* status) {
            CHECK(port == replacement.port, "the independent request uses the replacement");
            *status = 7;
            return KERN_SUCCESS;
        });
        std::lock_guard<std::mutex> lock(mutex);
        replacementDone = true; changed.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(changed.wait_for(lock, std::chrono::seconds(2), [&] { return replacementDone; }),
              "another request completes while the original RPC is blocked");
        release = true; changed.notify_all();
    }
    pending.join(); other.join();
    CHECK(firstResult == 0 && replacementResult == 7, "transport retry and engine status keep their return conventions");
    CHECK(resolver.calls == 2, "the old completion does not discard a healthy replacement");
}

static void deadPeerRecoveryAndFailuresAreBounded() {
    Endpoint first, replacement;
    Resolver resolver; resolver.port = first.port;
    EngineConnection cache(Resolver::lookup, &resolver);
    auto pending = cache.acquire();
    const auto oldName = pending.port();
    first.close();
    resolver.port = replacement.port;
    int attempts = 0;
    CHECK(cache.perform([&](mach_port_t port, uint32_t*) { ++attempts; return send(port); }) == 0,
          "a real dead-name send failure recovers to a new endpoint");
    CHECK(attempts == 2, "dead peer is retried once");
    receive(replacement.port);
    CHECK(references(oldName, MACH_PORT_RIGHT_DEAD_NAME) == 1, "the in-flight old generation owns its dead name");
    pending = {};
    CHECK(references(oldName, MACH_PORT_RIGHT_DEAD_NAME) == 0, "the old dead name is released");
    attempts = 0;
    CHECK(cache.perform([&](mach_port_t, uint32_t*) { ++attempts; return KERN_FAILURE; }) == -2 && attempts == 2,
          "repeated transport failure does not retry indefinitely");
    resolver.port = MACH_PORT_NULL;
    attempts = 0;
    CHECK(cache.perform([&](mach_port_t, uint32_t*) { ++attempts; return KERN_SUCCESS; }) == -1 && attempts == 0,
          "failed resolution never calls the request with an invalid right");
}

static void theRealResolverReleasesBootstrapReferences() {
    mach_port_t bootstrap = MACH_PORT_NULL;
    CHECK(task_get_bootstrap_port(mach_task_self(), &bootstrap) == KERN_SUCCESS, "obtain owned bootstrap reference");
    const auto before = references(bootstrap);
    for (int n = 0; n < 25; ++n) {
        EngineConnection cache;
        auto lease = cache.acquire(); // lookup only; never sends an engine command
    }
    CHECK(references(bootstrap) == before, "real bootstrap lookups release their temporary rights, on success or failure");
    mach_port_deallocate(mach_task_self(), bootstrap);
}

static void concurrentFailuresKeepEveryBorrowValid() {
    Endpoint first, second;
    Resolver resolver; resolver.port = first.port;
    {
        EngineConnection cache(Resolver::lookup, &resolver);
        std::atomic<unsigned> calls{0};
        std::vector<std::thread> threads;
        for (int worker = 0; worker < 8; ++worker) threads.emplace_back([&] {
            for (int n = 0; n < 2000; ++n) {
                unsigned attempts = 0;
                const int result = cache.perform([&](mach_port_t port, uint32_t*) {
                    ++attempts;
                    CHECK(references(port) > 0, "every concurrent callback owns a live send right");
                    const auto call = ++calls;
                    if (call % 17 == 0) {
                        resolver.port = call % 2 ? first.port : second.port;
                        return KERN_FAILURE;
                    }
                    return KERN_SUCCESS;
                });
                CHECK((result == 0 || result == -2) && attempts <= 2, "concurrent retries remain bounded");
            }
        });
        for (auto& thread : threads) thread.join();
        auto last = cache.acquire();
        CHECK(last && references(first.port) + references(second.port) == 1,
              "only the final cache generation remains after concurrent requests finish");
    }
    CHECK(references(first.port) + references(second.port) == 0, "stress leaves no service rights behind");
}

int main() {
    aLeaseSurvivesInvalidationAndCacheDestruction();
    concurrentColdRequestsDoNotLeakRights();
    anOldFailureCannotInvalidateANewerResolution();
    aBlockedRequestDoesNotLockTheCache();
    deadPeerRecoveryAndFailuresAreBounded();
    theRealResolverReleasesBootstrapReferences();
    concurrentFailuresKeepEveryBorrowValid();
    if (!failures) std::puts("all engine connection tests passed");
    return failures ? 1 : 0;
}
