// SurroundStage: the stereo mix played back through two virtual speakers
// that stay put while the head turns. The checks cover what the feature
// promises (the image counter-rotates, the centre stays centred when facing
// forward) and what it must never do (change loudness, click on engage, touch
// the samples while off).
#include "SurroundStage.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
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

struct Stereo { std::vector<double> left, right; };

// Pink-ish noise, the same sequence every time, optionally uncorrelated.
Stereo source(double fs, double seconds, bool uncorrelated) {
    Stereo out;
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    std::mt19937 rng(9001);
    std::normal_distribution<double> white(0.0, 1.0);
    double a0 = 0, a1 = 0, a2 = 0, b0 = 0, b1 = 0, b2 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = white(rng);
        a0 = 0.99765 * a0 + w * 0.0990460;
        a1 = 0.96300 * a1 + w * 0.2965164;
        a2 = 0.57000 * a2 + w * 1.0526913;
        const double l = (a0 + a1 + a2 + w * 0.1848) * 0.05;
        double r = l;
        if (uncorrelated) {
            const double w2 = white(rng);
            b0 = 0.99765 * b0 + w2 * 0.0990460;
            b1 = 0.96300 * b1 + w2 * 0.2965164;
            b2 = 0.57000 * b2 + w2 * 1.0526913;
            r = (b0 + b1 + b2 + w2 * 0.1848) * 0.05;
        }
        out.left.push_back(l);
        out.right.push_back(r);
    }
    return out;
}

Stereo render(const Stereo& in, double fs, double yaw, bool enabled, int layout = 0) {
    SurroundStage stage;
    stage.prepare(fs);
    stage.setLayout(layout);
    stage.setEnabled(enabled);
    stage.setYawDegrees(yaw);
    stage.reset();
    Stereo out;
    out.left.reserve(in.left.size());
    out.right.reserve(in.left.size());
    for (std::size_t i = 0; i < in.left.size(); ++i) {
        float frame[2] = {static_cast<float>(in.left[i]), static_cast<float>(in.right[i])};
        stage.processFrame(frame, 2);
        out.left.push_back(frame[0]);
        out.right.push_back(frame[1]);
    }
    return out;
}

double rms(const std::vector<double>& x, std::size_t from = 0) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < x.size(); ++i) { sum += x[i] * x[i]; ++n; }
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

double levelDb(const Stereo& s, std::size_t from) {
    return 20.0 * std::log10(std::max(std::sqrt((rms(s.left, from) * rms(s.left, from)
                                               + rms(s.right, from) * rms(s.right, from)) * 0.5), 1e-300));
}

} // namespace

static void test_off_is_bit_identical() {
    const double fs = 48000.0;
    const Stereo in = source(fs, 0.5, true);
    const Stereo out = render(in, fs, 0.0, false);
    bool identical = true;
    for (std::size_t i = 0; i < in.left.size(); ++i) {
        identical = identical && out.left[i] == static_cast<float>(in.left[i])
                              && out.right[i] == static_cast<float>(in.right[i]);
    }
    CHECK(identical, "a disabled head-tracked stage leaves every sample untouched");
}

static void test_centre_stays_centred_facing_forward() {
    // Facing straight ahead, the two speakers are mirror images, so a centred
    // signal must still arrive identically at both ears.
    const double fs = 48000.0;
    const Stereo in = source(fs, 0.3, false);      // mono content
    const Stereo out = render(in, fs, 0.0, true);
    double worst = 0.0;
    for (std::size_t i = 0; i < out.left.size(); ++i)
        worst = std::max(worst, std::fabs(out.left[i] - out.right[i]));
    std::printf("  facing forward, centre L-R mismatch: %.3g\n", worst);
    CHECK(worst < 1e-6, "a centred source stays centred while facing forward");
}

