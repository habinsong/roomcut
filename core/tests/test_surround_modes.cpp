// Every Space > Surround choice has to be something a listener can hear, on the
// kind of programme they actually play, without being louder or duller than
// Off. Written against the reported failure: Off, Ambience, 5.1 and 7.1 on
// headphones and Off, Ambience and Wide on speakers "all sounded the same".
// Measured then (six programmes): Ambience moved the interaural correlation by
// 0.01-0.02 with no reverberant tail at all, 5.1 and 7.1 were within 0.06 of
// each other with the same tail, and on a single speaker Ambience and Wide only
// changed the level.
//
// These run the chain with its built-in bed (no AUSpatialMixer in core); the
// same choices through AUSpatialMixer are covered in the engine's tests.
#include "BinauralMeasure.hpp"
#include "DSPChain.hpp"
#include "KWeightedLevel.hpp"
#include "ProgramSignals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace roomcut;

static int g_failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", std::string(msg).c_str(), __FILE__, __LINE__); ++g_failures; } } while (0)

namespace {

constexpr double kFs = 48000.0;
using Stereo = programs::Stereo;

struct Choice { const char* name; bool headphone; double mode, surround; };

constexpr Choice kHeadphoneOff{"headphone Off", true, 1, 0};
constexpr Choice kHeadphoneAmbience{"headphone Ambience", true, 2, 0};
constexpr Choice kHeadphone51{"headphone 5.1", true, 1, 2};
constexpr Choice kHeadphone71{"headphone 7.1", true, 1, 3};
constexpr Choice kSpeakerOff{"speaker Off", false, 0, 0};
constexpr Choice kSpeakerAmbience{"speaker Ambience", false, 3, 0};
constexpr Choice kSpeakerWide{"speaker Wide", false, 0, 2};

Stereo render(const Stereo& in, const Choice& choice) {
    DSPChain chain;
    chain.prepare(kFs, 2);
    ChainParams p = ChainParams::flat();
    p.spatialMode = choice.mode;
    p.surroundType = choice.surround;
    chain.setParams(p);
    chain.reset();
    Stereo out{in.left, in.right};
    std::vector<float> block(1024);
    for (std::size_t i = 0; i < in.left.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.left.size() - i);
        for (std::size_t k = 0; k < n; ++k) { block[2 * k] = in.left[i + k]; block[2 * k + 1] = in.right[i + k]; }
        chain.processInterleaved(block.data(), n);
        for (std::size_t k = 0; k < n; ++k) { out.left[i + k] = block[2 * k]; out.right[i + k] = block[2 * k + 1]; }
    }
    return out;
}

Stereo mono(const Stereo& s) {
    Stereo m = s;
    for (std::size_t i = 0; i < s.left.size(); ++i) m.left[i] = m.right[i] = 0.5f * (s.left[i] + s.right[i]);
    return m;
}

// K-weighted level over 3 s windows, after the first second.
double loudnessDb(const Stereo& s) {
    KWeightedLevel meter;
    meter.prepare(kFs, 2, 30);
    double sum = 0.0;
    int count = 0;
    for (std::size_t i = 0; i < s.left.size(); ++i) {
        const float frame[2] = {s.left[i], s.right[i]};
        if (meter.processFrame(frame) && i > kFs) { sum += meter.power(); ++count; }
    }
    return 10.0 * std::log10(sum / count);
}

// Worst third-octave level change against a reference, both outputs summed,
// the overall level change removed (250 Hz - 10 kHz).
double colourationDb(const Stereo& x, const Stereo& reference) {
    const std::size_t from = static_cast<std::size_t>(kFs), len = x.left.size() - from;
    auto bands = [&](const Stereo& s) {
        binaural::Ears e;
        e.left.assign(s.left.begin() + from, s.left.begin() + from + len);
        e.right.assign(s.right.begin() + from, s.right.begin() + from + len);
        return binaural::thirdOctaves(e, kFs);
    };
    const auto a = bands(x), b = bands(reference);
    std::vector<double> d;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].centreHz > 10000.0) break;
        d.push_back(10.0 * std::log10((a[i].left + a[i].right) / (b[i].left + b[i].right)));
    }
    double mean = 0.0;
    for (double v : d) mean += v;
    mean /= static_cast<double>(d.size());
    double worst = 0.0;
    for (double v : d) worst = std::max(worst, std::fabs(v - mean));
    return worst;
}

