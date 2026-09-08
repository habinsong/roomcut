#include "SincResampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using Resampler = roomcut::SincResampler;
static int failures = 0;
#define CHECK(condition, message) do { \
    if (!(condition)) { std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } \
} while (0)

struct ToneSource {
    double rate, frequency;
    uint64_t position = 0;
    static uint32_t pull(void* context, float* output, uint32_t frames, uint32_t channels) {
        auto& source = *static_cast<ToneSource*>(context);
        for (uint32_t f = 0; f < frames; ++f) {
            const float sample = 0.25f * std::sin(2 * M_PI * source.frequency * source.position++ / source.rate);
            for (uint32_t c = 0; c < channels; ++c) output[f * channels + c] = c % 2 ? -sample : sample;
        }
        return frames;
    }
};

struct ToneResult { double gainDb, residualDb, rmsDb; };

static ToneResult measure(double inputRate, double outputRate, double frequency) {
    Resampler resampler;
    resampler.prepare(inputRate, outputRate, 2);
    ToneSource source{inputRate, frequency};
    const uint32_t block = 257;
    std::vector<float> output(block * 2);
    const auto scratchFrames = static_cast<uint32_t>(std::ceil(block * inputRate / outputRate)) + 8;
    std::vector<float> scratch(scratchFrames * 2);
    // Let the interpolation filter settle before evaluating a coherent 100ms tone.
    for (uint32_t n = 0; n < static_cast<uint32_t>(outputRate * 0.05); n += block)
        resampler.produce(output.data(), block, ToneSource::pull, &source, scratch.data(), scratchFrames);
    const auto count = static_cast<uint32_t>(std::lround(outputRate * 0.1));
    std::vector<double> measured(count);
    double sine = 0, cosine = 0, energy = 0;
    double sineSquared = 0, cosineSquared = 0, sineCosine = 0;
    for (uint32_t at = 0; at < count;) {
        const auto frames = std::min(block, count - at);
        resampler.produce(output.data(), frames, ToneSource::pull, &source, scratch.data(), scratchFrames);
        for (uint32_t f = 0; f < frames; ++f) {
            const double phase = 2 * M_PI * frequency * (at + f) / outputRate;
            const double sample = output[f * 2];
            measured[at + f] = sample;
            const double s = std::sin(phase), c = std::cos(phase);
            sine += sample * s;
            cosine += sample * c;
            sineSquared += s * s;
            cosineSquared += c * c;
            sineCosine += s * c;
            energy += sample * sample;
            CHECK(output[f * 2 + 1] == -output[f * 2], "resampling preserves channel phase and gain symmetry");
        }
        at += frames;
    }
    // Least-squares fitting also works when a device's non-integer nominal
    // rate makes the measurement window contain a fractional number of cycles.
    const double determinant = sineSquared * cosineSquared - sineCosine * sineCosine;
    const double sineGain = (sine * cosineSquared - cosine * sineCosine) / determinant;
    const double cosineGain = (cosine * sineSquared - sine * sineCosine) / determinant;
    const double amplitude = std::hypot(sineGain, cosineGain);
    const double power = energy / count;
    double residualEnergy = 0;
    for (uint32_t n = 0; n < count; ++n) {
        const double phase = 2 * M_PI * frequency * n / outputRate;
        const double reconstructed = sineGain * std::sin(phase) + cosineGain * std::cos(phase);
        const double error = measured[n] - reconstructed;
        residualEnergy += error * error;
    }
    const double residual = std::sqrt(residualEnergy / count);
    const auto db = [](double value) { return 20 * std::log10(std::max(value, 1e-15)); };
    return {db(amplitude / 0.25), db(residual / (0.25 / std::sqrt(2.0))),
            db(std::sqrt(power) / (0.25 / std::sqrt(2.0)))};
}

static void passbandAndInterpolationImages() {
    for (const auto rates : {std::pair{44100.,48000.}, {48000.,44100.}, {48000.,96000.},
                             {96000.,48000.}, {192000.,44100.}, {44100.,768000.},
                             {44100.25,48000.75}, {48000.75,44100.25}, {8000.,48000.}, {48000.,8000.}}) {
        for (double frequency : {20., 100., 1000., 3500., 10000., 19000., 20000.}) {
            if (frequency >= std::min(rates.first, rates.second) * 0.5) continue;
            const auto result = measure(rates.first, rates.second, frequency);
            std::printf("pass %.2f->%.2f %.0fHz gain=%.4fdB residual=%.1fdB\n",
                        rates.first, rates.second, frequency, result.gainDb, result.residualDb);
            CHECK(std::abs(result.gainDb) < 0.05, "audible-band level error stays below 0.05 dB");
            CHECK(result.residualDb < -100, "interpolation images and modulation stay below -100 dB");
        }
    }
}

static void downsamplingRejectsOutOfBandInput() {
    struct Case { double inRate, outRate, frequency; };
    for (const auto test : {Case{96000,48000,30000}, {96000,48000,24010},
                           {96000,44100,23000}, {48000,44100,22060}, {96000,44100.25,23000},
                           {192000,48000,70000}, {768000,44100,60000},
                           {48000,44100,23000}, {192000,96000,60000},
                           {48000,8000,5000}, {44100,8000,4100}}) {
        const auto result = measure(test.inRate, test.outRate, test.frequency);
        std::printf("stop %.2f->%.2f %.0fHz alias=%.1fdB\n",
                    test.inRate, test.outRate, test.frequency, result.rmsDb);
        CHECK(result.rmsDb < -100, "downsampling rejects out-of-band tones by at least 100 dB");
    }
}

static void stopbandFrequencyGrid() {
    for (const auto rates : {std::pair{96000.,48000.}, {192000.,44100.}, {768000.,48000.}}) {
        double worst = -300;
        for (int step = 0; step < 32; ++step) {
            double frequency = std::round((rates.second * 0.5 + 10
                               + step * (rates.first * 0.5 - rates.second * 0.5 - 110) / 31) / 10) * 10;
            if (std::fmod(frequency, rates.second * 0.5) == 0) frequency += 10;
            worst = std::max(worst, measure(rates.first, rates.second, frequency).rmsDb);
        }
        std::printf("stop grid %.2f->%.2f worst=%.1fdB\n", rates.first, rates.second, worst);
        CHECK(worst < -100, "stop-band rejection holds across the source spectrum");
    }
}

int main() {
    passbandAndInterpolationImages();
    downsamplingRejectsOutOfBandInput();
    stopbandFrequencyGrid();
    if (failures) std::fprintf(stderr, "%d resampler quality checks failed\n", failures);
    else std::puts("all resampler quality tests passed");
    return failures ? 1 : 0;
}
