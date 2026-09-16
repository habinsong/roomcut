// Binaural direction harness: where each upmix channel is heard on headphones,
// measured from the two ear signals the renderer produces.
//
// Every channel is driven on its own through SurroundStage::renderBed() and the
// ear pair is read for the cues a listener has: the interaural time difference
// (cross-correlation), the interaural level difference (per third octave) and
// the lateral angle those imply for a sphere of the renderer's head radius.
//
// The estimator is checked first against signals whose answer is known, then
// the current renderer is measured. ITD and ILD only fix the cone of confusion,
// so front and back are NOT told apart here; a lateral angle is compared with
// the lateral angle of the channel's true azimuth.
//
// What this records about the renderer as it stands (P0 M0 baseline):
//   - C, L and R land on their lateral angles and follow the head.
//   - The surround channels are not a single-source direction at all: the
//     diffuse bus gives the two ears opposite polarity. Nor do they move with
//     the head. Those two checks are the ones a directional renderer inverts.
#include "SurroundStage.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSpeedOfSound = 343.0;                 // VirtualSpeaker's value
const double kHeadRadius = VirtualSpeaker::kDefaultHeadRadiusCm * 0.01;
constexpr double kRates[] = {44100.0, 48000.0, 96000.0, 192000.0, 384000.0, 768000.0};

struct Ears { std::vector<double> left, right; };

double wrap180(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180.0) degrees -= 360.0;
    if (degrees < -180.0) degrees += 360.0;
    return degrees;
}

// Angle from the median plane, 0..90: the part of an azimuth ITD and ILD can
// see. 30 and 150 degrees are the same lateral angle.
double lateralOf(double azimuthDegrees) {
    const double a = std::fabs(wrap180(azimuthDegrees));
    return a <= 90.0 ? a : 180.0 - a;
}

// Woodworth & Schlosberg for the frontal quadrant, lateral angle in degrees.
double woodworthItd(double lateralDegrees) {
    const double theta = lateralDegrees * kPi / 180.0;
    return kHeadRadius / kSpeedOfSound * (theta + std::sin(theta));
}

