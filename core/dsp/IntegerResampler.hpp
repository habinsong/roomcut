/*
 * IntegerResampler.hpp — takes a stream down by a whole factor and back up,
 * for a renderer that cannot run at the stream's own rate.
 *
 * AUSpatialMixer is the case: its interaural delay stops growing somewhere
 * around 80-90 samples, so above 96 kHz a lateral channel's ITD shrinks (at
 * 192 kHz a speaker at 90 degrees gets 483 us instead of 723). A 176.4-768 kHz
 * stream therefore renders its bed at 88.2 / 96 kHz and comes back up.
 *
 * Only the audible band has to survive the trip. Both directions use the same
 * linear-phase low-pass, designed at the stream rate:
 *   - flat to 24 kHz,
 *   - kStopDb down from (rate / factor - 24 kHz) up to the stream's Nyquist,
 *     which covers everything that would fold into 0-24 kHz on the way down
 *     and every image of 0-24 kHz on the way up.
 * Between the two edges the filter rolls off; what lands there is ultrasonic.
 *
 * Kaiser-windowed sinc with the length from Kaiser's estimate, kept odd so each
 * direction delays by a whole (length - 1) / 2 frames of the stream rate.
 *
 * Real-time safe after prepare(): fixed arrays, no allocation, no locks.
 */
#ifndef ROOMCUT_INTEGER_RESAMPLER_HPP
#define ROOMCUT_INTEGER_RESAMPLER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace roomcut {

namespace integer_resampler {

constexpr std::size_t kMaxFactor = 8;
constexpr std::size_t kMaxTaps = 127;
constexpr std::size_t kMaxChannels = 7;
constexpr double kPassHz = 24000.0;
constexpr double kStopDb = 100.0;

inline double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= x * x / (4.0 * k * k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

// Writes the low-pass for going between `rate` and rate / factor into `taps`
// (unity DC gain) and returns its length.
inline std::size_t designLowPass(double rate, std::size_t factor, float* taps) {
    constexpr double kPi = 3.14159265358979323846;
    if (factor <= 1 || !(rate > 0.0)) {
        taps[0] = 1.0f;
        return 1;
    }
    const double lowRate = rate / static_cast<double>(factor);
    const double pass = std::min(kPassHz, 0.4 * lowRate);
    const double stop = lowRate - pass;
    const double transition = 2.0 * kPi * (stop - pass) / rate;
    std::size_t length = static_cast<std::size_t>(std::ceil((kStopDb - 7.95) / (2.285 * transition))) + 1;
    length = std::min(length | 1u, kMaxTaps);

    const double cutoff = 0.5 / static_cast<double>(factor);   // cycles per sample: the low rate's Nyquist
    const double beta = 0.1102 * (kStopDb - 8.7);
    const double centre = 0.5 * static_cast<double>(length - 1);
    double h[kMaxTaps] = {};
    double sum = 0.0;
    for (std::size_t n = 0; n < length; ++n) {
        const double x = static_cast<double>(n) - centre;
        const double sinc = x == 0.0 ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * x) / (kPi * x);
        const double u = centre > 0.0 ? x / centre : 0.0;
        h[n] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - u * u))) / besselI0(beta);
        sum += h[n];
    }
    for (std::size_t n = 0; n < length; ++n) taps[n] = static_cast<float>(h[n] / sum);
    return length;
}

inline float dot(const float* a, const float* b, std::size_t n) {
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    std::size_t k = 0;
    for (; k + 3 < n; k += 4) {
        s0 += a[k] * b[k];
        s1 += a[k + 1] * b[k + 1];
        s2 += a[k + 2] * b[k + 2];
        s3 += a[k + 3] * b[k + 3];
    }
    float sum = (s0 + s1) + (s2 + s3);
    for (; k < n; ++k) sum += a[k] * b[k];
    return sum;
}

} // namespace integer_resampler

