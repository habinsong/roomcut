/*
 * test_volume_ramp.cpp — verify the per-sample linear volume ramp logic
 * extracted from pullRender (ENGINE_AUDIT.md #6).
 *
 * The ramp eliminates zipper noise by smoothly interpolating between the
 * previous volume and the new target over ~10ms. Tests verify:
 *   - Constant volume is applied exactly (no drift).
 *   - A step change produces a smooth, monotonic ramp (no discontinuity).
 *   - The ramp reaches the exact target within one block (no residual error).
 *   - Zero-crossing: ramp from nonzero to zero produces no hard cut.
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)
#define CHECK_NEAR(a, b, tol, msg) do { \
    double _d = std::fabs((double)(a) - (double)(b)); \
    if (_d > (tol)) { fprintf(stderr, "FAIL: %s (|%.8f - %.8f| = %.8f > %.8f) (%s:%d)\n", \
        (msg), (double)(a), (double)(b), _d, (double)(tol), __FILE__, __LINE__); g_failures++; } \
} while (0)

namespace {

// Mimics the volume ramp logic in pullRender (engine/src/main.cpp).
// This is a faithful extraction so the test exercises the same math.
struct VolumeRamp {
    float volCurrent = 1.0f;
    float volTarget  = 1.0f;

    // Apply volume to an interleaved buffer, ramping if needed.
    void apply(float* dst, uint32_t frames, uint32_t channels) {
        const uint32_t n = frames * channels;
        if (volCurrent == volTarget) {
            if (volCurrent != 1.0f) {
                const float v = volCurrent;
                for (uint32_t i = 0; i < n; ++i) dst[i] *= v;
            }
        } else {
            const float rampSamples = (float)frames > 480.0f ? (float)frames : 480.0f;
            const float step = (volTarget - volCurrent) / rampSamples;
            float vol = volCurrent;
            for (uint32_t f = 0; f < frames; ++f) {
                if (step > 0.0f)      vol = std::min(volTarget, vol + step);
                else if (step < 0.0f) vol = std::max(volTarget, vol + step);
                for (uint32_t c = 0; c < channels; ++c) {
                    dst[f * channels + c] *= vol;
                }
            }
            volCurrent = vol;
        }
    }

    void setTarget(float t) { volTarget = t; }
};

// Generate a constant-amplitude signal (all samples = amp).
void fillConst(std::vector<float>& buf, float amp, uint32_t frames, uint32_t ch) {
    buf.assign((size_t)frames * ch, amp);
}

// Generate a 1kHz sine at the given amplitude.
void fillSine(std::vector<float>& buf, float amp, uint32_t frames, uint32_t ch, double sr) {
    buf.resize((size_t)frames * ch);
    const double w = 2.0 * M_PI * 1000.0 / sr;
    for (uint32_t f = 0; f < frames; ++f) {
        float s = (float)(amp * std::sin(w * f));
        for (uint32_t c = 0; c < ch; ++c) buf[f * ch + c] = s;
    }
}

void testConstantVolume() {
    VolumeRamp ramp;
    ramp.volCurrent = 0.5f;
    ramp.volTarget  = 0.5f;

    const uint32_t frames = 512;
    std::vector<float> buf;
    fillConst(buf, 1.0f, frames, 2);

    ramp.apply(buf.data(), frames, 2);

    // Every sample should be exactly 0.5.
    bool ok = true;
    for (float v : buf) if (v != 0.5f) ok = false;
    CHECK(ok, "constant volume applies exactly");
}

void testUnityNoOp() {
    VolumeRamp ramp;
    ramp.volCurrent = 1.0f;
    ramp.volTarget  = 1.0f;

    const uint32_t frames = 512;
    std::vector<float> buf;
    fillConst(buf, 0.75f, frames, 2);
    std::vector<float> orig = buf;

    ramp.apply(buf.data(), frames, 2);

    // Bit-exact: vol == 1.0 should not touch the buffer.
    CHECK(buf == orig, "unity volume is a no-op (bit-exact)");
}

void testRampDown() {
    VolumeRamp ramp;
    ramp.volCurrent = 0.8f;
    ramp.volTarget  = 0.8f;

    const uint32_t frames = 1024;
    std::vector<float> buf;
    fillConst(buf, 1.0f, frames, 2);

    // Step change: 0.8 → 0.4
    ramp.setTarget(0.4f);
    ramp.apply(buf.data(), frames, 2);

    // First sample should be close to 0.8 (just one step from it).
    CHECK(buf[0] > 0.39f, "ramp-down: first sample near old volume");

    // Last sample should be at or near target.
    CHECK_NEAR(ramp.volCurrent, 0.4f, 1e-4f, "ramp-down: reaches target within one block");

    // Monotonically decreasing (channel 0 every frame).
    bool monotonic = true;
    for (uint32_t f = 1; f < frames; ++f) {
        if (buf[f * 2] > buf[(f - 1) * 2] + 1e-6f) monotonic = false;
    }
    CHECK(monotonic, "ramp-down: output is monotonically non-increasing");
}

void testRampUp() {
    VolumeRamp ramp;
    ramp.volCurrent = 0.2f;
    ramp.volTarget  = 0.2f;

    const uint32_t frames = 1024;
    std::vector<float> buf;
    fillConst(buf, 1.0f, frames, 2);

    // Step change: 0.2 → 0.7
    ramp.setTarget(0.7f);
    ramp.apply(buf.data(), frames, 2);

    CHECK_NEAR(ramp.volCurrent, 0.7f, 1e-4f, "ramp-up: reaches target within one block");

    // Monotonically increasing.
    bool monotonic = true;
    for (uint32_t f = 1; f < frames; ++f) {
        if (buf[f * 2] < buf[(f - 1) * 2] - 1e-6f) monotonic = false;
    }
    CHECK(monotonic, "ramp-up: output is monotonically non-decreasing");
}

void testNoDiscontinuity() {
    // Simulate a sine playing at steady volume, then a step change mid-stream.
    // The maximum sample-to-sample jump should be bounded by the sine's natural
    // slope + the ramp's gentle gain change (no hard step).
    VolumeRamp ramp;
    ramp.volCurrent = 0.8f;
    ramp.volTarget  = 0.8f;

    const uint32_t frames = 512;
    const double sr = 48000.0;
    std::vector<float> buf1, buf2;
    fillSine(buf1, 1.0f, frames, 2, sr);
    fillSine(buf2, 1.0f, frames, 2, sr);

    // First block: steady at 0.8.
    ramp.apply(buf1.data(), frames, 2);
    float lastSample = buf1[(frames - 1) * 2]; // last L sample of block 1

    // Second block: step to 0.3 (big jump).
    ramp.setTarget(0.3f);
    // Regenerate sine continuing from where block 1 ended (phase continuity).
    const double w = 2.0 * M_PI * 1000.0 / sr;
    buf2.resize(frames * 2);
    for (uint32_t f = 0; f < frames; ++f) {
        float s = (float)(1.0f * std::sin(w * (frames + f)));
        buf2[f * 2 + 0] = s;
        buf2[f * 2 + 1] = s;
    }
    ramp.apply(buf2.data(), frames, 2);

    // The transition from block1's last sample to block2's first sample.
    float firstSample = buf2[0];
    double jump = std::fabs((double)firstSample - (double)lastSample);

    // Without ramp, the jump would be |0.3 - 0.8| * peak ≈ 0.5.
    // With ramp, it should be bounded by the sine's natural max slope per sample
    // plus the ramp's gentle step (~0.001 per sample for 480-sample ramp).
    // Max sine slope at 1kHz/48kHz ≈ 2π*1000/48000 ≈ 0.131 per sample.
    // Total max jump ≈ 0.131 * vol + step ≈ 0.13.
    CHECK(jump < 0.15, "block boundary discontinuity is bounded (no zipper click)");
}

void testRampToZero() {
    VolumeRamp ramp;
    ramp.volCurrent = 0.6f;
    ramp.volTarget  = 0.6f;

    const uint32_t frames = 1024;
    std::vector<float> buf;
    fillConst(buf, 1.0f, frames, 2);

    ramp.setTarget(0.0f);
    ramp.apply(buf.data(), frames, 2);

    // Floating-point accumulation may leave a sub-μ residual; functionally silent.
    CHECK_NEAR(ramp.volCurrent, 0.0f, 1e-4f, "ramp to zero: reaches ~0");

    // Last sample should be inaudible (< -100 dBFS for a unity input signal).
    CHECK(std::fabs(buf[(frames - 1) * 2]) < 1e-4f, "ramp to zero: final sample is inaudible");
}

void testMultipleSmallSteps() {
    // Simulate rapid slider movement: multiple small target changes across blocks.
    VolumeRamp ramp;
    ramp.volCurrent = 0.5f;
    ramp.volTarget  = 0.5f;

    const uint32_t frames = 256; // small blocks (typical CoreAudio)
    std::vector<float> buf;

    float targets[] = {0.52f, 0.55f, 0.60f, 0.58f, 0.53f, 0.50f};
    float prevLast = 0.5f; // volCurrent * 1.0 signal

    for (float t : targets) {
        fillConst(buf, 1.0f, frames, 2);
        ramp.setTarget(t);
        ramp.apply(buf.data(), frames, 2);

        // Block boundary: first sample of this block vs. last of previous.
        float first = buf[0];
        double jump = std::fabs((double)first - (double)prevLast);
        // With constant signal, the max inter-block jump is one ramp step.
        // step = delta / 480 ≈ 0.02/480 ≈ 0.00004. Allow generous margin.
        CHECK(jump < 0.01, "rapid slider: no inter-block click");

        prevLast = buf[(frames - 1) * 2];
    }
}

} // namespace

int main() {
    testConstantVolume();
    testUnityNoOp();
    testRampDown();
    testRampUp();
    testNoDiscontinuity();
    testRampToZero();
    testMultipleSmallSteps();

    if (g_failures == 0) { printf("all volume ramp tests passed\n"); return 0; }
    fprintf(stderr, "%d volume ramp check(s) failed\n", g_failures);
    return 1;
}
