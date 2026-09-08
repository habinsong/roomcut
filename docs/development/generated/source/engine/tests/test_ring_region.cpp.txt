/*
 * test_ring_region.cpp — verifies the Mach memory-entry handoff: a region the
 * engine creates can be mapped a second time from its send-right and both
 * mappings see the SAME physical pages (DEVELOPMENT_PLAN.md §4.1).
 *
 * This models the driver side of the handoff in-process: the engine's
 * RingRegion::create() is the producer side; a second RingRegion::mapFromPort()
 * on the same entry stands in for the driver's mach_vm_map after it receives
 * the send-right over the Mach service. (The actual cross-process right
 * transfer requires the driver loaded in coreaudiod — that's a later step.)
 */
#include "RingRegion.hpp"

#include <cstdio>
#include <cstring>

#include <mach/mach.h>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using roomcut::RingRegion;

// Bump the send-right refcount so two RingRegions can each own a reference to
// the same entry: create() and mapFromPort() both mach_port_deallocate their
// entry on destroy, and the +1 here balances that second deallocate. In the
// real system the driver receives its own right via IPC; here we model the
// extra reference directly.
static mach_port_t duplicateSendRight(mach_port_t port) {
    kern_return_t kr = mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, +1);
    if (kr != KERN_SUCCESS) return MACH_PORT_NULL;
    return port; // same name, refcount bumped; the two deallocates balance it
}

int main() {
    const uint32_t cap = 1024;
    const uint32_t ch  = ROOMCUT_MVP_CHANNELS;

    RingRegion engineSide;
    kern_return_t kr = engineSide.create(cap, ch, ROOMCUT_SR_48000);
    CHECK(kr == KERN_SUCCESS, "engine create() succeeds");
    CHECK(engineSide.valid(), "engine region valid");
    CHECK(engineSide.memoryEntry() != MACH_PORT_NULL, "memory entry send-right present");
    CHECK(engineSide.header() != nullptr, "engine header mapped");
    CHECK(roomcut_ring_validate(engineSide.header()), "engine header validates");

    // Model the driver mapping the same entry from its received send-right.
    mach_port_t handed = duplicateSendRight(engineSide.memoryEntry());
    CHECK(handed != MACH_PORT_NULL, "duplicate send-right for driver side");

    RingRegion driverSide;
    kr = driverSide.mapFromPort(handed);
    CHECK(kr == KERN_SUCCESS, "driver mapFromPort() succeeds");
    CHECK(driverSide.valid(), "driver region valid");
    CHECK(driverSide.header() != nullptr, "driver header mapped");

    // Distinct virtual addresses, SAME physical pages: a write on one side must
    // be visible on the other.
    RoomcutRingHeader* eng = engineSide.header();
    RoomcutRingHeader* drv = driverSide.header();
    CHECK(eng != drv, "two independent virtual mappings");
    CHECK(eng->capacityFrames == drv->capacityFrames, "geometry agrees");

    // Driver (producer) writes a ramp; engine (consumer) reads it back.
    roomcut_sample_t in[64 * ROOMCUT_MVP_CHANNELS];
    roomcut_sample_t out[64 * ROOMCUT_MVP_CHANNELS];
    for (uint32_t f = 0; f < 64; ++f)
        for (uint32_t c = 0; c < ch; ++c)
            in[f * ch + c] = 1000.0f + (float)f;

    uint32_t wrote = roomcut_ring_write(drv, in, 64, 0);
    CHECK(wrote == 64, "driver writes 64 frames");

    // The engine must observe the write through its own mapping.
    CHECK(roomcut_ring_readable(eng) == 64, "engine sees 64 readable via shared pages");
    uint32_t got = roomcut_ring_read(eng, out, 64);
    CHECK(got == 64, "engine reads 64 frames");
    CHECK(memcmp(in, out, sizeof(roomcut_sample_t) * 64 * ch) == 0, "data identical across mappings");

    // And the reverse: engine's read advanced readIndex, visible to driver.
    CHECK(roomcut_ring_writable(drv) == cap, "driver sees space freed by engine read");

    driverSide.destroy();
    engineSide.destroy();

    if (g_failures == 0) { printf("all ring-region tests passed\n"); return 0; }
    fprintf(stderr, "%d ring-region check(s) failed\n", g_failures);
    return 1;
}