static void test_turning_the_head_moves_the_image() {
    // Turn right: the stage stays put, so its energy shifts toward the LEFT ear.
    const double fs = 48000.0;
    const Stereo in = source(fs, 1.0, false);
    const std::size_t skip = static_cast<std::size_t>(fs * 0.1);
    double previous = 0.0;
    for (double yaw : {0.0, 20.0, 45.0}) {
        const Stereo out = render(in, fs, yaw, true);
        const double balance = 20.0 * std::log10(rms(out.left, skip) / std::max(rms(out.right, skip), 1e-300));
        std::printf("  yaw %+5.1f deg -> L/R balance %+5.2f dB\n", yaw, balance);
        if (yaw == 0.0) {
            CHECK(std::fabs(balance) < 0.05, "no rotation keeps the image centred");
        } else {
            CHECK(balance > previous + 0.5, "turning right moves the image toward the left ear");
        }
        previous = balance;
    }
    // And the mirror case behaves symmetrically.
    const Stereo rightTurn = render(in, fs, 30.0, true);
    const Stereo leftTurn = render(in, fs, -30.0, true);
    const double a = 20.0 * std::log10(rms(rightTurn.left, skip) / std::max(rms(rightTurn.right, skip), 1e-300));
    const double b = 20.0 * std::log10(rms(leftTurn.right, skip) / std::max(rms(leftTurn.left, skip), 1e-300));
    CHECK(std::fabs(a - b) < 0.1, "left and right rotations are mirror images");
}

static void test_loudness_is_preserved() {
    const double fs = 48000.0;
    const std::size_t skip = static_cast<std::size_t>(fs * 0.1);
    for (bool uncorrelated : {false, true}) {
        const Stereo in = source(fs, 1.0, uncorrelated);
        const Stereo dry = render(in, fs, 0.0, false);
        const Stereo wet = render(in, fs, 0.0, true);
        const double delta = levelDb(wet, skip) - levelDb(dry, skip);
        std::printf("  %s material: %+.2f dB\n", uncorrelated ? "uncorrelated" : "centred     ", delta);
        CHECK(std::fabs(delta) < 1.2, "virtual speakers do not change how loud the mix is");
    }
}

static void test_engaging_is_click_free() {
    const double fs = 48000.0;
    SurroundStage stage;
    stage.prepare(fs);
    stage.setEnabled(false);
    stage.reset();
    const double freq = 300.0, amp = 0.4;
    const double drySlope = 2.0 * M_PI * freq / fs * amp;
    double phase = 0.0, prevL = 0.0, maxStep = 0.0, peak = 0.0;
    const std::size_t n = static_cast<std::size_t>(fs * 3.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == static_cast<std::size_t>(fs * 0.5)) stage.setEnabled(true);
        if (i == static_cast<std::size_t>(fs * 1.5)) stage.setEnabled(false);   // tracker lost
        if (i == static_cast<std::size_t>(fs * 2.0)) stage.setEnabled(true);
        if (i > static_cast<std::size_t>(fs * 2.2))
            stage.setYawDegrees(50.0 * std::sin((double)i / fs * 3.0));
        const double s = amp * std::sin(phase);
        phase += 2.0 * M_PI * freq / fs;
        float frame[2] = {static_cast<float>(s), static_cast<float>(s)};
        stage.processFrame(frame, 2);
        if (i > 0) maxStep = std::max(maxStep, std::fabs(frame[0] - prevL));
        peak = std::max(peak, std::fabs((double)frame[0]));
        prevL = frame[0];
    }
    std::printf("  max sample step %.5f (dry sine slope %.5f), peak %.3f\n", maxStep, drySlope, peak);
    CHECK(maxStep < drySlope * 3.0, "engaging, losing and sweeping the tracker never steps the output");
    CHECK(peak < 1.0, "the stage keeps the signal inside full scale");
}

static void test_yaw_goes_all_the_way_round() {
    SurroundStage stage;
    stage.prepare(48000.0);
    // No limit: a listener who turns 90 degrees gets 90 degrees of rotation.
    stage.setYawDegrees(90.0);
    CHECK(stage.yawDegrees() == 90.0, "a quarter turn is rendered as a quarter turn");
    stage.setYawDegrees(-150.0);
    CHECK(stage.yawDegrees() == -150.0, "and so is a turn most of the way round");
    // Past the back the angle comes round the other side rather than growing.
    stage.setYawDegrees(200.0);
    CHECK(std::fabs(stage.yawDegrees() + 160.0) < 1e-9, "past the rear centre line the angle wraps");
    stage.setYawDegrees(-999.0);
    CHECK(std::fabs(stage.yawDegrees() - 81.0) < 1e-9, "a runaway value still lands on a real angle");
    stage.setYawDegrees(std::nan(""));
    CHECK(stage.yawDegrees() == 0.0, "a non-finite angle falls back to facing forward");
}

