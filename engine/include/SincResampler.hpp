#ifndef ROOMCUT_SINC_RESAMPLER_HPP
#define ROOMCUT_SINC_RESAMPLER_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace roomcut {

// Causal, band-limited streaming conversion. Coefficients and the mirrored
// history are allocated only in prepare(); produce() uses caller-owned scratch.
// The Kaiser-windowed sinc cuts off below the lower Nyquist frequency. Its
// support grows for downsampling so stop-band rejection does not deteriorate.
class SincResampler {
public:
    using InputFn = uint32_t (*)(void*, float*, uint32_t, uint32_t);
    static constexpr uint32_t kMaxChannels = 8;

    void prepare(double inputRate, double outputRate, uint32_t channels) {
        if (!std::isfinite(inputRate) || !std::isfinite(outputRate)
            || inputRate <= 0 || outputRate <= 0
            || channels == 0 || channels > kMaxChannels)
            throw std::invalid_argument("unsupported resampler format");
        const double ratio = inputRate / outputRate;
        const double half = inputRate == outputRate ? 0 : std::ceil(kHalfWidth * std::max(1.0, ratio));
        if (!std::isfinite(ratio) || ratio <= 0 || half > (std::numeric_limits<uint32_t>::max() - 1) / 2)
            throw std::length_error("resampler ratio exceeds addressable history");
        ratio_ = ratio;
        channels_ = channels;
        inputRate_ = inputRate;
        coefficients_.clear();
        history_.clear();
        half_ = static_cast<uint32_t>(half);
        taps_ = half_ * 2 + 1;
        phases_ = 1;
        step_ = 1;
        if (!passthrough()) {
            // Integer nominal rates use their exact rational phases and an
            // integer-valued clock: no table interpolation or accumulated drift.
            phases_ = static_cast<uint32_t>(std::ceil(1024 / std::max(1.0, ratio_)));
            step_ = ratio_ * phases_;
            if (inputRate == std::floor(inputRate) && outputRate == std::floor(outputRate)
                && inputRate <= UINT32_MAX && outputRate <= UINT32_MAX) {
                const auto inHz = static_cast<uint32_t>(inputRate);
                const auto outHz = static_cast<uint32_t>(outputRate);
                const auto divisor = std::gcd(inHz, outHz);
                if (outHz / divisor <= 4096) {
                    phases_ = outHz / divisor;
                    step_ = inHz / divisor;
                }
            }
            buildCoefficients();
            history_.resize(static_cast<std::size_t>(channels_) * taps_ * 2);
        }
        reset();
    }

    void reset() {
        phase_ = phases_; // pull the first source frame before the first output
        head_ = 0;
        std::fill(history_.begin(), history_.end(), 0.0);
    }

    bool passthrough() const { return ratio_ == 1.0; }
    double ratio() const { return ratio_; }
    double latencySeconds() const { return half_ / inputRate_; }

    uint32_t inputFramesFor(uint32_t outputFrames) const {
        if (passthrough()) return outputFrames;
        double phase = phase_;
        uint32_t total = 0;
        for (uint32_t f = 0; f < outputFrames; ++f) {
            const auto count = static_cast<uint32_t>(phase / phases_);
            total += count;
            phase -= static_cast<double>(count) * phases_;
            phase += step_;
        }
        return total;
    }

