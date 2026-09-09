/*
 * test_dynamic_eq.cpp — EQ-01, the optional dynamic band inside the parametric
 * EQ. The contract from the plan: with the feature off the band is the static
 * filter it always was; with it on, a band only comes down when it is actually
 * loud, by as much as it overshoots and no more than its range, at the same
 * amount on both channels.
 */
#include "ParametricEQ.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)
#define CHECK_NEAR(value, expected, tolerance, message) do { \
    const double v_ = (value), e_ = (expected); \
    if (!(std::fabs(v_ - e_) <= (tolerance))) { \
        std::fprintf(stderr, "FAIL: %s (%d): %.6f vs %.6f\n", message, __LINE__, v_, e_); \
        ++failures; } } while (0)

static ParametricBand dynamicBell(double gainDb, double thresholdDb, double rangeDb,
                                  double attackMs = 10.0, double releaseMs = 200.0) {
    ParametricBand band;
    band.enabled = true;
    band.type = 0;            // bell
    band.freqHz = 1000.0;
    band.gainDb = gainDb;
    band.q = 1.0;
    band.dynamic = true;
    band.thresholdDb = thresholdDb;
    band.rangeDb = rangeDb;
    band.attackMs = attackMs;
    band.releaseMs = releaseMs;
    return band;
}

static double dbToLin(double db) { return std::pow(10.0, db / 20.0); }

// Runs a 1 kHz stereo tone at `levelDb` and returns the output/input level in dB,
// measured after the envelope has settled.
static double settledGainDb(ParametricEQ& eq, double levelDb, double seconds = 1.0,
                            double fs = 48000.0, std::size_t blockFrames = 128) {
    const auto frames = static_cast<std::size_t>(fs * seconds);
    const double amplitude = dbToLin(levelDb);
    double inSq = 0, outSq = 0;
    std::vector<float> block(blockFrames * 2);
    std::size_t written = 0;
    for (std::size_t n = 0; n < frames; ++n) {
        const auto value = static_cast<float>(amplitude * std::sin(2.0 * M_PI * 1000.0 * n / fs));
        block[written * 2] = block[written * 2 + 1] = value;
        ++written;
        if (written == blockFrames) {
            for (std::size_t f = 0; f < written; ++f) eq.processFrame(&block[f * 2], 2);
            // Only the last quarter counts, by which point the envelope has settled.
            if (n > frames * 3 / 4) {
                for (std::size_t f = 0; f < written; ++f) outSq += (double)block[f * 2] * block[f * 2];
            }
            written = 0;
        }
        if (n > frames * 3 / 4) inSq += amplitude * amplitude * std::pow(std::sin(2.0 * M_PI * 1000.0 * n / fs), 2);
    }
    return 10.0 * std::log10(std::max(outSq, 1e-30) / std::max(inSq, 1e-30));
}

static void testTurnedOffTheFrameAndSampleParthsAgree() {
    ParametricBand band;
    band.enabled = true; band.type = 0; band.freqHz = 800.0; band.gainDb = -4.5; band.q = 1.3;
    ParametricEQ viaFrame, viaSample;
    viaFrame.prepare(48000.0); viaSample.prepare(48000.0);
    viaFrame.setBand(0, band); viaSample.setBand(0, band);
    bool identical = true;
    for (std::size_t n = 0; n < 4800; ++n) {
        const auto left = static_cast<float>(0.4 * std::sin(2.0 * M_PI * 700.0 * n / 48000.0));
        const auto right = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 1300.0 * n / 48000.0));
        float frame[2] = {left, right};
        viaFrame.processFrame(frame, 2);
        const float a = viaSample.processSample(left, 0);
        const float b = viaSample.processSample(right, 1);
        if (frame[0] != a || frame[1] != b) identical = false;
    }
    CHECK(identical, "a static band comes out of processFrame bit for bit as before");
}

