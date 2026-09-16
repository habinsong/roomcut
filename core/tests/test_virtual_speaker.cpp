// VirtualSpeaker: a loudspeaker placed at an azimuth, rendered for headphones.
// The checks measure the two cues the model claims to produce — the far ear
// hears the sound later (ITD) and duller (head shadow) — against the published
// formulas, plus the usual render-path safety: symmetry at centre, no clicks
// while the angle moves, no allocation, identical behaviour at every rate.
#include "VirtualSpeaker.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

struct Pair { std::vector<double> left, right; };

// Render an impulse (after letting the angle settle) and return both ears.
Pair impulse(double azimuth, double fs, double radiusCm = VirtualSpeaker::kDefaultHeadRadiusCm,
             std::size_t frames = 512) {
    VirtualSpeaker speaker;
    speaker.prepare(fs);
    speaker.setHeadRadiusCm(radiusCm);
    speaker.setAzimuth(azimuth);
    speaker.reset();                       // settle the angle before measuring
    Pair out;
    out.left.resize(frames);
    out.right.resize(frames);
    for (std::size_t i = 0; i < frames; ++i)
        speaker.process(i == 0 ? 1.0 : 0.0, out.left[i], out.right[i]);
    return out;
}

// Sub-sample arrival difference (right minus left) from the cross-correlation
// peak, refined by a parabolic fit. A source on the right reaches the right ear
// FIRST, so a right-hand angle reads negative here.
double interauralDelaySeconds(const Pair& p, double fs) {
    // Search far enough for the longest ITD the model can produce at this rate.
    // A fixed 128 was enough up to 192 kHz and quietly saturated above it, which
    // made a correct renderer look broken at 384 and 768 kHz.
    const int maxLag = std::min(static_cast<int>(p.left.size()) / 2,
                                static_cast<int>(std::ceil(VirtualSpeaker::kMaxItdSeconds * fs)) + 8);
    double best = -1e300;
    int bestLag = 0;
    std::vector<double> score(static_cast<std::size_t>(2 * maxLag + 1), 0.0);
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i < p.left.size(); ++i) {
            const long j = static_cast<long>(i) + lag;
            if (j < 0 || j >= static_cast<long>(p.right.size())) continue;
            sum += p.left[i] * p.right[static_cast<std::size_t>(j)];
        }
        score[static_cast<std::size_t>(lag + maxLag)] = sum;
        if (sum > best) { best = sum; bestLag = lag; }
    }
    double refined = bestLag;
    const std::size_t c = static_cast<std::size_t>(bestLag + maxLag);
    if (c > 0 && c + 1 < score.size()) {
        const double a = score[c - 1], b = score[c], d = score[c + 1];
        const double denom = a - 2.0 * b + d;
        if (std::fabs(denom) > 1e-18) refined = bestLag + 0.5 * (a - d) / denom;
    }
    return refined / fs;   // right ear later by this much
}

double energy(const std::vector<double>& x) {
    double e = 0.0;
    for (double v : x) e += v * v;
    return e;
}

// Energy of a one-pole-filtered copy: a crude but sufficient band split for
// "does the far ear lose its top end".
double highBandEnergy(const std::vector<double>& x, double fs, double cutoffHz) {
    const double a = 1.0 - std::exp(-2.0 * M_PI * cutoffHz / fs);
    double lp = 0.0, e = 0.0;
    for (double v : x) { lp += a * (v - lp); const double hp = v - lp; e += hp * hp; }
    return e;
}

} // namespace

static void test_centre_is_symmetric() {
    const Pair p = impulse(0.0, 48000.0);
    double worst = 0.0;
    for (std::size_t i = 0; i < p.left.size(); ++i)
        worst = std::max(worst, std::fabs(p.left[i] - p.right[i]));
    CHECK(worst < 1e-12, "a speaker straight ahead reaches both ears identically");
}

static void test_itd_follows_woodworth() {
    const double fs = 96000.0;             // finer time resolution for the check
    const double radius = VirtualSpeaker::kDefaultHeadRadiusCm * 0.01;
    for (double az : {15.0, 30.0, 60.0, 90.0, 135.0}) {
        const Pair right = impulse(az, fs);
        const double measured = interauralDelaySeconds(right, fs);
        const double expected = -VirtualSpeaker::woodworthItd(az, radius);   // right ear first
        std::printf("  az %+6.1f deg: ITD %+7.1f us (model %+7.1f us)\n",
                    az, measured * 1e6, expected * 1e6);
        CHECK(std::fabs(measured - expected) < 20e-6, "rendered ITD matches the Woodworth model");
        CHECK(measured < 0.0, "a source on the right reaches the right ear first");

        // Mirroring the angle mirrors the ears, and nothing else.
        const Pair left = impulse(-az, fs);
        const double mirrored = interauralDelaySeconds(left, fs);
        CHECK(std::fabs(mirrored + measured) < 5e-6, "left and right angles are mirror images");
    }
}