// 150 ms stereo noise bursts once a second; the energy 30-400 ms after each
// ends, against the burst (dB). No tail at all reads -99.
Stereo bursts() {
    const std::size_t n = static_cast<std::size_t>(kFs * 8);
    Stereo s{std::vector<float>(n), std::vector<float>(n)};
    uint32_t state = 7;
    auto noise = [&] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return (state & 0xffff) / 65535.0 - 0.5; };
    for (std::size_t i = 0; i < n; ++i) {
        const bool on = std::fmod(static_cast<double>(i) / kFs, 1.0) < 0.15;
        const double c = noise(), a = noise(), b = noise();
        s.left[i] = on ? static_cast<float>(0.4 * (0.7 * c + 0.7 * a)) : 0.0f;
        s.right[i] = on ? static_cast<float>(0.4 * (0.7 * c + 0.7 * b)) : 0.0f;
    }
    return s;
}

double tailDb(const Stereo& x) {
    double on = 0.0, after = 0.0;
    for (int second = 2; second < 7; ++second) {
        const std::size_t b0 = static_cast<std::size_t>(second * kFs), b1 = b0 + static_cast<std::size_t>(0.15 * kFs);
        for (std::size_t i = b0; i < b1; ++i) on += static_cast<double>(x.left[i]) * x.left[i] + static_cast<double>(x.right[i]) * x.right[i];
        for (std::size_t i = b1 + static_cast<std::size_t>(0.03 * kFs); i < b1 + static_cast<std::size_t>(0.4 * kFs); ++i)
            after += static_cast<double>(x.left[i]) * x.left[i] + static_cast<double>(x.right[i]) * x.right[i];
    }
    return after > 1e-20 ? 10.0 * std::log10(after / on) : -99.0;
}

// Where a source panned hard left is heard: lateral angle from the interaural delay.
double hardLeftDegrees(const Choice& choice) {
    const std::size_t n = static_cast<std::size_t>(kFs * 3);
    Stereo s{std::vector<float>(n), std::vector<float>(n)};
    uint32_t state = 11;
    for (std::size_t i = 0; i < n; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        s.left[i] = std::fmod(static_cast<double>(i) / kFs, 1.0) < 0.3 ? static_cast<float>(0.3 * ((state & 0xffff) / 65535.0 - 0.5)) : 0.0f;
    }
    const Stereo out = render(s, choice);
    binaural::Ears e;
    const std::size_t from = static_cast<std::size_t>(2.05 * kFs), len = static_cast<std::size_t>(0.2 * kFs);
    e.left.assign(out.left.begin() + from, out.left.begin() + from + len);
    e.right.assign(out.right.begin() + from, out.right.begin() + from + len);
    const binaural::Direction d = binaural::measure(e, kFs);
    // A right ear that hears nothing at all is a source at the left ear.
    return d.ildDb > 60.0 ? 90.0 : d.lateralDegrees;
}

}  // namespace