static void test_render_path_does_not_allocate() {
    SurroundStage stage;
    stage.prepare(48000.0);
    stage.setEnabled(true);
    stage.reset();
    g_allocated = 0;
    g_recording = true;
    for (std::size_t i = 0; i < 48000; ++i) {
        if (i % 960 == 0) stage.setYawDegrees(std::sin((double)i / 4800.0) * 45.0);
        if (i % 12000 == 0) stage.setEnabled(i % 24000 == 0);
        float frame[2] = {0.1f, -0.1f};
        stage.processFrame(frame, 2);
    }
    g_recording = false;
    CHECK(g_allocated == 0, "the render path allocates nothing");
}


// ---- Upmixed layouts (5.1 / 7.1) ------------------------------------------

static void test_upmix_keeps_a_centred_source_centred() {
    // A mono programme is all centre channel, and the centre speaker stands
    // straight ahead — so it must still arrive at both ears identically.
    const Stereo in = source(48000.0, 1.0, false);
    for (int layout : {2, 3}) {
        const Stereo out = render(in, 48000.0, 0.0, true, layout);
        double worst = 0.0;
        for (std::size_t i = 24000; i < out.left.size(); ++i)
            worst = std::max(worst, std::fabs(out.left[i] - out.right[i]));
        CHECK(worst < 1e-9, "an upmixed mono source stays exactly centred");
    }
}

static void test_upmix_preserves_loudness() {
    // Switching the upmix on must not be a volume change. These are the two
    // extremes — nothing real is either perfectly correlated or perfectly
    // uncorrelated — and the bus gain is set so the middle of the range sits at
    // unity (see SurroundStage::kUpmixBusGain), which leaves the extremes
    // here inside +-1.7 dB.
    for (bool uncorrelated : {false, true}) {
        const Stereo in = source(48000.0, 2.0, uncorrelated);
        const double dry = levelDb(in, 48000);
        for (int layout : {2, 3}) {
            const Stereo out = render(in, 48000.0, 0.0, true, layout);
            const double delta = levelDb(out, 48000) - dry;
            std::printf("  layout %d, %s: %+.2f dB\n", layout,
                        uncorrelated ? "uncorrelated" : "correlated  ", delta);
            CHECK(std::fabs(delta) < 1.75, "the upmix holds loudness close to the dry signal");
        }
    }
}

// The centre channel is the dialogue, and the centre is the dullest position a
// virtual speaker can occupy: a source straight ahead reaches both ears from
// behind their axis and loses its top. Before the per-angle correction a mono
// programme came out +1.7 dB at 40 Hz and -3.1 dB at 14 kHz — a 4.8 dB tilt on
// exactly the material this feature exists to make clearer.
static void test_upmix_does_not_colour_a_centred_source() {
    const double fs = 48000.0;
    for (int layout : {2, 3}) {
        double worst = 0.0, lowest = 1e9, highest = -1e9;
        for (double hz : {60.0, 250.0, 1000.0, 4000.0, 12000.0}) {
            Stereo tone;
            const std::size_t n = static_cast<std::size_t>(fs * 1.0);
            for (std::size_t i = 0; i < n; ++i) {
                const double s = 0.25 * std::sin(2.0 * M_PI * hz * (double)i / fs);
                tone.left.push_back(s);
                tone.right.push_back(s);
            }
            const double delta = levelDb(render(tone, fs, 0.0, true, layout), n / 2)
                               - levelDb(tone, n / 2);
            lowest = std::min(lowest, delta);
            highest = std::max(highest, delta);
            worst = std::max(worst, std::fabs(delta));
        }
        std::printf("  layout %d centred tone: %.2f to %.2f dB (tilt %.2f dB)\n",
                    layout, lowest, highest, highest - lowest);
        CHECK(highest - lowest < 2.5, "a centred source keeps its tone through the upmix");
        CHECK(worst < 2.5, "and keeps its level");
    }
}

