#include "Analyzer.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

std::vector<float> sine(double freq, double amp, bool antiPhase = false) {
    constexpr int sr = 48000;
    constexpr int frames = 2048;
    std::vector<float> out((size_t)frames * 2, 0.0f);
    for (int f = 0; f < frames; ++f) {
        const float x = (float)(amp * std::sin(2.0 * kPi * freq * (double)f / (double)sr));
        out[(size_t)f * 2] = x;
        out[(size_t)f * 2 + 1] = antiPhase ? -x : x;
    }
    return out;
}

void test_level_and_centroid() {
    auto input = sine(1000.0, 0.5);
    roomcut::Analyzer analyzer;
    auto a = analyzer.analyzeInterleaved(input.data(), 2048, 2, 48000.0);
    check(a.valid, "sine is valid");
    check(std::fabs(a.peakDb - -6.02f) < 0.2f, "peak dB");
    check(std::fabs(a.rmsDb - -9.03f) < 0.4f, "rms dB");
    check(a.spectralCentroid > 700.0f && a.spectralCentroid < 1400.0f, "centroid tracks 1k tone");
    check(a.voicePresence > 0.2f, "voice band energy present");
}

void test_stereo_width() {
    auto input = sine(440.0, 0.4, true);
    roomcut::Analyzer analyzer;
    auto a = analyzer.analyzeInterleaved(input.data(), 2048, 2, 48000.0);
    check(a.stereoWidth > 0.95f, "anti-phase tone is wide");
    check(a.midSideRatio < 0.05f, "anti-phase tone has low mid ratio");
}

void test_band_energy_changes() {
    roomcut::Analyzer analyzer;
    auto lowMidInput = sine(250.0, 0.4);
    auto highInput = sine(8000.0, 0.4);
    auto lowMid = analyzer.analyzeInterleaved(lowMidInput.data(), 2048, 2, 48000.0);
    auto high = analyzer.analyzeInterleaved(highInput.data(), 2048, 2, 48000.0);
    check(lowMid.lowMidEnergy > lowMid.highEnergy, "250 Hz favors low-mid");
    check(high.highEnergy > high.lowMidEnergy, "8 kHz favors high");
    check(high.sibilance > lowMid.sibilance, "8 kHz raises sibilance estimate");
}

}

int main() {
    test_level_and_centroid();
    test_stereo_width();
    test_band_energy_changes();
    return 0;
}