static void testQuietBandIsLeftAlone() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    eq.setBand(0, dynamicBell(0.0, -30.0, 6.0));
    const double gainDb = settledGainDb(eq, -45.0);
    CHECK_NEAR(gainDb, 0.0, 0.05, "below the threshold the band does nothing");
    CHECK_NEAR(eq.dynamicReductionDb(0), 0.0, 0.01, "and asks for no reduction");
}

static void testLoudBandComesDownByTheOvershoot() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    // A -18 dBFS sine reads -21 dBFS RMS in the band. Threshold -24 → 3 dB over,
    // and the range is wide enough to allow all of it.
    eq.setBand(0, dynamicBell(0.0, -24.0, 12.0));
    const double gainDb = settledGainDb(eq, -18.0);
    CHECK_NEAR(eq.dynamicReductionDb(0), 3.01, 0.15, "reduction tracks the overshoot");
    CHECK_NEAR(gainDb, -3.01, 0.3, "the band is pulled down by that much");
}

static void testRangeCapsHowFarItGoes() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    // 24 dB over the threshold, but the range only allows 4 dB.
    eq.setBand(0, dynamicBell(0.0, -30.0, 4.0));
    const double gainDb = settledGainDb(eq, -6.0);
    CHECK_NEAR(eq.dynamicReductionDb(0), 4.0, 0.01, "reduction stops at the range");
    CHECK_NEAR(gainDb, -4.0, 0.5, "and so does the output");
}

static void testAttackAndReleaseTakeTheirTime() {
    const double fs = 48000.0;
    ParametricEQ eq;
    eq.prepare(fs);
    eq.setBand(0, dynamicBell(0.0, -30.0, 12.0, /*attack*/ 20.0, /*release*/ 300.0));
    auto run = [&](double amplitude, double milliseconds) {
        const auto frames = static_cast<std::size_t>(fs * milliseconds * 0.001);
        for (std::size_t n = 0; n < frames; ++n) {
            const auto v = static_cast<float>(amplitude * std::sin(2.0 * M_PI * 1000.0 * n / fs));
            float frame[2] = {v, v};
            eq.processFrame(frame, 2);
        }
    };
    run(dbToLin(-6.0), 5.0);
    const double early = eq.dynamicReductionDb(0);
    run(dbToLin(-6.0), 500.0);
    const double settled = eq.dynamicReductionDb(0);
    CHECK(early < settled * 0.8, "a 20 ms attack is not there yet after 5 ms");
    CHECK(settled > 10.0, "and gets to the range once the tone stays");

    run(0.0, 500.0);
    const double afterSilence = eq.dynamicReductionDb(0);
    CHECK(afterSilence < settled - 3.0, "release lets go once the band goes quiet");
    run(0.0, 3000.0);
    CHECK_NEAR(eq.dynamicReductionDb(0), 0.0, 0.05, "silence returns the band to its static gain");
}

static void testBothChannelsGetTheSameReduction() {
    const double fs = 48000.0;
    ParametricEQ eq;
    eq.prepare(fs);
    eq.setBand(0, dynamicBell(0.0, -30.0, 8.0));
    // Loud left, quiet right. A per-channel detector would duck only the left and
    // pull the image sideways; the linked one has to move both by the same amount.
    double leftIn = 0, leftOut = 0, rightIn = 0, rightOut = 0;
    const auto frames = static_cast<std::size_t>(fs);
    for (std::size_t n = 0; n < frames; ++n) {
        const double phase = 2.0 * M_PI * 1000.0 * n / fs;
        const auto l = static_cast<float>(dbToLin(-6.0) * std::sin(phase));
        const auto r = static_cast<float>(dbToLin(-40.0) * std::sin(phase));
        float frame[2] = {l, r};
        eq.processFrame(frame, 2);
        if (n > frames * 3 / 4) {
            leftIn += (double)l * l;   leftOut += (double)frame[0] * frame[0];
            rightIn += (double)r * r;  rightOut += (double)frame[1] * frame[1];
        }
    }
    const double leftDb = 10.0 * std::log10(leftOut / leftIn);
    const double rightDb = 10.0 * std::log10(rightOut / rightIn);
    CHECK(leftDb < -7.0, "the loud channel is pulled down");
    CHECK_NEAR(rightDb, leftDb, 0.05, "the quiet channel moves with it, so the image holds");
}