// Engaging a layout is not instant: the steering starts with everything in
// front, so a mono programme arrives as a phantom pair across the front
// speakers and settles onto the discrete centre speaker over about 160 ms. A
// phantom centre is inherently hotter than a discrete one, so the level glides
// down by a measured 3.6 dB. That is a property of the decomposition, not a
// fault — what matters is that it GLIDES rather than steps, and that it is over
// quickly. This pins both.
static void test_upmix_engaging_settles_smoothly() {
    const double fs = 48000.0;
    const std::size_t n = static_cast<std::size_t>(fs * 0.5);
    Stereo tone;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = 0.25 * std::sin(2.0 * M_PI * 440.0 * (double)i / fs);
        tone.left.push_back(s);
        tone.right.push_back(s);
    }
    const Stereo out = render(tone, fs, 0.0, true, 2);
    const auto windowPeak = [&](double fromMs, double toMs) {
        double peak = 0.0;
        for (std::size_t i = (std::size_t)(fs * fromMs / 1000.0);
             i < std::min(out.left.size(), (std::size_t)(fs * toMs / 1000.0)); ++i)
            peak = std::max(peak, std::fabs(out.left[i]));
        return peak;
    };
    const double onset = 20.0 * std::log10(windowPeak(0, 10) / 0.25);
    const double settled = 20.0 * std::log10(windowPeak(300, 500) / 0.25);
    std::printf("  upmix onset %+.2f dB, settled %+.2f dB\n", onset, settled);
    CHECK(onset < 4.0, "engaging a layout does not overshoot beyond the phantom-centre difference");
    CHECK(std::fabs(settled) < 1.0, "and it settles to unity");
    CHECK(20.0 * std::log10(windowPeak(150, 200) / 0.25) < 1.0, "the glide is over inside 200 ms");

    // A glide, not a step: no sample-to-sample jump beyond the tone's own slope.
    double worstStep = 0.0, drySlope = 0.0;
    for (std::size_t i = 1; i < tone.left.size(); ++i)
        drySlope = std::max(drySlope, std::fabs(tone.left[i] - tone.left[i - 1]));
    for (std::size_t i = 1; i < out.left.size(); ++i)
        worstStep = std::max(worstStep, std::fabs(out.left[i] - out.left[i - 1]));
    CHECK(worstStep < drySlope * 2.0, "the settle never steps the waveform");
}

static void test_upmix_rotation_stays_level() {
    // Turning the head moves every speaker at once, so the level changes
    // cancel. This is the check that a runaway loudness swing never reaches a
    // listener who simply looked away from the screen.
    const Stereo in = source(48000.0, 1.0, true);
    for (int layout : {2, 3}) {
        double lo = 1e9, hi = -1e9;
        for (double yaw = -60.0; yaw <= 60.0; yaw += 20.0) {
            const double level = levelDb(render(in, 48000.0, yaw, true, layout), 24000);
            lo = std::min(lo, level);
            hi = std::max(hi, level);
        }
        std::printf("  layout %d rotation spread: %.2f dB\n", layout, hi - lo);
        CHECK(hi - lo < 1.5, "a full head turn barely changes the upmixed level");
    }
}

static void test_upmix_layout_switch_is_click_free() {
    // The steering gains start from zero, which renders exactly like the plain
    // pair, so opening a layout mid-track should slide rather than step.
    const Stereo in = source(48000.0, 1.0, true);
    SurroundStage stage;
    stage.prepare(48000.0);
    stage.setEnabled(true);
    stage.reset();
    double previous = 0.0, worstStep = 0.0, drySlope = 0.0;
    for (std::size_t i = 1; i < in.left.size(); ++i)
        drySlope = std::max(drySlope, std::fabs(in.left[i] - in.left[i - 1]));
    for (std::size_t i = 0; i < in.left.size(); ++i) {
        if (i == 24000) stage.setLayout(2);
        if (i == 36000) stage.setLayout(3);
        if (i == 44000) stage.setLayout(0);
        float frame[2] = {static_cast<float>(in.left[i]), static_cast<float>(in.right[i])};
        stage.processFrame(frame, 2);
        if (i > 0) worstStep = std::max(worstStep, std::fabs(frame[0] - previous));
        previous = frame[0];
    }
    CHECK(worstStep < drySlope * 4.0, "changing layout mid-track does not step the output");
}