static void test_every_choice_is_as_loud_as_off_and_not_dull() {
    struct Programme { const char* name; Stereo audio; };
    const Programme programmes[] = {
        {"pop", programs::pop(kFs, 6)}, {"ballad", programs::ballad(kFs, 6)},
        {"electronic", programs::electronic(kFs, 6)}, {"podcast", programs::podcast(kFs, 6)},
    };
    for (const Programme& p : programmes) {
        const Stereo headphoneOff = render(p.audio, kHeadphoneOff), speakerOff = render(p.audio, kSpeakerOff);
        for (const Choice& c : {kHeadphoneAmbience, kHeadphone51, kHeadphone71, kSpeakerAmbience, kSpeakerWide}) {
            const Stereo out = render(p.audio, c);
            const Stereo& off = c.headphone ? headphoneOff : speakerOff;
            const double loud = loudnessDb(out) - loudnessDb(off);
            const double colour = colourationDb(out, off);
            std::printf("  %-10s %-18s level %+5.2f dB, colouration %.1f dB", p.name, c.name, loud, colour);
            char msg[160];
            std::snprintf(msg, sizeof msg, "%s %s: level %+.2f dB against Off", p.name, c.name, loud);
            CHECK(std::fabs(loud) <= 1.0, msg);
            // The virtual speakers colour by design (their direction filters),
            // and so does speaker Wide's difference-signal canceller at its
            // output: it lifts the side so the acoustic crosstalk takes it back
            // (at the ears, through AUSpatialMixer's HRTF for speakers at +-15
            // and +-30 degrees, measured at most 3.2 dB). The reverb-only
            // choices must not colour.
            const double limit = c.surround >= 2 ? (c.headphone ? 5.0 : 4.0) : 3.5;
            std::snprintf(msg, sizeof msg, "%s %s: colouration %.1f dB against Off", p.name, c.name, colour);
            CHECK(colour <= limit, msg);
            if (!c.headphone) {
                const Stereo m = mono(out), mo = mono(off);
                const double monoLoud = loudnessDb(m) - loudnessDb(mo);
                std::printf(", mono sum %+5.2f dB", monoLoud);
                std::snprintf(msg, sizeof msg, "%s %s: a single speaker hears %+.2f dB", p.name, c.name, monoLoud);
                CHECK(std::fabs(monoLoud) <= 1.0, msg);
            }
            std::printf("\n");
        }
    }
}

static void test_the_choices_differ_where_a_listener_hears_it() {
    const Stereo burst = bursts();
    const double hpOff = tailDb(render(burst, kHeadphoneOff)), hpAmbience = tailDb(render(burst, kHeadphoneAmbience));
    const double hp51 = tailDb(render(burst, kHeadphone51)), hp71 = tailDb(render(burst, kHeadphone71));
    const double spAmbience = tailDb(mono(render(burst, kSpeakerAmbience))), spWide = tailDb(mono(render(burst, kSpeakerWide)));
    const double spOff = tailDb(mono(render(burst, kSpeakerOff)));
    std::printf("  tail after a burst: headphones Off %.1f, Ambience %.1f, 5.1 %.1f, 7.1 %.1f dB; speakers (mono sum) Off %.1f, Ambience %.1f, Wide %.1f dB\n",
                hpOff, hpAmbience, hp51, hp71, spOff, spAmbience, spWide);
    CHECK(hpOff < -60.0 && spOff < -60.0, "Off adds no space");
    CHECK(hpAmbience > -25.0, "headphone Ambience puts an audible space around the mix");
    CHECK(hp51 > -25.0, "headphone 5.1 stands its speakers in a room");
    CHECK(hp71 > hp51 + 4.0, "headphone 7.1 is a clearly larger space than 5.1");
    CHECK(spAmbience > -25.0, "speaker Ambience is audible on a single speaker");
    CHECK(spWide > spAmbience + 3.0, "speaker Wide is a larger space than Ambience, audible on a single speaker too");

    const double offAngle = hardLeftDegrees(kHeadphoneOff), ambienceAngle = hardLeftDegrees(kHeadphoneAmbience);
    const double angle51 = hardLeftDegrees(kHeadphone51), angle71 = hardLeftDegrees(kHeadphone71);
    std::printf("  a source panned hard left is heard at: Off %.0f, Ambience %.0f, 5.1 %.0f, 7.1 %.0f degrees\n",
                offAngle, ambienceAngle, angle51, angle71);
    CHECK(offAngle > 80.0 && ambienceAngle > 70.0, "Off and Ambience leave the mix's own stereo in place");
    CHECK(angle51 < 40.0 && angle71 < 40.0, "5.1 and 7.1 put the front pair on speakers in front of the listener");
}

int main() {
    test_every_choice_is_as_loud_as_off_and_not_dull();
    test_the_choices_differ_where_a_listener_hears_it();
    if (g_failures == 0) std::printf("test_surround_modes: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