class IntegerDecimator {
public:
    void prepare(double rate, std::size_t factor, std::size_t channels) {
        factor_ = std::clamp<std::size_t>(factor, 1, integer_resampler::kMaxFactor);
        channels_ = std::min(channels, integer_resampler::kMaxChannels);
        length_ = integer_resampler::designLowPass(rate, factor_, taps_);
        reset();
    }

    void reset() {
        for (auto& channel : history_) std::fill(std::begin(channel), std::end(channel), 0.0f);
        head_ = 0;
        phase_ = 0;
    }

    std::size_t factor() const { return factor_; }
    // In frames of the stream (high) rate.
    std::size_t delayFrames() const { return (length_ - 1) / 2; }

    // in[c] holds frames * factor() samples; out[c] receives frames.
    void process(const float* const* in, float* const* out, std::size_t frames) {
        std::size_t written = 0;
        for (std::size_t i = 0; i < frames * factor_; ++i) {
            head_ = head_ == 0 ? length_ - 1 : head_ - 1;
            for (std::size_t c = 0; c < channels_; ++c) history_[c][head_] = history_[c][head_ + length_] = in[c][i];
            if (phase_ == 0) {
                for (std::size_t c = 0; c < channels_; ++c)
                    out[c][written] = integer_resampler::dot(taps_, history_[c] + head_, length_);
                ++written;
            }
            phase_ = phase_ + 1 == factor_ ? 0 : phase_ + 1;
        }
    }

private:
    std::size_t factor_ = 1, channels_ = 0, length_ = 1, head_ = 0, phase_ = 0;
    float taps_[integer_resampler::kMaxTaps] = {1.0f};
    // Each channel's last length_ samples, twice over, so a window is contiguous.
    float history_[integer_resampler::kMaxChannels][2 * integer_resampler::kMaxTaps] = {};
};

class IntegerInterpolator {
public:
    void prepare(double rate, std::size_t factor, std::size_t channels) {
        factor_ = std::clamp<std::size_t>(factor, 1, integer_resampler::kMaxFactor);
        channels_ = std::min(channels, integer_resampler::kMaxChannels);
        float taps[integer_resampler::kMaxTaps] = {};
        length_ = integer_resampler::designLowPass(rate, factor_, taps);
        // Zero-stuffing and filtering, without the zeros: output phase p of an
        // input frame only ever meets the taps p, p + factor, p + 2 factor ...
        span_ = (length_ + factor_ - 1) / factor_;
        for (std::size_t p = 0; p < factor_; ++p)
            for (std::size_t q = 0; q < span_; ++q) {
                const std::size_t n = p + q * factor_;
                phases_[p][q] = n < length_ ? taps[n] * static_cast<float>(factor_) : 0.0f;
            }
        reset();
    }

    void reset() {
        for (auto& channel : history_) std::fill(std::begin(channel), std::end(channel), 0.0f);
        head_ = 0;
    }

    std::size_t factor() const { return factor_; }
    // In frames of the stream (high) rate.
    std::size_t delayFrames() const { return (length_ - 1) / 2; }

    // in[c] holds frames samples; out[c] receives frames * factor().
    void process(const float* const* in, float* const* out, std::size_t frames) {
        for (std::size_t i = 0; i < frames; ++i) {
            head_ = head_ == 0 ? span_ - 1 : head_ - 1;
            for (std::size_t c = 0; c < channels_; ++c) history_[c][head_] = history_[c][head_ + span_] = in[c][i];
            for (std::size_t p = 0; p < factor_; ++p)
                for (std::size_t c = 0; c < channels_; ++c)
                    out[c][i * factor_ + p] = integer_resampler::dot(phases_[p], history_[c] + head_, span_);
        }
    }

private:
    std::size_t factor_ = 1, channels_ = 0, length_ = 1, span_ = 1, head_ = 0;
    float phases_[integer_resampler::kMaxFactor][integer_resampler::kMaxTaps] = {{1.0f}};
    float history_[integer_resampler::kMaxChannels][2 * integer_resampler::kMaxTaps] = {};
};

} // namespace roomcut

#endif // ROOMCUT_INTEGER_RESAMPLER_HPP
