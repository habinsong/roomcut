// SideCanceller: the middle passes untouched (a centred voice and a single
// speaker are not affected), the difference is lifted in the cancellation band,
// the recursion stays stable, and it allocates nothing.
#include "SideCanceller.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

using namespace roomcut;

static int g_failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", std::string(msg).c_str(), __FILE__, __LINE__); ++g_failures; } } while (0)

static bool g_counting = false;
static std::size_t g_allocated = 0;
void* operator new(std::size_t bytes) {
    if (g_counting) g_allocated += bytes;
    if (void* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static double sideGainDb(double fs, double hz, double gain) {
    SideCanceller c;
    c.prepare(fs);
    c.setGain(gain);
    const std::size_t n = static_cast<std::size_t>(fs);
    double in = 0.0, out = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = 0.25 * std::sin(2.0 * M_PI * hz * static_cast<double>(i) / fs);
        double l = s, r = -s;
        c.process(l, r);
        if (i > n / 2) { in += s * s; out += 0.25 * (l - r) * (l - r); }
    }
    return 10.0 * std::log10(out / in);
}

int main() {
    for (double fs : {44100.0, 48000.0, 192000.0, 768000.0}) {
        SideCanceller c;
        c.prepare(fs);
        c.setGain(0.45);
        uint32_t state = 5;
        bool middleUntouched = true, finite = true;
        g_counting = true;
        for (std::size_t i = 0; i < static_cast<std::size_t>(fs * 2); ++i) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const double mid = 0.3 * ((state & 0xffff) / 65535.0 - 0.5);
            const double side = 0.2 * (((state >> 16) & 0xffff) / 65535.0 - 0.5);
            double l = mid + side, r = mid - side;
            c.process(l, r);
            middleUntouched = middleUntouched && std::fabs(0.5 * (l + r) - mid) < 1e-12;
            finite = finite && std::isfinite(l) && std::isfinite(r) && std::fabs(l) < 10.0;
        }
        g_counting = false;
        char msg[96];
        std::snprintf(msg, sizeof msg, "fs %.0f: the sum of the outputs is the programme's middle, sample for sample", fs);
        CHECK(middleUntouched, msg);
        std::snprintf(msg, sizeof msg, "fs %.0f: the recursion stays bounded on noise", fs);
        CHECK(finite, msg);
    }
    CHECK(g_allocated == 0, "processing allocates nothing");

    const double at1k = sideGainDb(48000.0, 1000.0, 0.45), at80 = sideGainDb(48000.0, 80.0, 0.45), off = sideGainDb(48000.0, 1000.0, 0.0);
    std::printf("difference-signal gain at 0.45: %+.2f dB at 1 kHz, %+.2f dB at 80 Hz; at 0: %+.4f dB\n", at1k, at80, off);
    CHECK(at1k > 2.0, "the difference is lifted in the cancellation band");
    CHECK(std::fabs(at80) < 0.5, "and left alone below it, where there is little crosstalk to cancel");
    CHECK(std::fabs(off) < 1e-6, "gain 0 is a straight wire");
    if (g_failures == 0) std::printf("test_side_canceller: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
