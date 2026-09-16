// RoomSim: the optional virtual room (early reflections + FDN tail).
// The checks here are the safety contract for the stage: off must be
// bit-identical, the decay must match the room it claims to be, the bass must
// stay out of it, switching rooms must not click, and the render path must
// never allocate.
#include "RoomSim.hpp"
#include "Biquad.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)
#define CHECK_NEAR(a, b, tol, msg) do { \
    double _d = std::fabs((a) - (b)); \
    if (_d > (tol)) { std::fprintf(stderr, "FAIL: %s (%g vs %g, |d|=%g > %g) (%s:%d)\n", \
        (msg), (double)(a), (double)(b), _d, (double)(tol), __FILE__, __LINE__); g_failures++; } \
} while (0)

// Allocation counter for the render path. Armed only around processFrame calls.
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

// Run `frames` of silence so the wet ramp reaches its target before measuring.
void settle(RoomSim& room, double fs, double seconds = 0.05) {
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    for (std::size_t i = 0; i < n; ++i) {
        float frame[2] = {0.0f, 0.0f};
        room.processFrame(frame, 2);
    }
}

// Impulse response of the wet path (the dry impulse sits at sample 0 and the
// earliest reflection arrives after the predelay, so the tail is wet only).
std::vector<double> impulseResponse(int type, double amount, double fs, double seconds,
                                    std::vector<double>* right = nullptr) {
    RoomSim room;
    room.prepare(fs);
    room.setParams(type, amount);
    settle(room, fs);
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    std::vector<double> out(n, 0.0);
    if (right) right->assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        float frame[2] = {i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f};
        room.processFrame(frame, 2);
        out[i] = frame[0];
        if (right) (*right)[i] = frame[1];
    }
    out[0] -= 1.0;                       // remove the dry impulse
    if (right) (*right)[0] -= 1.0;
    return out;
}

std::vector<double> bandpass(const std::vector<double>& x, double fs, double freq, double q) {
    Biquad filter;
    filter.set(BiquadType::BandPass, fs, freq, 0.0, q);
    std::vector<double> y(x.size(), 0.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = filter.processSample(static_cast<float>(x[i]), 0);
    return y;
}

double energy(const std::vector<double>& x) {
    double e = 0.0;
    for (double v : x) e += v * v;
    return e;
}

// Schroeder backward integration → T20 → RT60. Returns 0 if the decay never
// spans the measurement range.
double rt60FromT20(const std::vector<double>& ir, double fs) {
    const std::size_t n = ir.size();
    std::vector<double> edc(n + 1, 0.0);
    for (std::size_t i = n; i-- > 0;) edc[i] = edc[i + 1] + ir[i] * ir[i];
    if (edc[0] <= 0.0) return 0.0;
    const double ref = edc[0];
    double t5 = -1.0, t25 = -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double db = 10.0 * std::log10(std::max(edc[i], 1e-300) / ref);
        if (t5 < 0.0 && db <= -5.0) t5 = static_cast<double>(i) / fs;
        if (db <= -25.0) { t25 = static_cast<double>(i) / fs; break; }
    }
    if (t5 < 0.0 || t25 < 0.0 || t25 <= t5) return 0.0;
    return 3.0 * (t25 - t5);
}

// The decay time describes the late field, so the measurement starts after the
// last early reflection (the tap table never runs past 100 ms).
std::vector<double> lateWindow(const std::vector<double>& x, double fs, double skipSeconds = 0.12) {
    const std::size_t start = std::min(x.size(), static_cast<std::size_t>(fs * skipSeconds));
    return std::vector<double>(x.begin() + static_cast<std::ptrdiff_t>(start), x.end());
}

double rms(const std::vector<double>& x) {
    return x.empty() ? 0.0 : std::sqrt(energy(x) / static_cast<double>(x.size()));
}

} // namespace

static void test_off_is_bit_identical() {
    RoomSim room;
    room.prepare(48000.0);
    room.setParams(RoomSim::kOff, 50.0);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    bool identical = true;
    for (int i = 0; i < 4800; ++i) {
        float frame[2] = {dist(rng), dist(rng)};
        const float before[2] = {frame[0], frame[1]};
        room.processFrame(frame, 2);
        identical = identical && frame[0] == before[0] && frame[1] == before[1];
    }
    CHECK(identical, "room off leaves every sample bit-identical");

    // Amount 0 on a real room is the same as off.
    RoomSim quiet;
    quiet.prepare(48000.0);
    quiet.setParams(RoomSim::kHall, 0.0);
    bool quietIdentical = true;
    for (int i = 0; i < 4800; ++i) {
        float frame[2] = {dist(rng), dist(rng)};
        const float before[2] = {frame[0], frame[1]};
        quiet.processFrame(frame, 2);
        quietIdentical = quietIdentical && frame[0] == before[0] && frame[1] == before[1];
    }
    CHECK(quietIdentical, "amount 0 leaves every sample bit-identical");
}