// The inverse, by bisection (the formula rises monotonically on 0..90).
double lateralFromItd(double itdSeconds) {
    const double itd = std::fabs(itdSeconds);
    if (itd >= woodworthItd(90.0)) return 90.0;
    double lo = 0.0, hi = 90.0;
    for (int i = 0; i < 100; ++i) {
        const double mid = 0.5 * (lo + hi);
        (woodworthItd(mid) < itd ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

std::vector<double> powerSpectrum(const std::vector<double>& x, std::size_t n) {
    std::vector<std::complex<double>> a(n);
    for (std::size_t i = 0; i < x.size() && i < n; ++i) a[i] = x[i];
    fft(a);
    std::vector<double> p(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) p[k] = std::norm(a[k]);
    return p;
}

struct Direction {
    double itdSeconds = 0.0;   // > 0: the right ear hears it later (source on the left)
    double peak = 0.0;         // largest normalised interaural correlation in the window
    double trough = 0.0;       // most negative one
    double lateralDegrees = 0.0;
    double side = 0.0;         // -1 left, +1 right, 0 centre
    double ildDb = 0.0;        // broadband, left over right
    double arrivalLeftMs = -1.0, arrivalRightMs = -1.0;
    // One source through one path reaches both ears with the same polarity,
    // no further apart than the head allows. Opposite polarity dominating, or a
    // lag beyond the head, is not a direction a single source can have.
    bool singleSource = false;
};

double arrivalMs(const std::vector<double>& x, double fs) {
    double top = 0.0;
    for (double v : x) top = std::max(top, std::fabs(v));
    if (top <= 0.0) return -1.0;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::fabs(x[i]) >= top * 1.0e-3) return 1000.0 * static_cast<double>(i) / fs;
    return -1.0;
}

Direction measure(const Ears& e, double fs) {
    Direction d;
    const std::size_t n = e.left.size();
    double ll = 0.0, rr = 0.0;
    for (std::size_t i = 0; i < n; ++i) { ll += e.left[i] * e.left[i]; rr += e.right[i] * e.right[i]; }
    d.ildDb = 10.0 * std::log10(std::max(ll, 1e-300) / std::max(rr, 1e-300));
    d.arrivalLeftMs = arrivalMs(e.left, fs);
    d.arrivalRightMs = arrivalMs(e.right, fs);
    const double norm = std::sqrt(ll * rr);
    if (norm <= 0.0) return d;

    // Search as far as the renderer's largest head can delay an ear, scaled
    // with the rate (a fixed sample count saturates at high rates).
    const long window = static_cast<long>(std::ceil(VirtualSpeaker::kMaxItdSeconds * fs));
    std::vector<double> r(static_cast<std::size_t>(2 * window + 1));
    long best = 0;
    double peak = -2.0, trough = 2.0;
    for (long lag = -window; lag <= window; ++lag) {
        double sum = 0.0;
        const long from = std::max(0L, -lag), to = std::min(static_cast<long>(n), static_cast<long>(n) - lag);
        for (long i = from; i < to; ++i) sum += e.left[static_cast<std::size_t>(i)] * e.right[static_cast<std::size_t>(i + lag)];
        const double v = sum / norm;
        r[static_cast<std::size_t>(lag + window)] = v;
        if (v > peak) { peak = v; best = lag; }
        trough = std::min(trough, v);
    }
    // Sub-sample peak. A parabola through three points is biased by up to a
    // tenth of a sample on broadband material (measured 2.7 us at 44.1 kHz).
    // The correlation of band-limited signals is itself band-limited, so it is
    // interpolated with a windowed sinc and its maximum searched on that.
    auto interpolated = [&](double t) {
        constexpr long half = 32;
        double sum = 0.0;
        for (long k = -half; k <= half; ++k) {
            const long index = best + k + window;
            if (index < 0 || index >= static_cast<long>(r.size())) continue;
            const double x = t - static_cast<double>(k);
            if (std::fabs(x) >= static_cast<double>(half)) continue;
            const double sinc = x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double u = (x + half) / (2.0 * half);
            sum += r[static_cast<std::size_t>(index)] * sinc
                 * (0.42 - 0.5 * std::cos(2.0 * kPi * u) + 0.08 * std::cos(4.0 * kPi * u));
        }
        return sum;
    };
    double lo = -1.0, hi = 1.0;
    constexpr double golden = 0.6180339887498949;
    double x1 = hi - golden * (hi - lo), x2 = lo + golden * (hi - lo);
    double f1 = interpolated(x1), f2 = interpolated(x2);
    for (int i = 0; i < 60; ++i) {
        if (f1 < f2) { lo = x1; x1 = x2; f1 = f2; x2 = lo + golden * (hi - lo); f2 = interpolated(x2); }
        else         { hi = x2; x2 = x1; f2 = f1; x1 = hi - golden * (hi - lo); f1 = interpolated(x1); }
    }
    const double fraction = 0.5 * (lo + hi);
    d.peak = peak;
    d.trough = trough;
    d.itdSeconds = (static_cast<double>(best) + fraction) / fs;
    d.lateralDegrees = lateralFromItd(d.itdSeconds);
    d.side = d.itdSeconds > 0.0 ? -1.0 : (d.itdSeconds < 0.0 ? 1.0 : 0.0);
    d.singleSource = peak > 0.0 && peak >= -trough
        && std::fabs(d.itdSeconds) <= woodworthItd(90.0) + 1.0 / fs;
    return d;
}

// Third-octave ILDs, left over right, for bands wholly below 0.45 fs. The
// transform is padded until the narrowest band (250 Hz, 58 Hz wide) holds at
// least four bins; a short response otherwise leaves bands with none at all.
std::vector<std::pair<double, double>> bandIlds(const Ears& e, double fs) {
    const double narrowest = 250.0 * (std::pow(2.0, 1.0 / 6.0) - std::pow(2.0, -1.0 / 6.0));
    std::size_t n = 1;
    while (n < e.left.size() || fs / static_cast<double>(n) > narrowest / 4.0) n <<= 1;
    const std::vector<double> pl = powerSpectrum(e.left, n), pr = powerSpectrum(e.right, n);
    static constexpr double kCentres[] = {250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                          2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000};
    std::vector<std::pair<double, double>> out;
    for (double fc : kCentres) {
        const double lo = fc * std::pow(2.0, -1.0 / 6.0), hi = fc * std::pow(2.0, 1.0 / 6.0);
        if (hi > 0.45 * fs) break;
        double el = 0.0, er = 0.0;
        std::size_t bins = 0;
        for (std::size_t k = 1; k < n / 2; ++k) {
            const double f = static_cast<double>(k) * fs / static_cast<double>(n);
            if (f >= lo && f < hi) { el += pl[k]; er += pr[k]; ++bins; }
        }
        CHECK(bins >= 4, "every third-octave band holds at least four bins");
        out.emplace_back(fc, 10.0 * std::log10(std::max(el, 1e-300) / std::max(er, 1e-300)));
    }
    return out;
}

// A Blackman-windowed sinc pulse at a fractional position: band-limited to
// Nyquist, so it is the hardest case for a sub-sample delay estimate.
std::vector<double> pulse(std::size_t length, double at, double gain) {
    std::vector<double> x(length, 0.0);
    constexpr double half = 64.0;
    for (std::size_t i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) - at;
        if (std::fabs(t) >= half) continue;
        const double sinc = t == 0.0 ? 1.0 : std::sin(kPi * t) / (kPi * t);
        const double u = (t + half) / (2.0 * half);
        const double window = 0.42 - 0.5 * std::cos(2.0 * kPi * u) + 0.08 * std::cos(4.0 * kPi * u);
        x[i] = gain * sinc * window;
    }
    return x;
}

// ITU-R BS.775 angles in the order renderBed() takes the channels.
struct Channel { const char* name; double UpmixFrame::*field; double azimuth; bool surround; };
constexpr Channel k51[] = {{"C", &UpmixFrame::centre, 0.0, false},
                           {"L", &UpmixFrame::frontL, -30.0, false},
                           {"R", &UpmixFrame::frontR, 30.0, false},
                           {"Ls", &UpmixFrame::sideL, -110.0, true},
                           {"Rs", &UpmixFrame::sideR, 110.0, true}};
constexpr Channel k71[] = {{"C", &UpmixFrame::centre, 0.0, false},
                           {"L", &UpmixFrame::frontL, -30.0, false},
                           {"R", &UpmixFrame::frontR, 30.0, false},
                           {"Ls", &UpmixFrame::sideL, -90.0, true},
                           {"Rs", &UpmixFrame::sideR, 90.0, true},
                           {"Lb", &UpmixFrame::backL, -135.0, true},
                           {"Rb", &UpmixFrame::backR, 135.0, true}};

// One channel's impulse through the headphone render, long enough for the
// latest diffuse arrival (32 ms) to decay.
Ears channelImpulse(int layout, const Channel& channel, double yaw, double fs) {
    SurroundStage stage;
    stage.prepare(fs);
    stage.setLayout(layout);
    stage.setYawDegrees(yaw);
    stage.reset();                         // speakers and emphasis settled on the angle
    const std::size_t frames = static_cast<std::size_t>(std::ceil(0.064 * fs));
    Ears out;
    out.left.resize(frames);
    out.right.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        UpmixFrame up;
        up.*channel.field = i == 0 ? 1.0 : 0.0;
        stage.renderBed(up, out.left[i], out.right[i]);
    }
    return out;
}

} // namespace