static void test_head_radius_scales_the_itd() {
    const double fs = 96000.0;
    const double small = std::fabs(interauralDelaySeconds(impulse(45.0, fs, 7.5), fs));
    const double large = std::fabs(interauralDelaySeconds(impulse(45.0, fs, 10.0), fs));
    std::printf("  ITD at 45 deg: 7.5 cm head %.1f us, 10.0 cm head %.1f us\n",
                small * 1e6, large * 1e6);
    CHECK(large > small, "a larger head produces a longer ITD");
    CHECK(std::fabs(large / small - 10.0 / 7.5) < 0.05, "ITD scales with the head radius");
}

static void test_far_ear_is_shadowed() {
    const double fs = 48000.0;
    double previousIld = -1e9;
    for (double az : {15.0, 45.0, 90.0}) {
        const Pair p = impulse(az, fs);
        const double ildDb = 10.0 * std::log10(energy(p.right) / std::max(energy(p.left), 1e-300));
        // The shadow is a low-pass: the far ear should lose more high band than
        // broadband energy.
        const double nearHigh = highBandEnergy(p.right, fs, 3000.0);
        const double farHigh = highBandEnergy(p.left, fs, 3000.0);
        const double highIldDb = 10.0 * std::log10(nearHigh / std::max(farHigh, 1e-300));
        std::printf("  az %5.1f deg: ILD %+5.2f dB, above 3 kHz %+5.2f dB\n", az, ildDb, highIldDb);
        CHECK(ildDb > 0.0, "the near ear is louder than the far ear");
        CHECK(ildDb > previousIld, "the level difference grows with the angle");
        CHECK(highIldDb > ildDb, "the far ear loses its top end first — that is head shadow");
        previousIld = ildDb;
    }
}

static void test_moving_the_angle_is_click_free() {
    const double fs = 48000.0;
    VirtualSpeaker speaker;
    speaker.prepare(fs);
    speaker.setAzimuth(0.0);
    speaker.reset();

    const double freq = 400.0, amp = 0.5;
    const double drySlope = 2.0 * M_PI * freq / fs * amp;
    double phase = 0.0, prevL = 0.0, prevR = 0.0, maxStep = 0.0, peak = 0.0;
    const std::size_t n = static_cast<std::size_t>(fs * 4.0);
    for (std::size_t i = 0; i < n; ++i) {
        // A hard jump (what a lost-and-recovered head tracker does) plus a sweep.
        if (i == static_cast<std::size_t>(fs * 0.5)) speaker.setAzimuth(75.0);
        if (i == static_cast<std::size_t>(fs * 1.5)) speaker.setAzimuth(-75.0);
        if (i > static_cast<std::size_t>(fs * 2.0))
            speaker.setAzimuth(60.0 * std::sin((double)i / fs * 2.0));
        const double s = amp * std::sin(phase);
        phase += 2.0 * M_PI * freq / fs;
        double l = 0.0, r = 0.0;
        speaker.process(s, l, r);
        if (i > 0) {
            maxStep = std::max(maxStep, std::fabs(l - prevL));
            maxStep = std::max(maxStep, std::fabs(r - prevR));
        }
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        prevL = l; prevR = r;
        CHECK(std::isfinite(l) && std::isfinite(r), "the renderer stays finite while the angle moves");
        if (g_failures > 5) return;
    }
    std::printf("  max sample step %.5f (dry sine slope %.5f), peak %.3f\n", maxStep, drySlope, peak);
    CHECK(maxStep < drySlope * 3.0, "a jumped or swept angle never steps the output");
    CHECK(peak < 1.2, "the shadow filter's lift stays bounded");
}

// Pink-ish noise through a fixed angle; returns the summed energy of both ears.
// Music has far less energy up top than an impulse does, and the head shadow
// acts up top, so loudness has to be judged on material like this.
static double loudnessAt(double azimuth, double fs) {
    VirtualSpeaker speaker;
    speaker.prepare(fs);
    speaker.setAzimuth(azimuth);
    speaker.reset();
    unsigned state = 22222;
    double b0 = 0, b1 = 0, b2 = 0, total = 0.0;
    const std::size_t n = static_cast<std::size_t>(fs * 2.0);
    for (std::size_t i = 0; i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        const double w = (double)(state >> 8) / 8388608.0 - 1.0;
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        double l = 0.0, r = 0.0;
        speaker.process((b0 + b1 + b2 + w * 0.1848) * 0.02, l, r);
        if (i > n / 10) total += l * l + r * r;
    }
    return total;
}

static void test_level_stays_steady_across_angles() {
    // Turning your head should move the image, not the volume.
    const double fs = 48000.0;
    const double front = loudnessAt(0.0, fs);
    double nearFrontSpread = 0.0, circleSpread = 0.0;
    for (double az = -180.0; az <= 180.0; az += 15.0) {
        const double db = 10.0 * std::log10(loudnessAt(az, fs) / front);
        circleSpread = std::max(circleSpread, std::fabs(db));
        if (std::fabs(az) <= 45.0) nearFrontSpread = std::max(nearFrontSpread, std::fabs(db));
    }
    std::printf("  loudness vs front: within +-45 deg %.2f dB, full circle %.2f dB\n",
                nearFrontSpread, circleSpread);
    // Head tracking only swings the image a little either side of the screen —
    // that range is where a level change would be noticed as a volume wobble.
    CHECK(nearFrontSpread < 2.0, "rotating around the front does not change loudness");
    CHECK(circleSpread < 5.0, "even a full circle stays within a reasonable level window");
}

