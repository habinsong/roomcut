/*
 * RingRegion.hpp — the shared-memory audio region the engine owns and the
 * driver maps into (DEVELOPMENT_PLAN.md §4.1).
 *
 * Why a Mach memory entry rather than POSIX shm_open: the driver runs inside
 * the coreaudiod sandbox and CANNOT open shared memory by name (docs §0). The
 * sanctioned path is for the engine to create the region, obtain a Mach
 * send-right (a memory entry port), and pass that right to the driver over the
 * declared Mach service. The driver then vm_map()s the right — no name, no
 * filesystem. So the Mach memory entry IS the production mechanism, not an
 * optimization.
 *
 * RingRegion owns:
 *   - the backing vm allocation (engine's own mapping),
 *   - the memory entry port (the send-right handed to the driver),
 * and lays a RoomcutRingHeader + sample area over the mapping via roomcut_ring.
 */
#ifndef ROOMCUT_RING_REGION_HPP
#define ROOMCUT_RING_REGION_HPP

#include <cstdint>

#include <mach/mach.h>

extern "C" {
#include "roomcut_ring.h"
}

namespace roomcut {

class RingRegion {
public:
    RingRegion() = default;
    ~RingRegion();

    RingRegion(const RingRegion&) = delete;
    RingRegion& operator=(const RingRegion&) = delete;
    RingRegion(RingRegion&&) noexcept;
    RingRegion& operator=(RingRegion&&) noexcept;

    // Create + map the region and initialize the ring header. capacityFrames
    // must be a power of two. Returns KERN_SUCCESS on success; on failure the
    // object stays empty (valid() == false).
    kern_return_t create(uint32_t capacityFrames, uint32_t channels, uint32_t sampleRate);

    // Map an existing memory entry send-right (the driver side of the handoff).
    // Validates the ring header after mapping. Takes a send-right the caller
    // received over the Mach service; on success RingRegion owns it.
    kern_return_t mapFromPort(mach_port_t memoryEntry);

    void destroy();

    bool valid() const { return header_ != nullptr; }

    // The ring header at the base of the mapping (nullptr if !valid()).
    RoomcutRingHeader* header() const { return header_; }

    // The send-right to hand to the driver. MACH_PORT_NULL until create().
    // Ownership stays with RingRegion; the receiver gets a copy via IPC.
    mach_port_t memoryEntry() const { return memoryEntry_; }

    uint64_t sizeBytes() const { return sizeBytes_; }

private:
    RoomcutRingHeader* header_      = nullptr;   // == mapped base address
    mach_vm_address_t  mappedAddr_  = 0;
    mach_vm_size_t     mappedSize_  = 0;
    mach_port_t        memoryEntry_ = MACH_PORT_NULL;
    uint64_t           sizeBytes_   = 0;         // logical ring region size
};

} // namespace roomcut

#endif // ROOMCUT_RING_REGION_HPP