// --- The estimator, against answers known in advance -------------------------

static void test_the_lateral_angle_inverts_woodworth() {
    double worst = 0.0;
    for (int degrees = 0; degrees <= 90; ++degrees)
        worst = std::max(worst, std::fabs(lateralFromItd(woodworthItd(degrees)) - degrees));
    std::printf("  lateral inverse: worst error %.2e deg\n", worst);
    CHECK(worst < 1.0e-9, "ITD -> lateral angle recovers every angle of the formula");
}

// The accuracy the harness needs follows from what it has to decide: 1 degree
// of lateral angle is at least 4.45 us of ITD (the formula's flattest slope,
// at 90 degrees, for this head). Two microseconds keeps estimator error under
// half a degree everywhere.
static void test_the_estimator_reads_a_known_delay() {
    const double delays[] = {-0.00065, -0.00047, -0.00012, -0.00003, 0.0, 0.00005, 0.00031, 0.0006};
    for (double fs : kRates) {
        double worstUs = 0.0;
        for (double delay : delays) {
            const std::size_t length = static_cast<std::size_t>(fs * 0.01);
            const double at = static_cast<double>(length) * 0.3 + 0.37;
            Ears e{pulse(length, at, 1.0), pulse(length, at + delay * fs, 0.5)};
            const Direction d = measure(e, fs);
            worstUs = std::max(worstUs, std::fabs(d.itdSeconds - delay) * 1.0e6);
            char msg[120];
            std::snprintf(msg, sizeof msg, "a same-polarity copy is a single source (%.0f Hz, %.2f ms)", fs, delay * 1e3);
            CHECK(d.singleSource, msg);
        }
        std::printf("  known delays at %6.0f Hz: worst error %.3f us\n", fs, worstUs);
        char msg[100];
        std::snprintf(msg, sizeof msg, "delay read to within 2 us at %.0f Hz", fs);
        CHECK(worstUs <= 2.0, msg);
    }
}

