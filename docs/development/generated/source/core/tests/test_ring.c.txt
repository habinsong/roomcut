/*
 * test_ring.c — host unit tests for the lock-free SPSC ring (roomcut_ring.h).
 *
 * Dependency-free: plain C + pthreads. No test framework; a failed check prints
 * and sets the process exit code non-zero (ctest treats that as failure).
 *
 * Coverage:
 *   1. init/validate + geometry helpers
 *   2. single-threaded write/read round-trip with wrap-around
 *   3. overrun: a write that doesn't fit is rejected wholesale + counted
 *   4. underrun: a read larger than available returns partial + counts shortfall
 *   5. threaded stress: producer writes a known counter sequence, consumer
 *      verifies it arrives intact and in order (data integrity under contention)
 */
#include "roomcut_ring.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

#define CHANNELS ROOMCUT_MVP_CHANNELS

/* Allocate a ring region with the given frame capacity. */
static RoomcutRingHeader* make_ring(uint32_t capFrames, uint32_t channels)
{
    uint64_t bytes = roomcut_ring_region_bytes(capFrames, channels);
    RoomcutRingHeader* h = (RoomcutRingHeader*)malloc((size_t)bytes);
    memset(h, 0xAB, (size_t)bytes); /* poison: init must overwrite the header */
    CHECK(roomcut_ring_init(h, capFrames, channels, ROOMCUT_SR_48000), "init ok");
    return h;
}

static void test_init_validate(void)
{
    RoomcutRingHeader* h = make_ring(8, CHANNELS);
    CHECK(roomcut_ring_validate(h), "validate after init");
    CHECK(h->capacityFrames == 8, "capacity stored");
    CHECK(h->channels == CHANNELS, "channels stored");
    CHECK(roomcut_ring_readable(h) == 0, "empty: nothing readable");
    CHECK(roomcut_ring_writable(h) == 8, "empty: full capacity writable");

    /* Non-power-of-two capacity must be rejected. */
    uint64_t bytes = roomcut_ring_region_bytes(10, CHANNELS);
    RoomcutRingHeader* bad = (RoomcutRingHeader*)malloc((size_t)bytes);
    CHECK(!roomcut_ring_init(bad, 10, CHANNELS, ROOMCUT_SR_48000), "reject non-pow2");
    free(bad);

    /* A zeroed/garbage header must fail validation. */
    RoomcutRingHeader z;
    memset(&z, 0, sizeof z);
    CHECK(!roomcut_ring_validate(&z), "reject zeroed header");

    free(h);
}

/* Fill `buf` with a recognizable ramp so reads can be checked exactly. */
static void fill_ramp(roomcut_sample_t* buf, uint32_t frames, uint32_t channels, float base)
{
    for (uint32_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < channels; ++c) {
            buf[f * channels + c] = base + (float)f + (float)c * 0.01f;
        }
    }
}

static void test_roundtrip_wrap(void)
{
    const uint32_t cap = 8;
    RoomcutRingHeader* h = make_ring(cap, CHANNELS);

    roomcut_sample_t in[8 * CHANNELS], out[8 * CHANNELS];

    /* Advance the indices near the wrap point: write 6, read 6. */
    fill_ramp(in, 6, CHANNELS, 100.0f);
    CHECK(roomcut_ring_write(h, in, 6, 0) == 6, "write 6");
    CHECK(roomcut_ring_readable(h) == 6, "readable 6");
    CHECK(roomcut_ring_read(h, out, 6) == 6, "read 6");
    CHECK(memcmp(in, out, sizeof(roomcut_sample_t) * 6 * CHANNELS) == 0, "data 6 intact");
    CHECK(roomcut_ring_readable(h) == 0, "drained");

    /* Now write 6 more: this straddles the buffer end (start index = 6). */
    fill_ramp(in, 6, CHANNELS, 200.0f);
    CHECK(roomcut_ring_write(h, in, 6, 0) == 6, "write 6 across wrap");
    CHECK(roomcut_ring_read(h, out, 6) == 6, "read 6 across wrap");
    CHECK(memcmp(in, out, sizeof(roomcut_sample_t) * 6 * CHANNELS) == 0, "wrapped data intact");
    CHECK(h->overruns == 0 && h->underruns == 0, "no over/underruns on clean path");

    free(h);
}

static void test_overrun(void)
{
    const uint32_t cap = 8;
    RoomcutRingHeader* h = make_ring(cap, CHANNELS);
    roomcut_sample_t in[8 * CHANNELS], out[8 * CHANNELS];

    fill_ramp(in, 8, CHANNELS, 0.0f);
    CHECK(roomcut_ring_write(h, in, 8, 0) == 8, "fill to capacity");
    CHECK(roomcut_ring_writable(h) == 0, "full");

    /* A further write of 3 cannot fit → rejected wholesale, counted as overrun. */
    CHECK(roomcut_ring_write(h, in, 3, 0) == 0, "overflowing write rejected");
    CHECK(h->overruns == 3, "overrun counts dropped frames");

    /* Existing data must be untouched by the rejected write. */
    CHECK(roomcut_ring_read(h, out, 8) == 8, "read back full");
    CHECK(memcmp(in, out, sizeof(roomcut_sample_t) * 8 * CHANNELS) == 0, "data not corrupted by rejected write");

    free(h);
}

