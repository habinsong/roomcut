#ifndef ROOMCUT_ANALYZER_HPP
#define ROOMCUT_ANALYZER_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace roomcut {

struct AnalysisSnapshot {
    static constexpr std::size_t kSpectrumBins = 24;

    bool valid = false;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint64_t framesAnalyzed = 0;
    float peakDb = -120.0f;
    float rmsDb = -120.0f;
    float crestFactor = 0.0f;
    float lowEnergy = 0.0f;
    float lowMidEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;
    float spectralCentroid = 0.0f;
    float stereoWidth = 0.0f;
    float midSideRatio = 1.0f;
    float correlation = 1.0f;   // L/R correlation: +1 mono, 0 wide, <0 phase risk
    float muddiness = 0.0f;
    float harshness = 0.0f;
    float sibilance = 0.0f;
    float voicePresence = 0.0f;
    float reverbEstimate = 0.0f;
    float dynamicRange = 0.0f;
    std::array<float, kSpectrumBins> spectrum{};
};

class Analyzer {
public:
    AnalysisSnapshot analyzeInterleaved(const float* samples,
                                        std::size_t frames,
                                        std::size_t channels,
                                        double sampleRate) {
        AnalysisSnapshot out;
        out.sampleRate = sampleRate > 0.0 ? (uint32_t)std::lround(sampleRate) : 0;
        out.channels = (uint32_t)channels;
        out.framesAnalyzed = frames;
        if (samples == nullptr || frames == 0 || channels == 0 || sampleRate <= 0.0) {
            return out;
        }
        prepareSpectralCache(frames, sampleRate);

        double sumSq = 0.0;
        double peak = 0.0;
        double midSq = 0.0;
        double sideSq = 0.0;
        double llSq = 0.0;
        double rrSq = 0.0;
        double lrSum = 0.0;
        for (std::size_t f = 0; f < frames; ++f) {
            const float* frame = samples + f * channels;
            const double l = frame[0];
            const double r = channels > 1 ? frame[1] : l;
            const double mono = 0.5 * (l + r);
            sumSq += mono * mono;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));

            const double mid = 0.5 * (l + r);
            const double side = 0.5 * (l - r);
            midSq += mid * mid;
            sideSq += side * side;

