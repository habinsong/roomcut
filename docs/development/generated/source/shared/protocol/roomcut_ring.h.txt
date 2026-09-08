/*
 * roomcut_ring.h — lock-free single-producer/single-consumer ring buffer ops
 * over the shared RoomcutRingHeader (declared in roomcut_ipc.h).
 *
 * This is the real-time audio path between Roomcut.driver (producer: the IO
 * proc inside coreaudiod) and RoomcutAudioEngine (consumer: the render-side
 * reader). See DEVELOPMENT_PLAN.md §4.1 — the engine creates the shared region
 * and hands the driver a send-right; both then map the same pages and run this
 * ring.
 *
 * Constraints (must hold for the sandboxed driver):
 *   - Dependency-free: only <stdint.h>/<stdbool.h>/<string.h> + the protocol
 *     headers. No CoreAudio, no Foundation, no allocation, no locks, no syscalls.
 *   - Header-only inline functions so both sides compile their own copy.
 *
 * Memory model: writeIndex is producer-owned, readIndex is consumer-owned, both
 * free-running 64-bit frame counters. We use acquire/release ordering via the
 * compiler __atomic builtins (clang/gcc) directly on the existing volatile
 * uint64_t fields, so the shared struct layout is untouched and ABI-stable
 * across the two processes.
 *
 *   producer publishes data:  store samples → release-store writeIndex
 *   consumer observes data:    acquire-load writeIndex → read samples
 *   consumer frees space:      release-store readIndex
 *   producer checks space:     acquire-load readIndex
 */
#ifndef ROOMCUT_RING_H
#define ROOMCUT_RING_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "roomcut_audio_format.h"
#include "roomcut_ipc.h"

/* The interleaved float32 samples live immediately after the header. */
static inline roomcut_sample_t* roomcut_ring_samples(RoomcutRingHeader* h)
{
    return (roomcut_sample_t*)((unsigned char*)h + sizeof(RoomcutRingHeader));
}

/* Total bytes a region must be to hold a header + `capacityFrames` of `channels`. */
static inline uint64_t roomcut_ring_region_bytes(uint32_t capacityFrames, uint32_t channels)
{
    return (uint64_t)sizeof(RoomcutRingHeader)
         + (uint64_t)capacityFrames * (uint64_t)channels * (uint64_t)ROOMCUT_BYTES_PER_SAMPLE;
}

static inline bool roomcut_is_pow2(uint32_t v)
{
    return v != 0u && (v & (v - 1u)) == 0u;
}

/*
 * Initialize a freshly mapped region as an empty ring. Called once by the
 * producer side of the handoff (the engine, which owns region creation) before
 * the driver starts writing. capacityFrames MUST be a power of two.
 * Returns false on bad arguments.
 */
static inline bool roomcut_ring_init(RoomcutRingHeader* h, uint32_t capacityFrames, uint32_t channels, uint32_t sampleRate)
{
    if (h == NULL || channels == 0u || !roomcut_is_pow2(capacityFrames)) {
        return false;
    }
    h->magic          = ROOMCUT_RING_MAGIC;
    h->version        = ROOMCUT_IPC_VERSION;
    h->capacityFrames = capacityFrames;
    h->channels       = channels;
    h->sampleRate     = sampleRate;
    h->_pad0          = 0u;
    h->writeIndex     = 0u;
    h->hostTimeAtWrite= 0u;
    h->readIndex      = 0u;
    h->overruns       = 0u;
    h->underruns      = 0u;
    h->dropouts       = 0u;
    return true;
}

/*
 * Validate a region the other side mapped. The driver calls this after the
 * handoff before trusting the mapping (DEVELOPMENT_PLAN.md §4.1, step
 * "validate RoomcutRingHeader.magic/version").
 */
static inline bool roomcut_ring_validate(const RoomcutRingHeader* h)
{
    return h != NULL
        && h->magic == ROOMCUT_RING_MAGIC
        && h->version == ROOMCUT_IPC_VERSION
        && h->channels != 0u
        && roomcut_is_pow2(h->capacityFrames);
}

/* Frames currently available to read (producer ahead of consumer). */
static inline uint32_t roomcut_ring_readable(const RoomcutRingHeader* h)
{
    uint64_t w = __atomic_load_n(&h->writeIndex, __ATOMIC_ACQUIRE);
    uint64_t r = __atomic_load_n(&h->readIndex, __ATOMIC_ACQUIRE);
    return (uint32_t)(w - r);
}