static void testBlockSizeDoesNotChangeTheOutput() {
    auto render = [](std::size_t blockFrames) {
        ParametricEQ eq;
        eq.prepare(48000.0);
        eq.setBand(0, dynamicBell(0.0, -30.0, 9.0));
        std::vector<float> out;
        std::vector<float> block(blockFrames * 2);
        std::size_t written = 0;
        for (std::size_t n = 0; n < 24576; ++n) {
            const auto v = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 1000.0 * n / 48000.0)
                                            * (n < 12000 ? 1.0 : 0.02));
            block[written * 2] = block[written * 2 + 1] = v;
            if (++written == blockFrames) {
                for (std::size_t f = 0; f < written; ++f) eq.processFrame(&block[f * 2], 2);
                out.insert(out.end(), block.begin(), block.begin() + written * 2);
                written = 0;
            }
        }
        return out;
    };
    const auto small = render(64), large = render(512);
    CHECK(small.size() == large.size(), "same number of samples either way");
    bool identical = small.size() == large.size();
    for (std::size_t i = 0; identical && i < small.size(); ++i) identical = small[i] == large[i];
    CHECK(identical, "the caller's block size does not change a single sample");
}

static void testSampleRateDoesNotChangeTheReduction() {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        ParametricEQ eq;
        eq.prepare(fs);
        eq.setBand(0, dynamicBell(0.0, -24.0, 12.0));
        // -12 dBFS sine → -15 dBFS RMS in the band, 9 dB over the threshold.
        const double gainDb = settledGainDb(eq, -12.0, 1.0, fs);
        CHECK_NEAR(eq.dynamicReductionDb(0), 9.01, 0.2, "the same overshoot at any rate");
        CHECK(gainDb < -8.0 && gainDb > -10.5, "and the same audible result");
    }
}

static void testOutputStaysFiniteThroughChangesAndSilence() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    eq.setBand(0, dynamicBell(0.0, -30.0, 10.0));
    bool finite = true;
    double peak = 0;
    for (std::size_t n = 0; n < 96000; ++n) {
        // Bursts, silence, and parameter changes while audio is running.
        const double envelope = (n / 4000) % 2 == 0 ? 0.7 : 0.0;
        const auto v = static_cast<float>(envelope * std::sin(2.0 * M_PI * 1000.0 * n / 48000.0));
        float frame[2] = {v, v};
        if (n == 30000) eq.setBand(0, dynamicBell(-3.0, -36.0, 18.0, 5.0, 80.0));
        if (n == 60000) { ParametricBand off; off.enabled = false; eq.setBand(0, off); }
        eq.processFrame(frame, 2);
        for (float sample : frame) {
            if (!std::isfinite(sample)) finite = false;
            peak = std::max(peak, std::fabs((double)sample));
        }
    }
    CHECK(finite, "no NaN or infinity through bursts, changes and silence");
    CHECK(peak < 1.5, "and nothing runs away");
    CHECK_NEAR(eq.dynamicReductionDb(0), 0.0, 0.05, "a disabled band asks for nothing");
}

int main() {
    testTurnedOffTheFrameAndSampleParthsAgree();
    testQuietBandIsLeftAlone();
    testLoudBandComesDownByTheOvershoot();
    testRangeCapsHowFarItGoes();
    testAttackAndReleaseTakeTheirTime();
    testBothChannelsGetTheSameReduction();
    testBlockSizeDoesNotChangeTheOutput();
    testSampleRateDoesNotChangeTheReduction();
    testOutputStaysFiniteThroughChangesAndSilence();
    if (failures == 0) {
        std::printf("all dynamic EQ tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d dynamic EQ check(s) failed\n", failures);
    return 1;
}