static void test_the_estimator_reads_a_known_level_difference() {
    for (double fs : kRates) {
        const std::size_t length = static_cast<std::size_t>(fs * 0.01);
        const double at = static_cast<double>(length) * 0.5;
        const double gain = std::pow(10.0, -6.0 / 20.0);
        Ears e{pulse(length, at, 1.0), pulse(length, at, gain)};
        double worst = std::fabs(measure(e, fs).ildDb - 6.0);
        for (const auto& band : bandIlds(e, fs)) worst = std::max(worst, std::fabs(band.second - 6.0));
        char msg[100];
        std::snprintf(msg, sizeof msg, "a 6 dB level difference reads 6 dB in every band at %.0f Hz", fs);
        CHECK(worst < 1.0e-6, msg);
    }
}

static void test_opposite_polarity_is_not_a_single_source() {
    const double fs = 48000.0;
    const std::size_t length = 480;
    std::vector<double> left = pulse(length, 200.0, 1.0), right = left;
    for (double& v : right) v = -0.4 * v;
    const Direction d = measure({left, right}, fs);
    std::printf("  opposite polarity: peak %.3f trough %.3f\n", d.peak, d.trough);
    CHECK(!d.singleSource, "two ears in opposite polarity are not one source's direction");
}

// --- The renderer as it stands ------------------------------------------------

static void test_front_channels_land_on_their_lateral_angles() {
    for (double fs : kRates) {
        for (int layout : {Upmixer::k51, Upmixer::k71}) {
            const auto* channels = layout == Upmixer::k71 ? k71 : k51;
            const std::size_t count = layout == Upmixer::k71 ? 7u : 5u;
            for (std::size_t i = 0; i < count; ++i) {
                const Channel& ch = channels[i];
                if (ch.surround) continue;
                const Direction d = measure(channelImpulse(layout, ch, 0.0, fs), fs);
                const double expected = lateralOf(ch.azimuth);
                const double expectedSide = ch.azimuth > 0.0 ? 1.0 : (ch.azimuth < 0.0 ? -1.0 : 0.0);
                if (fs == 48000.0)
                    std::printf("  %s %-2s az %+5.0f: ITD %+8.1f us, lateral %5.2f (want %5.2f), "
                                "corr %+.3f/%+.3f, ILD %+6.2f dB\n",
                                layout == Upmixer::k71 ? "7.1" : "5.1", ch.name, ch.azimuth,
                                d.itdSeconds * 1e6, d.lateralDegrees, expected, d.peak, d.trough, d.ildDb);
                char msg[140];
                std::snprintf(msg, sizeof msg, "%s at %.0f Hz is a single source within 10 deg of its lateral angle",
                              ch.name, fs);
                CHECK(d.singleSource && std::fabs(d.lateralDegrees - expected) <= 10.0
                      && (expected < 1.0 || d.side == expectedSide), msg);
            }
        }
    }
}

// Baseline, not a goal: this is what a directional renderer must change.
static void test_surround_channels_have_no_single_source_direction() {
    for (double fs : kRates) {
        for (int layout : {Upmixer::k51, Upmixer::k71}) {
            const auto* channels = layout == Upmixer::k71 ? k71 : k51;
            const std::size_t count = layout == Upmixer::k71 ? 7u : 5u;
            for (std::size_t i = 0; i < count; ++i) {
                const Channel& ch = channels[i];
                if (!ch.surround) continue;
                const Ears e = channelImpulse(layout, ch, 0.0, fs);
                const Direction d = measure(e, fs);
                if (fs == 48000.0) {
                    std::printf("  %s %-2s az %+5.0f: corr %+.3f/%+.3f, ILD %+6.2f dB, arrives L %.2f / R %.2f ms\n",
                                layout == Upmixer::k71 ? "7.1" : "5.1", ch.name, ch.azimuth,
                                d.peak, d.trough, d.ildDb, d.arrivalLeftMs, d.arrivalRightMs);
                    for (const auto& band : bandIlds(e, fs))
                        if (band.first == 500 || band.first == 2000 || band.first == 8000)
                            std::printf("      ILD @ %5.0f Hz %+6.2f dB\n", band.first, band.second);
                }
                char msg[120];
                std::snprintf(msg, sizeof msg, "%s %s at %.0f Hz reaches the ears in opposite polarity",
                              layout == Upmixer::k71 ? "7.1" : "5.1", ch.name, fs);
                CHECK(!d.singleSource && d.trough < -0.99, msg);
            }
        }
    }
}