static void test_upmix_render_path_does_not_allocate() {
    const Stereo in = source(48000.0, 0.2, true);
    SurroundStage stage;
    stage.prepare(48000.0);
    stage.setLayout(3);
    stage.setEnabled(true);
    stage.reset();
    g_allocated = 0;
    g_recording = true;
    for (std::size_t i = 0; i < in.left.size(); ++i) {
        float frame[2] = {static_cast<float>(in.left[i]), static_cast<float>(in.right[i])};
        stage.processFrame(frame, 2);
    }
    stage.setLayout(2);
    stage.setSurroundLevels(-3.0, 2.0);
    stage.setYawDegrees(20.0);
    g_recording = false;
    CHECK(g_allocated == 0, "the upmixed render path and its setters allocate nothing");
}

// The diffuse bus promises fixed arrival times: sides at 13 ms (right ear 6.5 ms
// later), backs at 23 ms (right ear 9 ms later). Each is driven on its own and
// the first sample that reaches either ear must land exactly there, at every
// rate the engine accepts (it gates at 768 kHz). The line was once sized for
// 20 ms, which silently clamped the 7.1 back pair above 512 kHz.
static void test_surround_arrivals_hold_at_every_rate() {
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0,
                            192000.0, 352800.0, 384000.0, 705600.0, 768000.0};
    struct Feed { double UpmixFrame::*channel; double ms; const char* name; };
    const Feed feeds[] = {{&UpmixFrame::sideL, SurroundStage::kSurroundDelayMs, "sideL"},
                          {&UpmixFrame::sideR, SurroundStage::kSurroundDelayMs + SurroundStage::kSurroundSkewMs, "sideR"},
                          {&UpmixFrame::backL, SurroundStage::kBackDelayMs, "backL"},
                          {&UpmixFrame::backR, SurroundStage::kBackDelayMs + SurroundStage::kBackSkewMs, "backR"}};
    for (double fs : rates) {
        for (const Feed& feed : feeds) {
            SurroundStage stage;
            stage.prepare(fs);
            stage.setLayout(3);
            stage.reset();
            const long expected = std::lround(fs * feed.ms * 0.001);
            long firstL = -1, firstR = -1;
            for (long i = 0; i <= expected + 8; ++i) {
                UpmixFrame up;
                up.*feed.channel = i == 0 ? 1.0 : 0.0;
                double l = 0.0, r = 0.0;
                stage.renderBed(up, l, r);
                if (firstL < 0 && l != 0.0) firstL = i;
                if (firstR < 0 && r != 0.0) firstR = i;
            }
            char msg[160];
            std::snprintf(msg, sizeof msg, "%s arrives at %ld samples at %.0f Hz (L %ld, R %ld)",
                          feed.name, expected, fs, firstL, firstR);
            CHECK(firstL == expected && firstR == expected, msg);
        }
    }
}

int main() {
    test_surround_arrivals_hold_at_every_rate();
    test_off_is_bit_identical();
    test_centre_stays_centred_facing_forward();
    test_turning_the_head_moves_the_image();
    test_loudness_is_preserved();
    test_engaging_is_click_free();
    test_yaw_goes_all_the_way_round();
    test_render_path_does_not_allocate();
    test_upmix_keeps_a_centred_source_centred();
    test_upmix_preserves_loudness();
    test_upmix_does_not_colour_a_centred_source();
    test_upmix_engaging_settles_smoothly();
    test_upmix_rotation_stays_level();
    test_upmix_layout_switch_is_click_free();
    test_upmix_render_path_does_not_allocate();
    if (g_failures == 0) std::printf("test_surround_stage: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
