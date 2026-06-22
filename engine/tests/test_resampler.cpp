/*
 * test_resampler.cpp — LinearResampler correctness.
 *
 * Verifies the streaming contract that the Phase 4 smoke test exposed:
 *   - ratio == 1 is a bit-exact passthrough,
 *   - input frames pulled == inputFramesFor() (no over-read of the ring),
 *   - across many blocks the resampler consumes the input stream contiguously
 *     (no skipped/duplicated source frames → no glitch/pitch drift),
 *   - a constant input yields ~constant output (DC preserved),
 *   - upsampling 48k→96k of a ramp produces a monotonically increasing,
 *     in-range output (linear interpolation, no overshoot).
 */
#include "LinearResampler.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace roomcut;

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

namespace {

// A monotonic interleaved source: frame index k → value k for every channel.
// Tracks total frames pulled so tests can assert contiguous consumption.
struct RampSource {
    uint32_t next = 0;     // next frame index to emit
    uint32_t channels = 2;
    bool     dry = false;  // when true, return 0 (source ran out)

    static uint32_t pull(void* ctx, float* dst, uint32_t inFrames, uint32_t ch) {
        auto* s = static_cast<RampSource*>(ctx);
        if (s->dry) return 0;
        for (uint32_t f = 0; f < inFrames; ++f) {
            for (uint32_t c = 0; c < ch; ++c) dst[f * ch + c] = (float)(s->next + f);
        }
        s->next += inFrames;
        return inFrames;
    }
};

struct ConstSource {
    float v = 0.5f;
    static uint32_t pull(void* ctx, float* dst, uint32_t inFrames, uint32_t ch) {
        auto* s = static_cast<ConstSource*>(ctx);
        for (uint32_t i = 0; i < inFrames * ch; ++i) dst[i] = s->v;
        return inFrames;
    }
};

void testPassthrough() {
    LinearResampler rs;
    rs.prepare(48000.0, 48000.0, 2);
    CHECK(rs.passthrough(), "48k->48k is passthrough");

    RampSource src; src.channels = 2;
    float out[256 * 2];
    float scratch[256 * 2];
    uint32_t pulled = rs.produce(out, 256, &RampSource::pull, &src, scratch, 256);
    CHECK(pulled == 256, "passthrough pulls exactly outFrames");
    bool ok = true;
    for (uint32_t f = 0; f < 256; ++f) if (out[f * 2] != (float)f) ok = false;
    CHECK(ok, "passthrough is bit-exact ramp");
}

void testConsumedMatchesPredicted() {
    LinearResampler rs;
    rs.prepare(48000.0, 96000.0, 2);     // ratio 0.5 (upsample)
    CHECK(std::fabs(rs.ratio() - 0.5) < 1e-12, "ratio 0.5");

    RampSource src; src.channels = 2;
    std::vector<float> out(1024 * 2), scratch(1024 * 2);

    uint32_t prevNext = 0;
    for (int blk = 0; blk < 50; ++blk) {
        uint32_t predicted = rs.inputFramesFor(512);
        uint32_t pulled = rs.produce(out.data(), 512, &RampSource::pull, &src,
                                     scratch.data(), 1024);
        CHECK(pulled == predicted, "pulled == inputFramesFor (no over-read)");
        // Source advanced by exactly `pulled` frames each block → contiguous.
        CHECK(src.next - prevNext == pulled, "source consumed contiguously");
        prevNext = src.next;
    }
}

void testUpsampleRampMonotonic() {
    LinearResampler rs;
    rs.prepare(48000.0, 96000.0, 1);     // mono ramp, clean to reason about
    RampSource src; src.channels = 1;
    std::vector<float> out(2048), scratch(2048);

    // Skip the first block (priming brings prev_/cur_ up from the reset zeros).
    rs.produce(out.data(), 1024, &RampSource::pull, &src, scratch.data(), 2048);
    rs.produce(out.data(), 1024, &RampSource::pull, &src, scratch.data(), 2048);

    bool monotonic = true;
    for (uint32_t i = 1; i < 1024; ++i) {
        if (out[i] < out[i - 1] - 1e-4f) monotonic = false;   // never decreasing
    }
    CHECK(monotonic, "upsampled ramp is monotonically non-decreasing");

    // Consecutive output deltas on a ramp upsampled 2x should be ~0.5/sample.
    double avgDelta = (out[1023] - out[0]) / 1023.0;
    CHECK(std::fabs(avgDelta - 0.5) < 0.05, "upsampled ramp slope ~= ratio (0.5)");
}

void testConstantPreserved() {
    LinearResampler rs;
    rs.prepare(44100.0, 48000.0, 2);     // arbitrary downsample-ish ratio
    ConstSource src; src.v = 0.25f;
    std::vector<float> out(1024 * 2), scratch(1024 * 2);

    // Prime, then check a steady block holds the constant (within interp error,
    // which for a constant signal is zero once primed).
    rs.produce(out.data(), 512, &ConstSource::pull, &src, scratch.data(), 1024);
    rs.produce(out.data(), 512, &ConstSource::pull, &src, scratch.data(), 1024);
    bool ok = true;
    for (uint32_t i = 0; i < 512 * 2; ++i) if (std::fabs(out[i] - 0.25f) > 1e-5f) ok = false;
    CHECK(ok, "constant input → constant output");
}

void testDrySourceNoCrash() {
    LinearResampler rs;
    rs.prepare(48000.0, 96000.0, 2);
    RampSource src; src.channels = 2; src.dry = true;
    std::vector<float> out(512 * 2), scratch(512 * 2);
    uint32_t pulled = rs.produce(out.data(), 512, &RampSource::pull, &src,
                                 scratch.data(), 512);
    CHECK(pulled == 0, "dry source pulls nothing");
    // Output should be finite (zeros), never NaN.
    bool finite = true;
    for (float v : out) if (!std::isfinite(v)) finite = false;
    CHECK(finite, "dry source output is finite");
}

} // namespace

int main() {
    testPassthrough();
    testConsumedMatchesPredicted();
    testUpsampleRampMonotonic();
    testConstantPreserved();
    testDrySourceNoCrash();

    if (g_failures == 0) { printf("all resampler tests passed\n"); return 0; }
    fprintf(stderr, "%d resampler check(s) failed\n", g_failures);
    return 1;
}
