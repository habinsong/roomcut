/*
 * RingRegion.cpp — see RingRegion.hpp.
 *
 * Mach memory entry lifecycle:
 *   create():
 *     mach_vm_allocate a page-rounded region in this task
 *     → mach_make_memory_entry_64 to get a send-right covering it
 *     → roomcut_ring_init over the mapping
 *   mapFromPort():
 *     mach_vm_map the received entry into this task (driver side)
 *     → roomcut_ring_validate
 *
 * Page rounding: a memory entry must cover whole VM pages, so we round the
 * logical ring-region size (header + samples) up to the page size.
 */
#include "RingRegion.hpp"

#include <mach/mach_vm.h>
#include <unistd.h>

namespace roomcut {

namespace {

uint64_t roundToPage(uint64_t bytes) {
    const uint64_t page = static_cast<uint64_t>(getpagesize());
    return (bytes + page - 1) & ~(page - 1);
}

} // namespace

RingRegion::~RingRegion() {
    destroy();
}

RingRegion::RingRegion(RingRegion&& other) noexcept {
    *this = static_cast<RingRegion&&>(other);
}

RingRegion& RingRegion::operator=(RingRegion&& other) noexcept {
    if (this != &other) {
        destroy();
        header_      = other.header_;
        mappedAddr_  = other.mappedAddr_;
        mappedSize_  = other.mappedSize_;
        memoryEntry_ = other.memoryEntry_;
        sizeBytes_   = other.sizeBytes_;

        other.header_      = nullptr;
        other.mappedAddr_  = 0;
        other.mappedSize_  = 0;
        other.memoryEntry_ = MACH_PORT_NULL;
        other.sizeBytes_   = 0;
    }
    return *this;
}

kern_return_t RingRegion::create(uint32_t capacityFrames, uint32_t channels, uint32_t sampleRate) {
    destroy();

    const uint64_t logical = roomcut_ring_region_bytes(capacityFrames, channels);
    const mach_vm_size_t size = roundToPage(logical);

    mach_port_t task = mach_task_self();

    // Allocate the backing region in our own task.
    mach_vm_address_t addr = 0;
    kern_return_t kr = mach_vm_allocate(task, &addr, size, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    // Wrap it in a memory entry whose send-right we can hand to the driver.
    memory_object_size_t entrySize = size;
    mach_port_t entry = MACH_PORT_NULL;
    kr = mach_make_memory_entry_64(task,
                                   &entrySize,
                                   static_cast<memory_object_offset_t>(addr),
                                   VM_PROT_READ | VM_PROT_WRITE,
                                   &entry,
                                   MACH_PORT_NULL);
    if (kr != KERN_SUCCESS || entrySize < size) {
        mach_vm_deallocate(task, addr, size);
        if (entry != MACH_PORT_NULL) {
            mach_port_deallocate(task, entry);
        }
        return (kr == KERN_SUCCESS) ? KERN_FAILURE : kr;
    }

    auto* hdr = reinterpret_cast<RoomcutRingHeader*>(addr);
    if (!roomcut_ring_init(hdr, capacityFrames, channels, sampleRate)) {
        mach_vm_deallocate(task, addr, size);
        mach_port_deallocate(task, entry);
        return KERN_INVALID_ARGUMENT;
    }

    header_      = hdr;
    mappedAddr_  = addr;
    mappedSize_  = size;
    memoryEntry_ = entry;
    sizeBytes_   = logical;
    return KERN_SUCCESS;
}

kern_return_t RingRegion::mapFromPort(mach_port_t memoryEntry) {
    destroy();

    mach_port_t task = mach_task_self();

    // Determine the entry's size so we map the whole thing.
    // mach_vm_map with size 0 is invalid; the driver knows the negotiated
    // geometry from the HELLO reply, but here we map then validate the header,
    // which carries capacity/channels. We map a page first to read the header,
    // but simpler: the entry size equals the page-rounded region, and the
    // memory entry remembers its own size, so VM_FLAGS_ANYWHERE + size from the
    // entry works via mach_vm_map's handling. We pass the maximum we expect and
    // rely on the entry's actual extent.
    //
    // Practical approach: map with a size we read back. We cannot query the
    // entry size portably, so map header-sized first, read geometry, remap.
    mach_vm_address_t addr = 0;
    mach_vm_size_t probeSize = roundToPage(sizeof(RoomcutRingHeader));
    kern_return_t kr = mach_vm_map(task, &addr, probeSize, 0, VM_FLAGS_ANYWHERE,
                                   memoryEntry, 0, FALSE,
                                   VM_PROT_READ | VM_PROT_WRITE,
                                   VM_PROT_READ | VM_PROT_WRITE,
                                   VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    auto* probe = reinterpret_cast<RoomcutRingHeader*>(addr);
    if (!roomcut_ring_validate(probe)) {
        mach_vm_deallocate(task, addr, probeSize);
        return KERN_INVALID_ARGUMENT;
    }
    const uint32_t cap = probe->capacityFrames;
    const uint32_t ch  = probe->channels;
    const uint64_t logical = roomcut_ring_region_bytes(cap, ch);
    const mach_vm_size_t fullSize = roundToPage(logical);

    // Re-map at full size now that geometry is known.
    mach_vm_deallocate(task, addr, probeSize);
    addr = 0;
    kr = mach_vm_map(task, &addr, fullSize, 0, VM_FLAGS_ANYWHERE,
                     memoryEntry, 0, FALSE,
                     VM_PROT_READ | VM_PROT_WRITE,
                     VM_PROT_READ | VM_PROT_WRITE,
                     VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    auto* hdr = reinterpret_cast<RoomcutRingHeader*>(addr);
    if (!roomcut_ring_validate(hdr)) {
        mach_vm_deallocate(task, addr, fullSize);
        return KERN_INVALID_ARGUMENT;
    }

    header_      = hdr;
    mappedAddr_  = addr;
    mappedSize_  = fullSize;
    memoryEntry_ = memoryEntry;   // take ownership of the received right
    sizeBytes_   = logical;
    return KERN_SUCCESS;
}

void RingRegion::destroy() {
    mach_port_t task = mach_task_self();
    if (mappedAddr_ != 0 && mappedSize_ != 0) {
        mach_vm_deallocate(task, mappedAddr_, mappedSize_);
    }
    if (memoryEntry_ != MACH_PORT_NULL) {
        mach_port_deallocate(task, memoryEntry_);
    }
    header_      = nullptr;
    mappedAddr_  = 0;
    mappedSize_  = 0;
    memoryEntry_ = MACH_PORT_NULL;
    sizeBytes_   = 0;
}

} // namespace roomcut