static void test_underrun(void)
{
    const uint32_t cap = 8;
    RoomcutRingHeader* h = make_ring(cap, CHANNELS);
    roomcut_sample_t in[8 * CHANNELS], out[8 * CHANNELS];

    fill_ramp(in, 3, CHANNELS, 50.0f);
    CHECK(roomcut_ring_write(h, in, 3, 0) == 3, "write 3");

    /* Ask for 8, only 3 available → partial read, 5-frame underrun. */
    memset(out, 0, sizeof out);
    CHECK(roomcut_ring_read(h, out, 8) == 3, "partial read returns available");
    CHECK(h->underruns == 5, "underrun counts shortfall");
    CHECK(memcmp(in, out, sizeof(roomcut_sample_t) * 3 * CHANNELS) == 0, "partial data intact");

    /* Empty read → 0, underrun grows by full request. */
    CHECK(roomcut_ring_read(h, out, 4) == 0, "empty read returns 0");
    CHECK(h->underruns == 9, "underrun accumulates");

    free(h);
}

/* ---- threaded stress ----------------------------------------------------- */

#define STRESS_FRAMES   2000000u   /* total frames pushed through the ring */
#define STRESS_CAP      1024u
#define STRESS_CHUNKMAX 64u

typedef struct {
    RoomcutRingHeader* h;
    int ok;
} stress_ctx;

/* Producer: write a continuous ramp where frame N's channel 0 == (float)N.
 * Use a simple LCG for chunk sizes so writes vary. Retry on overrun (full). */
static void* producer_fn(void* arg)
{
    stress_ctx* ctx = (stress_ctx*)arg;
    RoomcutRingHeader* h = ctx->h;
    roomcut_sample_t chunk[STRESS_CHUNKMAX * CHANNELS];

    uint64_t produced = 0;
    uint32_t lcg = 12345u;
    while (produced < STRESS_FRAMES) {
        lcg = lcg * 1103515245u + 12345u;
        uint32_t want = 1u + (lcg >> 24) % STRESS_CHUNKMAX;
        if (produced + want > STRESS_FRAMES) want = (uint32_t)(STRESS_FRAMES - produced);

        for (uint32_t f = 0; f < want; ++f) {
            for (uint32_t c = 0; c < CHANNELS; ++c) {
                chunk[f * CHANNELS + c] = (float)(produced + f);
            }
        }
        uint32_t wrote = roomcut_ring_write(h, chunk, want, 0);
        if (wrote == want) {
            produced += want;
        }
        /* else: ring full, spin and retry (consumer will drain) */
    }
    return NULL;
}

/* Consumer: read everything and verify frame N's channel-0 sample == (float)N,
 * strictly increasing with no gaps or repeats. */
static void* consumer_fn(void* arg)
{
    stress_ctx* ctx = (stress_ctx*)arg;
    RoomcutRingHeader* h = ctx->h;
    roomcut_sample_t chunk[STRESS_CHUNKMAX * CHANNELS];

    uint64_t consumed = 0;
    ctx->ok = 1;
    while (consumed < STRESS_FRAMES) {
        uint32_t got = roomcut_ring_read(h, chunk, STRESS_CHUNKMAX);
        for (uint32_t f = 0; f < got; ++f) {
            float expected = (float)(consumed + f);
            if (chunk[f * CHANNELS] != expected) {
                fprintf(stderr, "FAIL: stress integrity at frame %llu: got %f expected %f\n",
                        (unsigned long long)(consumed + f), chunk[f * CHANNELS], expected);
                ctx->ok = 0;
                return NULL;
            }
            /* second channel mirrors channel 0 */
            if (chunk[f * CHANNELS + 1] != expected) {
                fprintf(stderr, "FAIL: stress channel mismatch at frame %llu\n",
                        (unsigned long long)(consumed + f));
                ctx->ok = 0;
                return NULL;
            }
        }
        consumed += got;
    }
    return NULL;
}

static void test_threaded_stress(void)
{
    RoomcutRingHeader* h = make_ring(STRESS_CAP, CHANNELS);
    stress_ctx ctx = { h, 1 };

    pthread_t prod, cons;
    pthread_create(&cons, NULL, consumer_fn, &ctx);
    pthread_create(&prod, NULL, producer_fn, &ctx);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    CHECK(ctx.ok, "threaded stress: data integrity preserved");
    /* In steady state the underrun counter may be non-zero (consumer outran
     * producer briefly); that is expected and not an error. The integrity
     * check above is the real guarantee. */

    free(h);
}

int main(void)
{
    test_init_validate();
    test_roundtrip_wrap();
    test_overrun();
    test_underrun();
    test_threaded_stress();

    if (g_failures == 0) {
        printf("all ring tests passed\n");
        return 0;
    }
    fprintf(stderr, "%d ring test check(s) failed\n", g_failures);
    return 1;
}