/* Frames the producer may write without overrunning unread data. */
static inline uint32_t roomcut_ring_writable(const RoomcutRingHeader* h)
{
    uint64_t w = __atomic_load_n(&h->writeIndex, __ATOMIC_ACQUIRE);
    uint64_t r = __atomic_load_n(&h->readIndex, __ATOMIC_ACQUIRE);
    return h->capacityFrames - (uint32_t)(w - r);
}

/*
 * Producer side (driver IO proc). Copy `frames` of interleaved float32 from
 * `src` into the ring and publish them. Real-time safe.
 *
 * If there isn't room for all `frames`, NOTHING is written and `overruns` is
 * bumped by the dropped frame count — we never partially write an IO buffer
 * (that would tear a callback's worth of audio). Returns frames actually
 * written (0 or `frames`).
 *
 * hostTime is mach_absolute_time at the callback; stored for the consumer's
 * latency accounting. Pass 0 if unavailable.
 */
static inline uint32_t roomcut_ring_write(RoomcutRingHeader* h, const roomcut_sample_t* src, uint32_t frames, uint64_t hostTime)
{
    const uint32_t cap = h->capacityFrames;
    const uint32_t ch  = h->channels;

    uint64_t w = __atomic_load_n(&h->writeIndex, __ATOMIC_RELAXED); /* producer owns it */
    uint64_t r = __atomic_load_n(&h->readIndex,  __ATOMIC_ACQUIRE);
    uint32_t freeFrames = cap - (uint32_t)(w - r);

    if (frames > freeFrames) {
        __atomic_fetch_add(&h->overruns, (uint64_t)frames, __ATOMIC_RELAXED);
        return 0u;
    }

    roomcut_sample_t* base = roomcut_ring_samples(h);
    uint32_t start = (uint32_t)(w & (cap - 1u));          /* cap is pow2 */
    uint32_t first = frames;
    if (start + first > cap) {
        first = cap - start;                              /* wrap split */
    }
    memcpy(base + (size_t)start * ch, src, (size_t)first * ch * sizeof(roomcut_sample_t));
    if (first < frames) {
        memcpy(base, src + (size_t)first * ch, (size_t)(frames - first) * ch * sizeof(roomcut_sample_t));
    }

    h->hostTimeAtWrite = hostTime;
    __atomic_store_n(&h->writeIndex, w + frames, __ATOMIC_RELEASE);  /* publish */
    return frames;
}

/*
 * Consumer side (engine render reader). Copy up to `frames` interleaved float32
 * into `dst`. Returns frames actually read (may be < frames if the ring is
 * short). If fewer than `frames` are available, the shortfall is counted as an
 * underrun (the caller is expected to zero-fill the rest of its output buffer).
 */
static inline uint32_t roomcut_ring_read(RoomcutRingHeader* h, roomcut_sample_t* dst, uint32_t frames)
{
    const uint32_t cap = h->capacityFrames;
    const uint32_t ch  = h->channels;

    uint64_t w = __atomic_load_n(&h->writeIndex, __ATOMIC_ACQUIRE);
    uint64_t r = __atomic_load_n(&h->readIndex,  __ATOMIC_RELAXED); /* consumer owns it */
    uint32_t avail = (uint32_t)(w - r);

    uint32_t toRead = frames < avail ? frames : avail;
    if (toRead < frames) {
        __atomic_fetch_add(&h->underruns, (uint64_t)(frames - toRead), __ATOMIC_RELAXED);
    }
    if (toRead == 0u) {
        return 0u;
    }

    const roomcut_sample_t* base = roomcut_ring_samples(h);
    uint32_t start = (uint32_t)(r & (cap - 1u));
    uint32_t first = toRead;
    if (start + first > cap) {
        first = cap - start;
    }
    memcpy(dst, base + (size_t)start * ch, (size_t)first * ch * sizeof(roomcut_sample_t));
    if (first < toRead) {
        memcpy(dst + (size_t)first * ch, base, (size_t)(toRead - first) * ch * sizeof(roomcut_sample_t));
    }

    __atomic_store_n(&h->readIndex, r + toRead, __ATOMIC_RELEASE);  /* free space */
    return toRead;
}

#endif /* ROOMCUT_RING_H */
