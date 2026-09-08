#include "RenderCallbackGate.hpp"
#include "PublishedRing.hpp"
#include <array>
#include <cstdio>
#include <memory>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

struct BlockedRender {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    float value = 0.25f;
    static void pull(void* pointer, float* output, uint32_t frames, uint32_t channels) {
        auto& state = *static_cast<BlockedRender*>(pointer);
        state.entered.store(true, std::memory_order_release);
        while (!state.release.load(std::memory_order_acquire)) std::this_thread::yield();
        for (uint32_t i = 0; i < frames * channels; ++i) output[i] = state.value;
    }
};

static void testRetirementWaitsAndLateCallbacksAreSilent() {
    RenderCallbackGate gate;
    auto state = std::make_unique<BlockedRender>();
    gate.activate(1, &BlockedRender::pull, state.get(), 2);
    std::array<float, 16> output{};
    std::thread render([&] { gate.render(1, output.data(), 8, sizeof(output)); });
    while (!state->entered.load(std::memory_order_acquire)) std::this_thread::yield();
    std::atomic<bool> retired{false};
    std::thread control([&] { gate.deactivate(1); retired.store(true, std::memory_order_release); });
    while (gate.activeToken() != 0) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    CHECK(!retired.load(std::memory_order_acquire), "retirement waits for an in-flight callback beyond 20 ms");
    std::array<float, 16> rejected; rejected.fill(1);
    CHECK(!gate.render(1, rejected.data(), 8, sizeof(rejected)), "retirement prevents new pulls immediately");
    CHECK(rejected.back() == 0, "a rejected callback renders silence");
    state->release.store(true, std::memory_order_release);
    render.join(); control.join();
    CHECK(output.back() == 0.25f, "the in-flight callback retains its valid context");
    state.reset();
    CHECK(!gate.render(1, output.data(), 8, sizeof(output)), "late callback cannot reach a freed context");

    BlockedRender next; next.release.store(true); next.value = 0.5f;
    gate.activate(2, &BlockedRender::pull, &next, 2);
    gate.deactivate(1);
    CHECK(gate.activeToken() == 2, "retiring an old device cannot disarm a new one");
    CHECK(!gate.render(1, output.data(), 8, sizeof(output)), "old tokens remain invalid after a reopen");
    CHECK(gate.render(2, output.data(), 8, sizeof(output)) && output.back() == 0.5f,
          "new token uses only its own context");
    output.fill(1);
    CHECK(!gate.render(2, output.data(), 100, sizeof(output)), "short HAL buffers are rejected before a pull");
    CHECK(output.back() == 0, "short buffers are silenced within their bounds");
    gate.deactivate(2);
}

static void testConcurrentGenerationChanges() {
    RenderCallbackGate gate;
    std::atomic<bool> done{false};
    auto pull = [](void* state, float* output, uint32_t frames, uint32_t channels) {
        const float expected = *static_cast<float*>(state);
        for (uint32_t i = 0; i < frames * channels; ++i) output[i] = expected;
    };
    bool consistent = true;
    std::thread render([&] {
        float output[32];
        while (!done.load(std::memory_order_acquire)) {
            const auto token = gate.activeToken();
            if (gate.render(token, output, 16, sizeof(output))) {
                for (float value : output) consistent &= value == (float)token;
            }
            if (token > 1 && gate.render(token - 1, output, 16, sizeof(output))) consistent = false;
        }
    });
    for (uintptr_t token = 1; token < 5000; ++token) {
        auto value = std::make_unique<float>((float)token);
        gate.activate(token, pull, value.get(), 2);
        std::this_thread::yield();
        gate.deactivate(token); // releases the only borrow before value is destroyed
    }
    done.store(true, std::memory_order_release);
    render.join();
    CHECK(consistent, "rapid generation changes never mix callback/context identities");
}

static void testRingRetirementWaitsForBorrower() {
    PublishedRing published;
    auto header = std::make_unique<RoomcutRingHeader>();
    header->magic = ROOMCUT_RING_MAGIC;
    published.publish(header.get());
    std::atomic<bool> borrowed{false}, release{false}, retired{false};
    bool valid = false;
    std::thread reader([&] {
        auto lease = published.borrow();
        borrowed.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        valid = lease.get() && lease.get()->magic == ROOMCUT_RING_MAGIC;
    });
    while (!borrowed.load(std::memory_order_acquire)) std::this_thread::yield();
    std::thread owner([&] {
        published.clear();
        header.reset();
        retired.store(true, std::memory_order_release);
    });
    while (published.current() != nullptr) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    CHECK(!retired.load(std::memory_order_acquire), "ring mapping is retained until the borrower finishes");
    release.store(true, std::memory_order_release);
    reader.join(); owner.join();
    CHECK(valid && retired.load(), "borrowed ring remains valid until explicit release");
    auto empty = published.borrow();
    CHECK(empty.get() == nullptr, "unpublished ring cannot be borrowed");
}

static void testRingReplacements() {
    PublishedRing published;
    std::atomic<bool> done{false};
    bool valid = true;
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            auto lease = published.borrow();
            if (auto* header = lease.get()) {
                valid &= header->magic == ROOMCUT_RING_MAGIC;
                valid &= header->sampleRate == header->capacityFrames;
            }
        }
    });
    for (uint32_t i = 1; i < 5000; ++i) {
        auto header = std::make_unique<RoomcutRingHeader>();
        header->magic = ROOMCUT_RING_MAGIC;
        header->sampleRate = header->capacityFrames = i;
        published.publish(header.get());
        std::this_thread::yield();
        published.clear();
    }
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK(valid, "ring retirement and address reuse never expose torn or freed headers");
}

int main() {
    testRetirementWaitsAndLateCallbacksAreSilent();
    testConcurrentGenerationChanges();
    testRingRetirementWaitsForBorrower();
    testRingReplacements();
    if (failures == 0) std::puts("all render lifetime tests passed");
    return failures == 0 ? 0 : 1;
}