static void test_room_produces_a_decaying_tail() {
    const double fs = 48000.0;
    const std::vector<double> ir = impulseResponse(RoomSim::kStudio, 50.0, fs, 2.0);
    double peak = 0.0;
    bool finite = true;
    for (double v : ir) {
        peak = std::max(peak, std::fabs(v));
        finite = finite && std::isfinite(v);
    }
    CHECK(finite, "studio impulse response stays finite");
    CHECK(peak > 1e-4, "studio produces an audible reflection pattern");

    // The first reflection must arrive after the predelay, never before.
    std::size_t firstNonZero = ir.size();
    for (std::size_t i = 0; i < ir.size(); ++i) {
        if (std::fabs(ir[i]) > 1e-9) { firstNonZero = i; break; }
    }
    CHECK(firstNonZero > 0, "the room never adds energy to the direct sound");

    // Far past the decay time the tail must be gone, not ringing.
    double tailPeak = 0.0;
    for (std::size_t i = static_cast<std::size_t>(fs * 1.5); i < ir.size(); ++i)
        tailPeak = std::max(tailPeak, std::fabs(ir[i]));
    CHECK(tailPeak < 1e-5, "studio tail has decayed to silence after 1.5 s");
}

static void test_rt60_matches_each_room() {
    const double fs = 48000.0;
    struct Case { int type; double target; double seconds; };
    const Case cases[] = {
        {RoomSim::kStudio, 0.22, 3.0},
        {RoomSim::kLiving, 0.55, 4.0},
        {RoomSim::kHall, 1.70, 8.0},
    };
    double measured[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const std::vector<double> ir = impulseResponse(cases[i].type, 50.0, fs, cases[i].seconds);
        const std::vector<double> band = lateWindow(bandpass(ir, fs, 1000.0, 1.41), fs);
        measured[i] = rt60FromT20(band, fs);
        std::printf("  RT60(1 kHz) room %d: %.3f s (target %.2f s)\n",
                    cases[i].type, measured[i], cases[i].target);
        CHECK(measured[i] > cases[i].target * 0.55, "measured decay is not far shorter than the target");
        CHECK(measured[i] < cases[i].target * 1.30, "measured decay is not far longer than the target");
    }
    CHECK(measured[0] < measured[1] && measured[1] < measured[2],
          "rooms get progressively more reverberant");
}

static void test_bass_stays_out_of_the_room() {
    const double fs = 48000.0;
    const std::vector<double> ir = impulseResponse(RoomSim::kHall, 50.0, fs, 6.0);
    const double low = energy(bandpass(ir, fs, 60.0, 1.41));
    const double mid = energy(bandpass(ir, fs, 1000.0, 1.41));
    const double ratioDb = 10.0 * std::log10(std::max(low, 1e-300) / std::max(mid, 1e-300));
    std::printf("  hall tail 60 Hz vs 1 kHz: %.1f dB\n", ratioDb);
    CHECK(ratioDb < -8.0, "the room send is high-passed so the bass stays dry");
}

static void test_level_increase_is_subtle() {
    const double fs = 48000.0;
    std::mt19937 rng(11);
    std::normal_distribution<double> noise(0.0, 0.1);   // ~ -20 dBFS RMS
    struct Case { int type; double maxDb; };
    const Case cases[] = {{RoomSim::kStudio, 1.0}, {RoomSim::kLiving, 1.2}, {RoomSim::kHall, 1.5}};
    for (const Case& c : cases) {
        RoomSim room;
        room.prepare(fs);
        room.setParams(c.type, 50.0);
        settle(room, fs, 0.5);
        std::vector<double> in, out;
        const std::size_t n = static_cast<std::size_t>(fs * 3.0);
        in.reserve(n); out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const float l = static_cast<float>(noise(rng));
            const float r = static_cast<float>(noise(rng));
            float frame[2] = {l, r};
            room.processFrame(frame, 2);
            in.push_back(l); in.push_back(r);
            out.push_back(frame[0]); out.push_back(frame[1]);
        }
        const double gainDb = 20.0 * std::log10(rms(out) / rms(in));
        std::printf("  level increase room %d: %+.2f dB\n", c.type, gainDb);
        CHECK(gainDb > 0.0, "the room adds energy");
        CHECK(gainDb < c.maxDb, "the room stays a subtle addition, not a wash");
    }
}