            llSq += l * l;
            rrSq += r * r;
            lrSum += l * r;
        }

        const double rms = std::sqrt(sumSq / (double)frames);
        out.valid = rms > 1.0e-6 || peak > 1.0e-5;
        out.peakDb = ampToDb(peak);
        out.rmsDb = ampToDb(rms);
        out.crestFactor = (float)(rms > 1.0e-12 ? peak / rms : 0.0);
        out.dynamicRange = (float)std::max(0.0, (double)out.peakDb - (double)out.rmsDb);

        const double midRms = std::sqrt(midSq / (double)frames);
        const double sideRms = std::sqrt(sideSq / (double)frames);
        const double stereoTotal = midRms + sideRms;
        if (stereoTotal > 1.0e-12) {
            out.stereoWidth = (float)clamp01(sideRms / stereoTotal);
            out.midSideRatio = (float)clamp01(midRms / stereoTotal);
        }

        // L/R correlation (Pearson): +1 = mono/in-phase, 0 = fully decorrelated,
        // negative = out-of-phase (mono-summing risk). Safe listening band ≈ +0.3…+1.
        const double corrDenom = std::sqrt(llSq * rrSq);
        if (corrDenom > 1.0e-12) {
            out.correlation = (float)std::max(-1.0, std::min(1.0, lrSum / corrDenom));
        }

        fillSpectrum(samples, frames, channels, out);
        classify(out);
        return out;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    std::size_t cachedFrames_ = 0;
    double cachedSampleRate_ = 0.0;
    std::vector<double> hannWindow_;
    std::array<double, AnalysisSnapshot::kSpectrumBins> binFreqs_{};
    std::array<double, AnalysisSnapshot::kSpectrumBins> binCoeffs_{};

    static float ampToDb(double amp) {
        return (float)(20.0 * std::log10(std::max(amp, 1.0e-6)));
    }

    static double clamp01(double v) {
        return std::min(1.0, std::max(0.0, v));
    }

    static double logBinFrequency(std::size_t bin, double sampleRate) {
        const double lo = 31.0;
        const double hi = std::min(20000.0, sampleRate * 0.45);
        const double t = (double)bin / (double)(AnalysisSnapshot::kSpectrumBins - 1);
        return lo * std::pow(hi / lo, t);
    }

    static double hann(std::size_t i, std::size_t n) {
        if (n <= 1) return 1.0;
        return 0.5 - 0.5 * std::cos(2.0 * kPi * (double)i / (double)(n - 1));
    }

    void prepareSpectralCache(std::size_t frames, double sampleRate) {
        if (frames == cachedFrames_ && std::fabs(sampleRate - cachedSampleRate_) < 0.5) {
            return;
        }
        cachedFrames_ = frames;
        cachedSampleRate_ = sampleRate;
        hannWindow_.resize(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            hannWindow_[i] = hann(i, frames);
        }
        for (std::size_t b = 0; b < AnalysisSnapshot::kSpectrumBins; ++b) {
            const double f = logBinFrequency(b, sampleRate);
            binFreqs_[b] = f;
            binCoeffs_[b] = 2.0 * std::cos(2.0 * kPi * f / sampleRate);
        }
    }

    double goertzelAmplitude(const float* samples,
                             std::size_t frames,
                             std::size_t channels,
                             std::size_t bin) const {
        const double coeff = binCoeffs_[bin];
        double s1 = 0.0;
        double s2 = 0.0;
        double wsum = 0.0;
        for (std::size_t i = 0; i < frames; ++i) {
            const float* frame = samples + i * channels;
            const double mono = channels > 1
                ? 0.5 * ((double)frame[0] + (double)frame[1])
                : (double)frame[0];
            const double w = hannWindow_[i];
            const double s0 = mono * w + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
            wsum += w;
        }
        const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        return 2.0 * std::sqrt(std::max(0.0, power)) / std::max(1.0e-12, wsum);
    }

    void fillSpectrum(const float* samples,
                      std::size_t frames,
                      std::size_t channels,
                      AnalysisSnapshot& out) const {
        double total = 0.0;
        double weightedHz = 0.0;
        double low = 0.0;
        double lowMid = 0.0;
        double mid = 0.0;
        double high = 0.0;
        double harsh = 0.0;
        double sibilant = 0.0;
        double voice = 0.0;

        for (std::size_t b = 0; b < AnalysisSnapshot::kSpectrumBins; ++b) {
            const double f = binFreqs_[b];
            const double amp = goertzelAmplitude(samples, frames, channels, b);
            const double energy = amp * amp;
            total += energy;
            weightedHz += energy * f;
            out.spectrum[b] = (float)clamp01((ampToDb(amp) + 80.0f) / 80.0f);

            if (f < 120.0) low += energy;
            else if (f < 500.0) lowMid += energy;
            else if (f < 4000.0) mid += energy;
            else high += energy;

            if (f >= 2500.0 && f <= 6000.0) harsh += energy;
            if (f >= 6000.0 && f <= 11000.0) sibilant += energy;
            if (f >= 1000.0 && f <= 4000.0) voice += energy;
        }

        const double denom = std::max(total, 1.0e-12);
        out.lowEnergy = (float)clamp01(low / denom);
        out.lowMidEnergy = (float)clamp01(lowMid / denom);
        out.midEnergy = (float)clamp01(mid / denom);
        out.highEnergy = (float)clamp01(high / denom);
        out.spectralCentroid = (float)(weightedHz / denom);
        out.harshness = (float)clamp01(harsh / denom * 2.3);
        out.sibilance = (float)clamp01(sibilant / denom * 2.5);
        out.voicePresence = (float)clamp01(voice / denom * 1.9);
    }

    static void classify(AnalysisSnapshot& out) {
        out.muddiness = (float)clamp01((out.lowMidEnergy - 0.18) / 0.34);
        out.harshness = (float)clamp01(out.harshness);
        out.sibilance = (float)clamp01(out.sibilance);
        out.voicePresence = (float)clamp01(out.voicePresence);
        const double room = out.stereoWidth * 0.70
            + out.lowMidEnergy * 0.20
            + std::max(0.0, (double)out.dynamicRange - 8.0) * 0.018
            - out.voicePresence * 0.12;
        out.reverbEstimate = (float)clamp01(room);
    }
};

} // namespace roomcut

#endif // ROOMCUT_ANALYZER_HPP