    uint32_t produce(float* output, uint32_t outputFrames, InputFn input, void* context,
                     float* scratch, uint32_t scratchFrames) {
        if (outputFrames == 0) return 0;
        if (passthrough()) {
            const uint32_t got = input(context, output, outputFrames, channels_);
            std::fill(output + static_cast<std::size_t>(got) * channels_,
                      output + static_cast<std::size_t>(outputFrames) * channels_, 0.0f);
            return got;
        }
        // Refill small scratch buffers instead of silently skipping source
        // frames. The zero-capacity fallback also stays allocation-free.
        float oneFrame[kMaxChannels];
        if (scratchFrames == 0) { scratch = oneFrame; scratchFrames = 1; }
        uint32_t remaining = inputFramesFor(outputFrames);
        uint32_t available = 0, at = 0, received = 0;
        for (uint32_t f = 0; f < outputFrames; ++f) {
            const auto consume = static_cast<uint32_t>(phase_ / phases_);
            for (uint32_t n = 0; n < consume; ++n) {
                if (at == available) {
                    available = std::min(remaining, scratchFrames);
                    const uint32_t got = input(context, scratch, available, channels_);
                    received += got;
                    std::fill(scratch + static_cast<std::size_t>(got) * channels_,
                              scratch + static_cast<std::size_t>(available) * channels_, 0.0f);
                    remaining -= available;
                    at = 0;
                }
                push(scratch + static_cast<std::size_t>(at++) * channels_);
            }
            phase_ -= static_cast<double>(consume) * phases_;
            const auto phaseIndex = static_cast<uint32_t>(phase_);
            const double blend = phase_ - phaseIndex;
            const double* first = coefficients_.data() + static_cast<std::size_t>(phaseIndex) * taps_;
            const double* second = first + taps_;
            for (uint32_t c = 0; c < channels_; ++c) {
                const double* samples = history_.data() + static_cast<std::size_t>(c) * taps_ * 2 + head_;
                double value = dot(first, samples);
                if (blend != 0.0) value += blend * (dot(second, samples) - value);
                output[static_cast<std::size_t>(f) * channels_ + c] = static_cast<float>(value);
            }
            phase_ += step_;
        }
        return received;
    }

private:
    static constexpr double kHalfWidth = 128;
    static constexpr double kBeta = 12;
    static constexpr double kPi = 3.14159265358979323846;

    static double bessel0(double x) {
        double sum = 1, term = 1;
        for (int k = 1; k < 40; ++k) {
            term *= x * x / (4.0 * k * k);
            sum += term;
            if (term < sum * 1e-16) break;
        }
        return sum;
    }

    void buildCoefficients() {
        coefficients_.resize(static_cast<std::size_t>(phases_ + 1) * taps_);
        const double cutoff = 0.95 / std::max(1.0, ratio_);
        const double windowScale = 1.0 / bessel0(kBeta);
        for (uint32_t p = 0; p <= phases_; ++p) {
            double* row = coefficients_.data() + static_cast<std::size_t>(p) * taps_;
            double sum = 0;
            for (uint32_t k = 0; k < taps_; ++k) {
                const double distance = static_cast<double>(k) - half_ + static_cast<double>(p) / phases_;
                const double u = distance / half_;
                const double x = kPi * cutoff * distance;
                const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(x) / x;
                const double window = std::abs(u) >= 1 ? 0 : bessel0(kBeta * std::sqrt(1 - u * u)) * windowScale;
                row[k] = cutoff * sinc * window;
                sum += row[k];
            }
            for (uint32_t k = 0; k < taps_; ++k) row[k] /= sum;
        }
    }

    void push(const float* frame) {
        head_ = head_ == 0 ? taps_ - 1 : head_ - 1;
        for (uint32_t c = 0; c < channels_; ++c) {
            const double value = std::isfinite(frame[c]) ? frame[c] : 0.0;
            const auto index = static_cast<std::size_t>(c) * taps_ * 2 + head_;
            history_[index] = history_[index + taps_] = value;
        }
    }

    double dot(const double* coefficients, const double* samples) const {
        std::size_t k = 0;
#if defined(__aarch64__)
        float64x2_t a = vdupq_n_f64(0), b = vdupq_n_f64(0);
        for (; k + 3 < taps_; k += 4) {
            a = vfmaq_f64(a, vld1q_f64(coefficients + k), vld1q_f64(samples + k));
            b = vfmaq_f64(b, vld1q_f64(coefficients + k + 2), vld1q_f64(samples + k + 2));
        }
        double sum = vaddvq_f64(a) + vaddvq_f64(b);
#else
        double a = 0, b = 0, c = 0, d = 0;
        for (; k + 3 < taps_; k += 4) {
            a += coefficients[k] * samples[k];
            b += coefficients[k + 1] * samples[k + 1];
            c += coefficients[k + 2] * samples[k + 2];
            d += coefficients[k + 3] * samples[k + 3];
        }
        double sum = (a + b) + (c + d);
#endif
        for (; k < taps_; ++k) sum += coefficients[k] * samples[k];
        return sum;
    }

    double ratio_ = 1, inputRate_ = 48000;
    uint32_t channels_ = 2, half_ = 0, taps_ = 1, phases_ = 1;
    double phase_ = 1, step_ = 1;
    uint32_t head_ = 0;
    std::vector<double> coefficients_, history_;
};

} // namespace roomcut
#endif
