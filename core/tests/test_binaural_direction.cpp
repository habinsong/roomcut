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
#include "BinauralMeasure.hpp"
#include "SurroundStage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

using namespace roomcut::binaural;

// The measurement's reference head and search window are this renderer's.
static_assert(kReferenceHeadRadiusM == VirtualSpeaker::kDefaultHeadRadiusCm * 0.01);
static_assert(kSearchWindowSeconds == VirtualSpeaker::kMaxItdSeconds);

namespace {

constexpr double kRates[] = {44100.0, 48000.0, 96000.0, 192000.0, 384000.0, 768000.0};

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
        for (const Band& band : thirdOctaves(e, fs)) {
            CHECK(band.bins >= 4, "every third-octave band holds at least four bins");
            worst = std::max(worst, std::fabs(ildDb(band) - 6.0));
        }
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
                    for (const Band& band : thirdOctaves(e, fs))
                        if (band.centreHz == 500 || band.centreHz == 2000 || band.centreHz == 8000)
                            std::printf("      ILD @ %5.0f Hz %+6.2f dB\n", band.centreHz, ildDb(band));
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