static void test_amount_scales_the_room() {
    const double fs = 48000.0;
    const double quarter = std::sqrt(energy(impulseResponse(RoomSim::kLiving, 25.0, fs, 4.0)));
    const double half = std::sqrt(energy(impulseResponse(RoomSim::kLiving, 50.0, fs, 4.0)));
    const double full = std::sqrt(energy(impulseResponse(RoomSim::kLiving, 100.0, fs, 4.0)));
    // Below the reference the slider is plain linear...
    CHECK_NEAR(20.0 * std::log10(half / std::max(quarter, 1e-300)), 6.02, 0.5,
               "below the reference level the Amount slider is linear");
    // ...and above it the curve flattens toward the room's ceiling, because past
    // that point more wet is comb filtering, not more space (see amountScale).
    const double topDb = 20.0 * std::log10(full / std::max(half, 1e-300));
    std::printf("  amount 100 vs 50: %+.2f dB\n", topDb);
    CHECK(topDb > 2.0, "the top of the slider still adds a clearly audible amount");
    CHECK(topDb < 5.0, "but never the full doubling that combs against the dry signal");
}

static void test_sample_rates() {
    const double rates[] = {44100.0, 48000.0, 96000.0, 192000.0};
    for (double fs : rates) {
        const std::vector<double> ir = impulseResponse(RoomSim::kStudio, 50.0, fs, 3.0);
        bool finite = true;
        for (double v : ir) finite = finite && std::isfinite(v);
        CHECK(finite, "impulse response stays finite at every sample rate");
        const double rt = rt60FromT20(lateWindow(bandpass(ir, fs, 1000.0, 1.41), fs), fs);
        std::printf("  RT60 at %.0f Hz: %.3f s\n", fs, rt);
        CHECK(rt > 0.25 * 0.55 && rt < 0.25 * 1.30, "decay time is sample-rate independent");
    }
}

static void test_room_switch_is_click_free() {
    const double fs = 48000.0;
    RoomSim room;
    room.prepare(fs);
    room.setParams(RoomSim::kStudio, 50.0);
    settle(room, fs, 0.2);

    const double freq = 500.0;
    const double amp = 0.5;
    const double slopeLimit = 2.0 * M_PI * freq / fs * amp;   // dry sine slope
    double phase = 0.0;
    double prev = 0.0;
    double maxStep = 0.0;
    double peak = 0.0;
    const std::size_t n = static_cast<std::size_t>(fs * 2.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == static_cast<std::size_t>(fs * 0.5)) room.setParams(RoomSim::kHall, 50.0);
        if (i == static_cast<std::size_t>(fs * 1.0)) room.setParams(RoomSim::kOff, 50.0);
        if (i == static_cast<std::size_t>(fs * 1.5)) room.setParams(RoomSim::kLiving, 100.0);
        const float s = static_cast<float>(amp * std::sin(phase));
        phase += 2.0 * M_PI * freq / fs;
        float frame[2] = {s, s};
        room.processFrame(frame, 2);
        if (i > 0) maxStep = std::max(maxStep, std::fabs(frame[0] - prev));
        peak = std::max(peak, std::fabs((double)frame[0]));
        prev = frame[0];
    }
    std::printf("  max sample step %.5f (dry sine slope %.5f), peak %.3f\n",
                maxStep, slopeLimit, peak);
    CHECK(maxStep < slopeLimit * 3.0, "switching rooms never produces a step discontinuity");
    CHECK(peak < 1.05, "switching rooms leaves the limiter something to work with");
}

static void test_render_path_does_not_allocate() {
    const double fs = 48000.0;
    RoomSim room;
    room.prepare(fs);
    room.setParams(RoomSim::kHall, 60.0);
    settle(room, fs, 0.1);

    std::mt19937 rng(3);
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
    g_allocated = 0;
    g_recording = true;
    for (std::size_t i = 0; i < static_cast<std::size_t>(fs); ++i) {
        if (i == 10000) room.setParams(RoomSim::kStudio, 40.0);
        if (i == 20000) room.setParams(RoomSim::kOff, 40.0);
        if (i == 30000) room.setParams(RoomSim::kLiving, 80.0);
        float frame[2] = {dist(rng), dist(rng)};
        room.processFrame(frame, 2);
    }
    g_recording = false;
    CHECK(g_allocated == 0, "processFrame and setParams allocate nothing");
}

