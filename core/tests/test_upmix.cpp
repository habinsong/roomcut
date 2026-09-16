// Upmixer: a stereo pair taken apart into 5.1/7.1 channels. The checks are the
// PRD's acceptance criteria (F2-FR1..FR6) stated as measurements — where each
// kind of material ends up, that the pieces still fold back down to the input,
// and that switching the feature off changes nothing at all.
#include "Upmixer.hpp"

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

using roomcut::Upmixer;
using roomcut::UpmixFrame;

namespace {

constexpr double kPi = 3.14159265358979323846;

// A deterministic generator, so a failure is always the same failure.
struct Noise {
    unsigned state = 22695477u;
    double next() {
        state = state * 1103515245u + 12345u;
        return (double)((state >> 8) & 0xFFFFu) / 32768.0 - 1.0;
    }
};

// Energy of each destination after the steering has settled.
struct Energies {
    double centre = 0.0, front = 0.0, surround = 0.0;
    double total() const { return centre + front + surround; }
};

// Runs `seconds` of a generated pair through the upmixer and sums energy over
// the last third only — the gain smoothers need ~100 ms to reach their target.
template <class Source>
Energies measure(Upmixer& up, double fs, double seconds, Source source) {
    const long total = (long)(fs * seconds);
    const long from = total * 2 / 3;
    Energies energy;
    UpmixFrame out;
    for (long n = 0; n < total; ++n) {
        double left = 0.0, right = 0.0;
        source(n, left, right);
        up.process(left, right, out);
        if (n < from) continue;
        energy.centre += out.centre * out.centre;
        energy.front += out.frontL * out.frontL + out.frontR * out.frontR;
        energy.surround += out.sideL * out.sideL + out.sideR * out.sideR
                         + out.backL * out.backL + out.backR * out.backR;
    }
    return energy;
}

void test_off_is_bit_identical() {
    // F2-FR6: with the feature off nothing may change, not approximately.
    for (int layout : {Upmixer::kOff, 1}) {
        Upmixer up;
        up.prepare(48000.0);
        up.setLayout(layout);
        Noise noise;
        UpmixFrame out;
        bool exact = true, silent = true;
        for (int n = 0; n < 4800; ++n) {
            const double left = noise.next(), right = noise.next();
            up.process(left, right, out);
            if (out.frontL != left || out.frontR != right) exact = false;
            if (out.centre != 0.0 || out.sideL != 0.0 || out.sideR != 0.0
                || out.backL != 0.0 || out.backR != 0.0) silent = false;
        }
        CHECK(exact, "layout off passes the pair through bit-identically");
        CHECK(silent, "layout off produces no centre or surround at all");
    }
}

void test_downmix_folds_back_exactly() {
    // F2-FR4: the upmix is a partition. Fold every channel back onto the side
    // it came from and the original must return, sample for sample.
    for (int layout : {Upmixer::k51, Upmixer::k71}) {
        Upmixer up;
        up.prepare(48000.0);
        up.setLayout(layout);
        Noise noise;
        UpmixFrame out;
        double worstL = 0.0, worstR = 0.0;
        for (int n = 0; n < 96000; ++n) {
            // Correlated centre + panned tone + independent ambience: every
            // steering path is active at once.
            const double voice = std::sin(2.0 * kPi * 700.0 * n / 48000.0) * 0.4;
            const double guitar = std::sin(2.0 * kPi * 2100.0 * n / 48000.0) * 0.3;
            const double left = voice + guitar + noise.next() * 0.2;
            const double right = voice + noise.next() * 0.2;
            up.process(left, right, out);
            worstL = std::max(worstL, std::fabs(out.centre + out.frontL + out.sideL + out.backL - left));
            worstR = std::max(worstR, std::fabs(out.centre + out.frontR + out.sideR + out.backR - right));
        }
        CHECK(worstL < 1.0e-12 && worstR < 1.0e-12, "every channel folds back down to the input exactly");
    }
}

void test_centre_material_goes_to_the_centre() {
    // F2-FR2: a mono voice belongs in the centre channel.
    Upmixer up;
    up.prepare(48000.0);
    up.setLayout(Upmixer::k51);
    const auto mono = [](long n, double& l, double& r) {
        l = r = std::sin(2.0 * kPi * 700.0 * (double)n / 48000.0) * 0.5;
    };
    const Energies energy = measure(up, 48000.0, 1.0, mono);
    const double share = energy.centre / energy.total();
    std::fprintf(stderr, "  mono source: centre %.1f%%\n", share * 100.0);
    CHECK(share >= 0.80, "a mono source lands in the centre channel");
}

void test_mono_leaves_the_surrounds_silent() {
    // F2-FR5: no side means no ambience to extract, so nothing may be invented.
    Upmixer up;
    up.prepare(48000.0);
    up.setLayout(Upmixer::k71);
    const auto mono = [](long n, double& l, double& r) {
        l = r = std::sin(2.0 * kPi * 440.0 * (double)n / 48000.0) * 0.5;
    };
    const Energies energy = measure(up, 48000.0, 1.0, mono);
    const double db = energy.surround > 0.0
        ? 10.0 * std::log10(energy.surround / energy.total()) : -200.0;
    std::fprintf(stderr, "  mono source: surround %.1f dB\n", db);
    CHECK(db <= -60.0, "a mono source produces no surround");
}

void test_hard_panning_stays_in_front() {
    // F2-FR1: silence opposite is not ambience, and it is not a centre image.
    Upmixer up;
    up.prepare(48000.0);
    up.setLayout(Upmixer::k51);
    const auto leftOnly = [](long n, double& l, double& r) {
        l = std::sin(2.0 * kPi * 1000.0 * (double)n / 48000.0) * 0.5;
        r = 0.0;
    };
    const Energies energy = measure(up, 48000.0, 1.0, leftOnly);
    const double share = energy.front / energy.total();
    std::fprintf(stderr, "  hard-panned source: front %.1f%%\n", share * 100.0);
    CHECK(share >= 0.90, "a hard-panned source stays where it was mixed");
}

void test_uncorrelated_material_goes_behind() {
    // F2-FR3: two independent channels are what a hall sounds like.
    Upmixer up;
    up.prepare(48000.0);
    up.setLayout(Upmixer::k51);
    Noise noise;
    const auto independent = [&noise](long, double& l, double& r) {
        l = noise.next() * 0.4;
        r = noise.next() * 0.4;
    };
    const Energies energy = measure(up, 48000.0, 2.0, independent);
    const double share = energy.surround / energy.total();
    std::fprintf(stderr, "  uncorrelated source: surround %.1f%%, centre %.1f%%\n",
                 share * 100.0, energy.centre / energy.total() * 100.0);
    CHECK(share >= 0.60, "uncorrelated material is steered to the surrounds");
    CHECK(energy.centre / energy.total() < 0.15, "uncorrelated material does not fabricate a centre");
}

void test_seven_one_splits_the_surround() {
    Upmixer up;
    up.prepare(48000.0);
    up.setLayout(Upmixer::k71);
    Noise noise;
    UpmixFrame out;
    double side = 0.0, back = 0.0;
    for (int n = 0; n < 96000; ++n) {
        up.process(noise.next() * 0.4, noise.next() * 0.4, out);
        if (n < 48000) continue;
        side += out.sideL * out.sideL + out.sideR * out.sideR;
        back += out.backL * out.backL + out.backR * out.backR;
    }
    CHECK(back > 0.0 && side > 0.0, "7.1 feeds both the side and the back pair");
    // Both pairs carry the same ambience at a fixed split, so the energy ratio
    // is the square of the share — they differ by where they get rendered.
    const double ratio = back / side;
    const double expected = (0.4 * 0.4) / (0.6 * 0.6);
    CHECK(std::fabs(ratio - expected) < 0.01, "the side/back split matches the declared share");
}

void test_every_sample_rate() {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        Upmixer up;
        up.prepare(fs);
        up.setLayout(Upmixer::k51);
        const auto mono = [fs](long n, double& l, double& r) {
            l = r = std::sin(2.0 * kPi * 700.0 * (double)n / fs) * 0.5;
        };
        const Energies energy = measure(up, fs, 1.0, mono);
        CHECK(energy.centre / energy.total() >= 0.80, "centre extraction is sample-rate independent");

        Upmixer wide;
        wide.prepare(fs);
        wide.setLayout(Upmixer::k71);
        Noise noise;
        UpmixFrame out;
        bool finite = true;
        for (int n = 0; n < 4096; ++n) {
            wide.process(noise.next(), noise.next(), out);
            finite = finite && std::isfinite(out.centre) && std::isfinite(out.frontL)
                  && std::isfinite(out.sideL) && std::isfinite(out.backR);
        }
        CHECK(finite, "output stays finite at every sample rate");
    }
}