static void test_every_sample_rate() {
    // Every rate the engine will accept, not just the common ones: the format
    // validator goes to 768 kHz, and the delay line used to run out above
    // 384 kHz, which shortened the ITD without anything reporting a problem.
    const double radius = VirtualSpeaker::kDefaultHeadRadiusCm * 0.01;
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0, 384000.0, 768000.0}) {
        // Long enough that the correlation still has signal on both sides of
        // the lag it has to find.
        const auto frames = static_cast<std::size_t>(std::max(1024.0, fs * 0.006));
        const Pair p = impulse(60.0, fs, VirtualSpeaker::kDefaultHeadRadiusCm, frames);
        const double measured = std::fabs(interauralDelaySeconds(p, fs));
        const double expected = VirtualSpeaker::woodworthItd(60.0, radius);
        std::printf("  %6.0f Hz: ITD %.1f us (model %.1f us)\n", fs, measured * 1e6, expected * 1e6);
        CHECK(std::fabs(measured - expected) < 25e-6, "ITD is sample-rate independent");
        bool finite = true;
        for (double v : p.left) finite = finite && std::isfinite(v);
        CHECK(finite, "output stays finite at every sample rate");
    }

    // The worst case the model can be asked for: the widest head at the angle
    // where the path round it is longest, at the highest rate.
    const double widest = VirtualSpeaker::kMaxHeadRadiusCm * 0.01;
    const double expected = VirtualSpeaker::woodworthItd(90.0, widest);
    for (double fs : {384000.0, 768000.0}) {
        const Pair p = impulse(90.0, fs, VirtualSpeaker::kMaxHeadRadiusCm,
                               static_cast<std::size_t>(fs * 0.006));
        const double measured = std::fabs(interauralDelaySeconds(p, fs));
        CHECK(std::fabs(measured - expected) < 25e-6,
              "the delay line holds the longest ITD the model can produce");
    }
}

static void test_render_path_does_not_allocate() {
    VirtualSpeaker speaker;
    speaker.prepare(48000.0);
    speaker.setAzimuth(30.0);
    speaker.reset();
    g_allocated = 0;
    g_recording = true;
    for (std::size_t i = 0; i < 48000; ++i) {
        if (i % 1000 == 0) speaker.setAzimuth(i % 2000 == 0 ? 40.0 : -40.0);
        if (i % 7000 == 0) speaker.setHeadRadiusCm(i % 14000 == 0 ? 9.5 : 8.0);
        double l = 0.0, r = 0.0;
        speaker.process(0.1, l, r);
    }
    g_recording = false;
    CHECK(g_allocated == 0, "process(), setAzimuth() and setHeadRadiusCm() allocate nothing");
}

// Azimuth is a circle. A speaker at the rear centre line that moves one degree
// must move one degree, not three hundred and fifty nine the other way. This is
// what a 7.1 back speaker does every time the listener turns past 45 degrees,
// and smoothing the raw difference sent it sweeping through the front — heard
// as a dropout and a smear instead of a head turn.
static void test_crossing_the_rear_centre_line_takes_the_short_way() {
    VirtualSpeaker speaker;
    speaker.prepare(48000.0);
    speaker.setAzimuth(-180.0);
    speaker.reset();
    speaker.setAzimuth(179.0);

    double worstExcursion = 0.0, l = 0.0, r = 0.0;
    for (int i = 0; i < 4800; ++i) {          // 100 ms, far longer than the glide
        speaker.process(0.0, l, r);
        double from = speaker.azimuth() - 180.0;      // distance from the rear line
        if (from > 180.0) from -= 360.0;
        else if (from < -180.0) from += 360.0;
        worstExcursion = std::max(worstExcursion, std::fabs(from));
    }
    std::printf("  crossing the rear line: worst excursion %.2f deg\n", worstExcursion);
    CHECK(worstExcursion < 5.0, "a speaker at the back never swings through the front");

    // And the ordinary case still glides rather than jumping.
    VirtualSpeaker front;
    front.prepare(48000.0);
    front.setAzimuth(-30.0);
    front.reset();
    front.setAzimuth(30.0);
    front.process(0.0, l, r);
    CHECK(std::fabs(front.azimuth() + 30.0) < 5.0, "a normal move still eases in");
}

int main() {
    test_centre_is_symmetric();
    test_itd_follows_woodworth();
    test_head_radius_scales_the_itd();
    test_far_ear_is_shadowed();
    test_moving_the_angle_is_click_free();
    test_level_stays_steady_across_angles();
    test_every_sample_rate();
    test_crossing_the_rear_centre_line_takes_the_short_way();
    test_render_path_does_not_allocate();
    if (g_failures == 0) std::printf("test_virtual_speaker: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