// Run the same pink-ish noise through a room and return the stereo output.
// (A fixed seed keeps every room comparable sample for sample.)
static void noiseThroughRoom(int type, double fs, double seconds,
                             std::vector<double>& left, std::vector<double>& right,
                             double amount = 50.0, bool headphone = true) {
    RoomSim room;
    room.prepare(fs);
    room.setParams(type, amount, headphone);
    settle(room, fs, 0.2);
    std::mt19937 rng(4242);
    std::normal_distribution<double> white(0.0, 1.0);
    double b0 = 0, b1 = 0, b2 = 0;
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    left.clear(); right.clear();
    left.reserve(n); right.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double w = white(rng);
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        const float s = static_cast<float>((b0 + b1 + b2 + w * 0.1848) * 0.02);
        float frame[2] = {s, s};
        room.processFrame(frame, 2);
        left.push_back(frame[0]);
        right.push_back(frame[1]);
    }
}

static double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    const double n = static_cast<double>(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        sa += a[i]; sb += b[i]; saa += a[i] * a[i]; sbb += b[i] * b[i]; sab += a[i] * b[i];
    }
    const double cov = sab / n - (sa / n) * (sb / n);
    const double va = saa / n - (sa / n) * (sa / n);
    const double vb = sbb / n - (sb / n) * (sb / n);
    return cov / std::sqrt(std::max(va * vb, 1e-300));
}