// FR3 baseline: yaw +40 must move every channel to (azimuth - 40). The fronts
// do; the surrounds come out sample-for-sample the same as facing forward.
static void test_head_turn_moves_the_fronts_only() {
    const double yaw = 40.0;
    for (double fs : kRates) {
        for (int layout : {Upmixer::k51, Upmixer::k71}) {
            const auto* channels = layout == Upmixer::k71 ? k71 : k51;
            const std::size_t count = layout == Upmixer::k71 ? 7u : 5u;
            for (std::size_t i = 0; i < count; ++i) {
                const Channel& ch = channels[i];
                const Ears turned = channelImpulse(layout, ch, yaw, fs);
                char msg[140];
                if (ch.surround) {
                    const Ears ahead = channelImpulse(layout, ch, 0.0, fs);
                    std::snprintf(msg, sizeof msg, "%s at %.0f Hz does not follow the head", ch.name, fs);
                    CHECK(turned.left == ahead.left && turned.right == ahead.right, msg);
                    continue;
                }
                const Direction d = measure(turned, fs);
                const double target = wrap180(ch.azimuth - yaw);
                const double expected = lateralOf(target);
                const double expectedSide = target > 0.0 ? 1.0 : -1.0;
                if (fs == 48000.0)
                    std::printf("  yaw +40 %s %-2s -> az %+5.0f: lateral %5.2f (want %5.2f) side %+.0f\n",
                                layout == Upmixer::k71 ? "7.1" : "5.1", ch.name, target,
                                d.lateralDegrees, expected, d.side);
                std::snprintf(msg, sizeof msg, "%s at %.0f Hz follows the head to within 5 deg", ch.name, fs);
                CHECK(d.singleSource && std::fabs(d.lateralDegrees - expected) <= 5.0 && d.side == expectedSide, msg);
            }
        }
    }
}

// NFR4 metric: a head turn must not click. A narrowband sine makes a click
// stand out: its own sample-to-sample step is small, a discontinuity is not.
// A clean glide can only exceed the settled slope by the Doppler of the moving
// delay (the 60-degree step moves the ITD by about 0.43 ms over the ~12 ms
// azimuth glide, a few percent) — 1.25x leaves room for that and nothing else.
static void test_a_head_turn_does_not_click() {
    for (double fs : {48000.0, 192000.0}) {
        for (double hz : {200.0, 1000.0}) {
            SurroundStage stage;
            stage.prepare(fs);
            stage.setLayout(Upmixer::k71);
            stage.setYawDegrees(0.0);
            stage.reset();
            const std::size_t turnAt = static_cast<std::size_t>(fs * 0.2);
            const std::size_t settle = static_cast<std::size_t>(fs * 0.2);
            const std::size_t total = turnAt + settle * 2;
            double before = 0.0, during = 0.0, after = 0.0, previousL = 0.0, previousR = 0.0;
            for (std::size_t i = 0; i < total; ++i) {
                if (i == turnAt) stage.setYawDegrees(60.0);
                UpmixFrame up;
                up.centre = 0.5 * std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs);
                double l = 0.0, r = 0.0;
                stage.renderBed(up, l, r);
                if (i > 0) {
                    const double step = std::max(std::fabs(l - previousL), std::fabs(r - previousR));
                    if (i > turnAt / 2 && i < turnAt) before = std::max(before, step);
                    else if (i >= turnAt && i < turnAt + settle) during = std::max(during, step);
                    else if (i >= turnAt + settle) after = std::max(after, step);
                }
                previousL = l;
                previousR = r;
            }
            const double ratio = during / std::max(before, after);
            std::printf("  turn 0->60 at %6.0f Hz, %4.0f Hz sine: step %.5f vs settled %.5f/%.5f (x%.3f)\n",
                        fs, hz, during, before, after, ratio);
            char msg[100];
            std::snprintf(msg, sizeof msg, "no click on a head turn (%.0f Hz sine at %.0f Hz)", hz, fs);
            CHECK(ratio <= 1.25, msg);
        }
    }
}

int main() {
    test_the_lateral_angle_inverts_woodworth();
    test_the_estimator_reads_a_known_delay();
    test_the_estimator_reads_a_known_level_difference();
    test_opposite_polarity_is_not_a_single_source();
    test_front_channels_land_on_their_lateral_angles();
    test_surround_channels_have_no_single_source_direction();
    test_head_turn_moves_the_fronts_only();
    test_a_head_turn_does_not_click();
    if (g_failures == 0) std::printf("test_binaural_direction: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
