// IntegerResampler: the whole-factor trip a 176.4-768 kHz bed takes to reach a
// renderer that runs at 88.2 / 96 kHz and back. Checked by what it promises:
// the audible band passes flat and on time, nothing folds into it or images
// out of it, the block size never changes the result, and nothing allocates.
#include "IntegerResampler.hpp"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

static bool g_recording = false;
static std::size_t g_allocated = 0;
void* operator new(std::size_t bytes) {
    if (g_recording) g_allocated += bytes;
    void* p = std::malloc(bytes ? bytes : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

using namespace roomcut;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Case { double rate; std::size_t factor; };
constexpr Case kCases[] = {{176400.0, 2}, {192000.0, 2}, {352800.0, 4}, {384000.0, 4}, {705600.0, 8}, {768000.0, 8}};

double responseDb(const float* taps, std::size_t length, double hz, double rate) {
    std::complex<double> sum = 0.0;
    for (std::size_t n = 0; n < length; ++n) sum += static_cast<double>(taps[n]) * std::polar(1.0, -2.0 * kPi * hz / rate * static_cast<double>(n));
    return 20.0 * std::log10(std::max(std::abs(sum), 1e-300));
}

// Amplitude of `hz` in x (least squares against a sine and a cosine).
double amplitude(const std::vector<float>& x, std::size_t from, double hz, double rate) {
    double ss = 0.0, cc = 0.0, sx = 0.0, cx = 0.0, sc = 0.0;
    for (std::size_t i = from; i < x.size(); ++i) {
        const double s = std::sin(2.0 * kPi * hz * i / rate), c = std::cos(2.0 * kPi * hz * i / rate);
        ss += s * s; cc += c * c; sc += s * c; sx += s * x[i]; cx += c * x[i];
    }
    const double det = ss * cc - sc * sc;
    const double a = (sx * cc - cx * sc) / det, b = (cx * ss - sx * sc) / det;
    return std::sqrt(a * a + b * b);
}

std::vector<float> tones(const std::vector<double>& hz, double rate, std::size_t frames) {
    std::vector<float> x(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        double v = 0.0;
        for (double f : hz) v += 0.2 * std::sin(2.0 * kPi * f * i / rate + f * 1e-3);
        x[i] = static_cast<float>(v);
    }
    return x;
}

// Down, then up, in blocks of `block` low-rate frames. One channel.
std::vector<float> roundTrip(const std::vector<float>& in, double rate, std::size_t factor, std::size_t block) {
    IntegerDecimator down;
    IntegerInterpolator up;
    down.prepare(rate, factor, 1);
    up.prepare(rate, factor, 1);
    std::vector<float> low(block), out(in.size(), 0.0f);
    for (std::size_t at = 0; at + block * factor <= in.size(); at += block * factor) {
        const float* src = in.data() + at;
        float* lowPtr = low.data();
        float* dst = out.data() + at;
        down.process(&src, &lowPtr, block);
        const float* lowIn = low.data();
        up.process(&lowIn, &dst, block);
    }
    return out;
}

} // namespace

static void test_the_filter_is_flat_below_24k_and_quiet_where_it_would_fold() {
    for (const Case& c : kCases) {
        float taps[integer_resampler::kMaxTaps];
        const std::size_t length = integer_resampler::designLowPass(c.rate, c.factor, taps);
        double worstPass = 0.0, worstStop = -400.0;
        for (double hz = 0.0; hz <= 20000.0; hz += 250.0) worstPass = std::max(worstPass, std::fabs(responseDb(taps, length, hz, c.rate)));
        const double stop = c.rate / c.factor - integer_resampler::kPassHz;
        for (double hz = stop; hz <= 0.5 * c.rate; hz += 100.0) worstStop = std::max(worstStop, responseDb(taps, length, hz, c.rate));
        std::printf("  %6.0f Hz / %zu: %zu taps (delay %zu), pass ripple %.5f dB to 20 kHz, stop %.1f dB from %.0f Hz\n",
                    c.rate, c.factor, length, (length - 1) / 2, worstPass, worstStop, stop);
        char msg[120];
        std::snprintf(msg, sizeof msg, "%.0f Hz / %zu: odd length that fits", c.rate, c.factor);
        CHECK(length % 2 == 1 && length < integer_resampler::kMaxTaps, msg);
        std::snprintf(msg, sizeof msg, "%.0f Hz / %zu: flat to 20 kHz within 0.001 dB", c.rate, c.factor);
        CHECK(worstPass < 0.001, msg);
        std::snprintf(msg, sizeof msg, "%.0f Hz / %zu: at least 95 dB down wherever it would fold into 0-24 kHz", c.rate, c.factor);
        CHECK(worstStop < -95.0, msg);
    }
    float taps[integer_resampler::kMaxTaps];
    CHECK(integer_resampler::designLowPass(192000.0, 1, taps) == 1 && taps[0] == 1.0f, "a factor of 1 is the identity");
}

// The audible band comes back unchanged and exactly delayFrames() late each way,
// whatever block size the renderer asks for.
static void test_the_round_trip_is_a_pure_delay_in_the_audible_band() {
    for (const Case& c : kCases) {
        const std::size_t frames = static_cast<std::size_t>(c.rate * 0.25) / (64 * c.factor) * (64 * c.factor);
        const std::vector<float> in = tones({100.0, 1000.0, 7000.0, 15000.0, 20000.0}, c.rate, frames);
        IntegerDecimator d;
        IntegerInterpolator u;
        d.prepare(c.rate, c.factor, 1);
        u.prepare(c.rate, c.factor, 1);
        const std::size_t delay = d.delayFrames() + u.delayFrames();
        const std::vector<float> out = roundTrip(in, c.rate, c.factor, 64);
        double residual = 0.0, energy = 0.0;
        for (std::size_t i = frames / 4; i < frames; ++i) {
            const double e = out[i] - in[i - delay];
            residual += e * e;
            energy += in[i - delay] * in[i - delay];
        }
        const double residualDb = 10.0 * std::log10(residual / energy);
        bool sameForAnyBlock = true;
        for (std::size_t block : {std::size_t{1}, std::size_t{7}, std::size_t{16}}) {
            const std::vector<float> other = roundTrip(in, c.rate, c.factor, block);
            for (std::size_t i = 0; i + 64 * c.factor < frames; ++i) sameForAnyBlock = sameForAnyBlock && other[i] == out[i];
        }
        std::printf("  %6.0f Hz / %zu: round trip against the input %zu frames earlier: residual %.1f dB\n", c.rate, c.factor, delay, residualDb);
        char msg[140];
        std::snprintf(msg, sizeof msg, "%.0f Hz / %zu: 100 Hz-20 kHz returns as the input delayed by the reported %zu frames", c.rate, c.factor, delay);
        CHECK(residualDb < -80.0, msg);
        std::snprintf(msg, sizeof msg, "%.0f Hz / %zu: block size does not change a single sample", c.rate, c.factor);
        CHECK(sameForAnyBlock, msg);
    }
}

// Down: a tone that would fold onto 12 kHz is gone. Up: the images of a 12 kHz
// tone are gone.
static void test_nothing_folds_into_or_images_out_of_the_audible_band() {
    for (const Case& c : kCases) {
        const double low = c.rate / c.factor;
        const std::size_t lowFrames = static_cast<std::size_t>(low * 0.2);
        // Down.
        std::vector<float> high = tones({low - 12000.0}, c.rate, lowFrames * c.factor);
        std::vector<float> decimated(lowFrames);
        IntegerDecimator d;
        d.prepare(c.rate, c.factor, 1);
        const float* src = high.data();
        float* dst = decimated.data();
        d.process(&src, &dst, lowFrames);
        const double folded = 20.0 * std::log10(amplitude(decimated, lowFrames / 4, 12000.0, low) / 0.2);
        // Up.
        const std::vector<float> lowTone = tones({12000.0}, low, lowFrames);
        std::vector<float> up(lowFrames * c.factor);
        IntegerInterpolator u;
        u.prepare(c.rate, c.factor, 1);
        const float* lowIn = lowTone.data();
        float* upOut = up.data();
        u.process(&lowIn, &upOut, lowFrames);
        double image = -400.0;
        for (std::size_t k = 1; k < c.factor; ++k)
            for (double hz : {k * low - 12000.0, k * low + 12000.0})
                if (hz < 0.5 * c.rate) image = std::max(image, 20.0 * std::log10(amplitude(up, up.size() / 4, hz, c.rate) / 0.2));
        const double kept = 20.0 * std::log10(amplitude(up, up.size() / 4, 12000.0, c.rate) / 0.2);
        std::printf("  %6.0f Hz / %zu: %.0f Hz folds to 12 kHz at %.1f dB; 12 kHz up: kept %+.4f dB, worst image %.1f dB\n",
                    c.rate, c.factor, low - 12000.0, folded, kept, image);
        char msg[120];
        std::snprintf(msg, sizeof msg, "%.0f Hz / %zu: no aliasing into the audible band", c.rate, c.factor);
        CHECK(folded < -95.0, msg);
        std::snprintf(msg, sizeof msg, "%.0f Hz / %zu: no images of the audible band, and the tone itself is kept", c.rate, c.factor);
        CHECK(image < -95.0 && std::fabs(kept) < 0.001, msg);
    }
}

static void test_many_channels_stay_independent_and_nothing_allocates() {
    const Case c = kCases[5];
    const std::size_t frames = 64 * 40;
    std::vector<std::vector<float>> in(integer_resampler::kMaxChannels), low(integer_resampler::kMaxChannels, std::vector<float>(64)),
                                    out(integer_resampler::kMaxChannels, std::vector<float>(frames));
    for (std::size_t ch = 0; ch < in.size(); ++ch) in[ch] = tones({500.0 * (ch + 1)}, c.rate, frames * c.factor);
    IntegerDecimator d;
    IntegerInterpolator u;
    d.prepare(c.rate, c.factor, integer_resampler::kMaxChannels);
    u.prepare(c.rate, c.factor, integer_resampler::kMaxChannels);
    const float* src[integer_resampler::kMaxChannels];
    float* lowOut[integer_resampler::kMaxChannels];
    const float* lowIn[integer_resampler::kMaxChannels];
    float* dst[integer_resampler::kMaxChannels];
    std::vector<std::vector<float>> high(integer_resampler::kMaxChannels, std::vector<float>(frames * c.factor));
    g_allocated = 0;
    g_recording = true;
    for (std::size_t block = 0; block < 40; ++block) {
        for (std::size_t ch = 0; ch < integer_resampler::kMaxChannels; ++ch) {
            src[ch] = in[ch].data() + block * 64 * c.factor;
            lowOut[ch] = low[ch].data();
            lowIn[ch] = low[ch].data();
            dst[ch] = high[ch].data() + block * 64 * c.factor;
        }
        d.process(src, lowOut, 64);
        u.process(lowIn, dst, 64);
    }
    g_recording = false;
    CHECK(g_allocated == 0, "decimating and interpolating allocate nothing");
    const std::vector<float> single = roundTrip(in[4], c.rate, c.factor, 64);
    bool same = true;
    for (std::size_t i = 0; i < single.size(); ++i) same = same && single[i] == high[4][i];
    CHECK(same, "channel 5 of seven comes out exactly as it does on its own");
}

int main() {
    test_the_filter_is_flat_below_24k_and_quiet_where_it_would_fold();
    test_the_round_trip_is_a_pure_delay_in_the_audible_band();
    test_nothing_folds_into_or_images_out_of_the_audible_band();
    test_many_channels_stay_independent_and_nothing_allocates();
    if (g_failures == 0) std::printf("test_integer_resampler: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