// Third-octave spread of room-on against room-off: how much comb the wet signal
// leaves in the dry one. This is the measurement behind the ceiling — at full
// amount the old linear law reached 4.8 dB on speakers, which reads as tearing.
static double combRippleDb(int type, double amount, bool headphone, double fs) {
    std::vector<double> dryL, dryR, wetL, wetR;
    noiseThroughRoom(RoomSim::kOff, fs, 4.0, dryL, dryR, amount, headphone);
    noiseThroughRoom(type, fs, 4.0, wetL, wetR, amount, headphone);
    double lo = 1e9, hi = -1e9;
    for (double f = 100.0; f <= 12000.0; f *= 1.26) {
        const double d = 10.0 * std::log10(std::max(energy(bandpass(wetL, fs, f, 1.41)), 1e-300)
                                         / std::max(energy(bandpass(dryL, fs, f, 1.41)), 1e-300));
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    return hi - lo;
}

static void test_full_amount_does_not_comb() {
    const double fs = 48000.0;
    for (bool headphone : {true, false}) {
        for (int type : {RoomSim::kStudio, RoomSim::kLiving, RoomSim::kHall}) {
            const double ripple = combRippleDb(type, 100.0, headphone, fs);
            std::printf("  %-9s room %d at amount 100: comb ripple %.2f dB\n",
                        headphone ? "headphone" : "speaker", type, ripple);
            CHECK(ripple < 3.0, "a room at full amount adds space, not comb filtering");
        }
    }
}

static void test_room_decorrelates_the_ears() {
    // The point of the stage: a mono source must arrive at the two ears as two
    // slightly different signals — that difference is the envelopment cue.
    const double fs = 48000.0;
    std::vector<double> l, r;
    double previousSide = 0.0;
    double previousCorr = 1.1;
    const int rooms[3] = {RoomSim::kStudio, RoomSim::kLiving, RoomSim::kHall};
    for (int type : rooms) {
        noiseThroughRoom(type, fs, 4.0, l, r);
        std::vector<double> mid(l.size()), side(l.size());
        for (std::size_t i = 0; i < l.size(); ++i) {
            mid[i] = (l[i] + r[i]) * 0.5;
            side[i] = (l[i] - r[i]) * 0.5;
        }
        const double ratio = rms(side) / std::max(rms(mid), 1e-300);
        const double corr = correlation(l, r);
        std::printf("  room %d: side/mid %.4f, L/R correlation %.4f\n", type, ratio, corr);
        CHECK(ratio > previousSide, "a larger room is more enveloping than a smaller one");
        CHECK(corr < previousCorr, "a larger room decorrelates the ears further");
        CHECK(corr > 0.5, "the room never destroys the centre image");
        previousSide = ratio;
        previousCorr = corr;
    }
}

static void test_room_keeps_the_tonal_balance() {
    // Real material, not a steady tone: a room must add space without becoming
    // an EQ. Every octave band has to stay within a dB or so of the dry signal.
    const double fs = 48000.0;
    const double centers[9] = {63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    std::vector<double> dryL, dryR, wetL, wetR;
    noiseThroughRoom(RoomSim::kOff, fs, 4.0, dryL, dryR);
    // A bigger, wetter room legitimately colours more than a small dry one, so
    // each room answers to its own budget rather than one blanket number.
    const std::pair<int, double> budgets[3] = {
        {RoomSim::kStudio, 0.8}, {RoomSim::kLiving, 1.4}, {RoomSim::kHall, 2.0}};
    for (const auto& [type, budget] : budgets) {
        noiseThroughRoom(type, fs, 4.0, wetL, wetR);
        double worst = 0.0;
        for (double f : centers) {
            const double dry = energy(bandpass(dryL, fs, f, 1.41));
            const double wet = energy(bandpass(wetL, fs, f, 1.41));
            const double db = 10.0 * std::log10(std::max(wet, 1e-300) / std::max(dry, 1e-300));
            worst = std::max(worst, std::fabs(db));
        }
        std::printf("  room %d: worst octave-band shift %.2f dB (budget %.1f)\n", type, worst, budget);
        CHECK(worst < budget, "the room colours no more than its size justifies");
    }
}

// Where the wet signal starts, in milliseconds.
static double firstArrivalMs(const std::vector<double>& ir, double fs) {
    for (std::size_t i = 0; i < ir.size(); ++i)
        if (std::fabs(ir[i]) > 1e-7) return 1000.0 * (double)i / fs;
    return 0.0;
}

// Wet impulse response for either profile over a fixed window, so energies
// from different rooms are directly comparable.
static std::vector<double> profileResponse(int type, bool headphone, double fs, double seconds) {
    RoomSim room;
    room.prepare(fs);
    room.setParams(type, 50.0, headphone);
    settle(room, fs, 0.1);
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    std::vector<double> out(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        float frame[2] = {i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f};
        room.processFrame(frame, 2);
        out[i] = frame[0];
    }
    out[0] -= 1.0;
    return out;
}

// Centre time: the energy-weighted mean arrival time. A standard room-acoustic
// measure of how spread out in time the sound is — small rooms sit early,
// halls sit late.
static double centreTimeMs(const std::vector<double>& ir, double fs) {
    double weighted = 0.0, total = 0.0;
    for (std::size_t i = 0; i < ir.size(); ++i) {
        const double e = ir[i] * ir[i];
        weighted += e * (double)i;
        total += e;
    }
    return total > 0.0 ? 1000.0 * weighted / total / fs : 0.0;
}

static void test_rooms_sound_like_different_places() {
    // The three rooms have to be three places, not three settings of one — on
    // headphones AND on speakers: the tail has to run much longer, the energy has
    // to sit later, and there has to be more room in the mix at each step.
    const double fs = 48000.0;
    const int rooms[3] = {RoomSim::kStudio, RoomSim::kLiving, RoomSim::kHall};
    for (bool headphone : {true, false}) {
        double decay[3] = {0, 0, 0}, centre[3] = {0, 0, 0}, level[3] = {0, 0, 0};
        for (int i = 0; i < 3; ++i) {
            const std::vector<double> ir = profileResponse(rooms[i], headphone, fs, 6.0);
            decay[i] = rt60FromT20(lateWindow(bandpass(ir, fs, 1000.0, 1.41), fs), fs);
            centre[i] = centreTimeMs(ir, fs);
            level[i] = 10.0 * std::log10(std::max(energy(ir), 1e-300));
            std::printf("  %-9s room %d: RT60 %.2f s, centre time %6.1f ms, wet energy %+6.1f dB, starts %5.1f ms\n",
                        headphone ? "headphone" : "speaker", rooms[i], decay[i], centre[i], level[i],
                        firstArrivalMs(ir, fs));
        }
        for (int i = 0; i < 2; ++i) {
            CHECK(decay[i + 1] > decay[i] * 1.8, "each room's tail runs far longer");
            CHECK(centre[i + 1] > centre[i] * 1.5, "each room's energy sits clearly later");
            CHECK(level[i + 1] > level[i] + 1.5, "each room puts more room in the mix");
        }
    }
}

static void test_speakers_get_a_different_room() {
    // A listener on speakers already has a room; the synthetic one must not add
    // a second set of early reflections on top of the real ones.
    const double fs = 48000.0;
    RoomSim headphone, speaker;
    headphone.prepare(fs);
    speaker.prepare(fs);
    headphone.setParams(RoomSim::kLiving, 50.0, true);
    speaker.setParams(RoomSim::kLiving, 50.0, false);
    settle(headphone, fs, 0.1);
    settle(speaker, fs, 0.1);

    const std::size_t n = static_cast<std::size_t>(fs * 3.0);
    std::vector<double> hp(n, 0.0), sp(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        float a[2] = {i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f};
        float b[2] = {a[0], a[1]};
        headphone.processFrame(a, 2);
        speaker.processFrame(b, 2);
        hp[i] = a[0]; sp[i] = b[0];
    }
    hp[0] -= 1.0; sp[0] -= 1.0;

    const double hpStart = firstArrivalMs(hp, fs);
    const double spStart = firstArrivalMs(sp, fs);
    const double hpLevel = 20.0 * std::log10(std::max(rms(hp), 1e-300));
    const double spLevel = 20.0 * std::log10(std::max(rms(sp), 1e-300));
    std::printf("  headphone: starts %5.1f ms, wet %+6.1f dBFS\n", hpStart, hpLevel);
    std::printf("  speaker  : starts %5.1f ms, wet %+6.1f dBFS\n", spStart, spLevel);
    CHECK(spStart > hpStart + 3.0, "the speaker room starts later, behind the direct sound");
    CHECK(spLevel < hpLevel - 1.0, "the speaker room sits lower — the real room is already there");

    // The first 25 ms is where a real room's own reflections live: the speaker
    // profile must leave that window essentially empty.
    const std::size_t window = static_cast<std::size_t>(fs * 0.025);
    double hpEarly = 0.0, spEarly = 0.0;
    for (std::size_t i = 0; i < window; ++i) { hpEarly += hp[i] * hp[i]; spEarly += sp[i] * sp[i]; }
    std::printf("  first 25 ms energy: headphone %.3e, speaker %.3e\n", hpEarly, spEarly);
    CHECK(spEarly < hpEarly * 0.05, "no synthetic early reflections on speakers");

    // Still a stereo field, not a mono blob.
    std::vector<double> l, r;
    RoomSim room;
    room.prepare(fs);
    room.setParams(RoomSim::kLiving, 50.0, false);
    settle(room, fs, 0.2);
    for (std::size_t i = 0; i < static_cast<std::size_t>(fs); ++i) {
        const float s = static_cast<float>(0.05 * std::sin(2.0 * M_PI * 700.0 * (double)i / fs));
        float frame[2] = {s, s};
        room.processFrame(frame, 2);
        l.push_back(frame[0]); r.push_back(frame[1]);
    }
    std::vector<double> side(l.size());
    for (std::size_t i = 0; i < l.size(); ++i) side[i] = (l[i] - r[i]) * 0.5;
    CHECK(rms(side) > 1e-4, "the speaker room still spreads left from right");
}

static void test_left_right_balance() {
    const double fs = 48000.0;
    std::vector<double> rightIr;
    const std::vector<double> leftIr = impulseResponse(RoomSim::kLiving, 50.0, fs, 4.0, &rightIr);
    const double balanceDb = 20.0 * std::log10(rms(leftIr) / std::max(rms(rightIr), 1e-300));
    std::printf("  L/R tail balance: %+.2f dB\n", balanceDb);
    CHECK(std::fabs(balanceDb) < 1.0, "a centred source drives a left/right balanced room");
}

int main() {
    test_off_is_bit_identical();
    test_room_produces_a_decaying_tail();
    test_rt60_matches_each_room();
    test_bass_stays_out_of_the_room();
    test_level_increase_is_subtle();
    test_amount_scales_the_room();
    test_full_amount_does_not_comb();
    test_sample_rates();
    test_room_switch_is_click_free();
    test_render_path_does_not_allocate();
    test_left_right_balance();
    test_room_decorrelates_the_ears();
    test_rooms_sound_like_different_places();
    test_speakers_get_a_different_room();
    test_room_keeps_the_tonal_balance();
    if (g_failures == 0) std::printf("test_roomsim: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