void test_steering_controls_move_material_not_level() {
    // Width decides whether the centre image becomes a discrete centre channel
    // or stays the phantom the front pair already makes. This is the check that
    // the control actually moves material: at 0 nothing is extracted.
    const auto mono = [](long n, double& l, double& r) {
        l = r = std::sin(2.0 * kPi * 700.0 * (double)n / 48000.0) * 0.5;
    };
    Upmixer wide;
    wide.prepare(48000.0);
    wide.setLayout(Upmixer::k51);
    const Energies full = measure(wide, 48000.0, 1.0, mono);
    CHECK(full.centre / full.total() > 0.95, "at full width a mono image is the centre channel");

    Upmixer phantom;
    phantom.prepare(48000.0);
    phantom.setLayout(Upmixer::k51);
    phantom.setCentreWidth(0.0);
    const Energies none = measure(phantom, 48000.0, 1.0, mono);
    std::fprintf(stderr, "  centre width 0: centre %.1f%%, front %.1f%%\n",
                 none.centre / none.total() * 100.0, none.front / none.total() * 100.0);
    CHECK(none.centre / none.total() < 0.01, "at zero width nothing is extracted");
    CHECK(none.front / none.total() > 0.99, "...it stays where the front pair can image it");

    // Depth does the same for the surrounds.
    Noise noise;
    const auto independent = [&noise](long, double& l, double& r) {
        l = noise.next() * 0.4;
        r = noise.next() * 0.4;
    };
    Upmixer deep;
    deep.prepare(48000.0);
    deep.setLayout(Upmixer::k51);
    const Energies back = measure(deep, 48000.0, 2.0, independent);
    CHECK(back.surround / back.total() > 0.95, "at full depth ambience goes behind the listener");

    Noise noise2;
    const auto independent2 = [&noise2](long, double& l, double& r) {
        l = noise2.next() * 0.4;
        r = noise2.next() * 0.4;
    };
    Upmixer flat;
    flat.prepare(48000.0);
    flat.setLayout(Upmixer::k51);
    flat.setSurroundDepth(0.0);
    const Energies stay = measure(flat, 48000.0, 2.0, independent2);
    std::fprintf(stderr, "  surround depth 0: surround %.1f%%, front %.1f%%\n",
                 stay.surround / stay.total() * 100.0, stay.front / stay.total() * 100.0);
    CHECK(stay.surround / stay.total() < 0.01, "at zero depth nothing is steered back");

    // Both are steering, so the fold-down stays exact at every setting.
    for (double setting : {0.0, 40.0, 100.0}) {
        Upmixer up;
        up.prepare(48000.0);
        up.setLayout(Upmixer::k71);
        up.setCentreWidth(setting);
        up.setSurroundDepth(100.0 - setting);
        Noise source;
        UpmixFrame out;
        double worst = 0.0;
        for (int n = 0; n < 48000; ++n) {
            const double left = source.next() * 0.4;
            const double right = source.next() * 0.4;
            up.process(left, right, out);
            worst = std::max(worst, std::fabs(out.centre + out.frontL + out.sideL + out.backL - left));
        }
        CHECK(worst < 1.0e-12, "steering never breaks the fold-down");
    }
}

void test_render_path_does_not_allocate() {
    Upmixer up;
    up.prepare(48000.0);
    up.setLayout(Upmixer::k71);
    UpmixFrame out;
    Noise noise;
    g_allocated = 0;
    g_recording = true;
    for (int n = 0; n < 4096; ++n) up.process(noise.next(), noise.next(), out);
    up.setLayout(Upmixer::k51);
    up.setCentreWidth(40.0);
    up.setSurroundDepth(70.0);
    up.reset();
    g_recording = false;
    CHECK(g_allocated == 0, "process(), the setters and reset() allocate nothing");
}

} // namespace

int main() {
    test_off_is_bit_identical();
    test_downmix_folds_back_exactly();
    test_centre_material_goes_to_the_centre();
    test_mono_leaves_the_surrounds_silent();
    test_hard_panning_stays_in_front();
    test_uncorrelated_material_goes_behind();
    test_seven_one_splits_the_surround();
    test_every_sample_rate();
    test_steering_controls_move_material_not_level();
    test_render_path_does_not_allocate();
    if (g_failures == 0) std::fprintf(stderr, "upmix: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
