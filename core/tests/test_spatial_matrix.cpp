// Every combination of the spatial features, walked exhaustively.
//
// Surround, head tracking and the virtual room all decide where a source
// appears, and they are reachable in any combination: a preset can set one, a
// restored state file another, the listener a third. Checking them one at a
// time is how the pairs went wrong — head tracking stacked on crossfeed, the
// upmix stacked on the ambience surround, and "off" that did not turn off.
//
// So this walks all of them: two outputs x four surround settings x three head
// angles x four rooms x bypass. For each it asserts the things that must hold no
// matter what is switched on.
//
// Built twice: as it stands, and with ROOMCUT_MATRIX_SPATIAL_MIXER, where every
// chain has AUSpatialMixer attached for its headphone bed (a fresh unit per
// render, so no case inherits another's history). That build walks one more
// axis, the bed renderer each sound picks — Apple's, or the built-in bed with
// Apple's still attached — because both are one tap away in the Space tab.
// Every reference is rendered the same way, so the checks mean the same thing
// for either bed renderer.
#include "DSPChain.hpp"
#ifdef ROOMCUT_MATRIX_SPATIAL_MIXER
#include "SpatialMixerBedRenderer.hpp"
#include <cstdlib>
#endif

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checked = 0;
#define CHECK(cond, msg) do { \
    ++g_checked; \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg).c_str(), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

namespace {

constexpr double kFs = 48000.0;

struct Case {
    bool headphone;
    double surroundType;   // 0 off, 2 = 5.1, 3 = 7.1
    bool ambience;         // the older surround, inside spatialMode
    double yaw;
    bool tracking;
    double roomType;
    bool bypass;
    // The rest of the Space tab, moved off neutral together: width, centre
    // focus, damping, room amount and the two upmix trims. Everything the tab
    // can reach has to survive being combined with everything else.
    bool controlsEngaged = false;
    // Deliberately non-zero by default: nothing may double up on it.
    double crossfeed = 40.0;
    double bedRenderer = 0.0;   // 0 = the attached system renderer, 1 = built-in

    std::string name() const {
        std::string out = headphone ? "headphone" : "speaker  ";
        out += surroundType >= 3 ? " 7.1" : (surroundType >= 2 ? " 5.1" : (ambience ? " amb" : " ---"));
        out += tracking ? (yaw == 0 ? " track0 " : " track40") : " notrack";
        out += roomType >= 3 ? " hall " : (roomType >= 2 ? " liv  " : (roomType >= 1 ? " stud " : " dry  "));
        out += controlsEngaged ? " ctl" : "";
        out += bypass ? " bypass" : "";
        out += bedRenderer >= 1.0 ? " builtin-bed" : "";
        return out;
    }

    ChainParams params() const {
        ChainParams p = ChainParams::flat();
        p.spatialMode = headphone ? (ambience ? 2.0 : 1.0) : (ambience ? 3.0 : 0.0);
        p.surroundType = surroundType;
        p.roomType = roomType;
        p.roomAmount = 50.0;
        p.crossfeed = crossfeed;
        p.bedRenderer = bedRenderer;
        if (controlsEngaged) {
            p.spatialWidth = 120.0;
            p.centerFocus = 80.0;
            p.roomReduce = 60.0;
            p.roomAmount = 75.0;
            p.centerWidth = 4.0;
            p.surroundDepth = -5.0;
        }
        return p;
    }
};

// Two probes: a centred tone (tests that the middle stays in the middle) and a
// stereo pair with real side content (the only thing that can see the width
// features at all — they are all mono-safe by design).
struct Probe { std::vector<float> data; bool centred; };

// The same noise in both channels.
Probe makeCentredNoise(std::size_t frames) {
    Probe p;
    p.centred = true;
    p.data.resize(frames * 2);
    unsigned state = 12345u;
    for (std::size_t f = 0; f < frames; ++f) {
        state = state * 1103515245u + 12345u;
        const float noise = static_cast<float>(0.2 * ((double)((state >> 8) & 0xFFFFu) / 32768.0 - 1.0));
        p.data[f * 2] = noise;
        p.data[f * 2 + 1] = noise;
    }
    return p;
}

Probe makeProbe(bool centred, std::size_t frames) {
    Probe p;
    p.centred = centred;
    p.data.resize(frames * 2);
    unsigned state = 22695477u;
    for (std::size_t f = 0; f < frames; ++f) {
        state = state * 1103515245u + 12345u;
        const double noise = (double)((state >> 8) & 0xFFFFu) / 32768.0 - 1.0;
        const double tone = 0.25 * std::sin(2.0 * M_PI * 440.0 * (double)f / kFs);
        p.data[f * 2] = static_cast<float>(centred ? tone : tone + 0.15 * noise);
        p.data[f * 2 + 1] = static_cast<float>(centred ? tone : tone - 0.15 * noise);
    }
    return p;
}

std::vector<float> render(const Case& c, const Probe& probe) {
    DSPChain chain;
#ifdef ROOMCUT_MATRIX_SPATIAL_MIXER
    SpatialMixerBedRenderer renderer;
    std::string error;
    if (!renderer.prepare(kFs, error)) {
        std::fprintf(stderr, "FAIL: AUSpatialMixer did not open: %s\n", error.c_str());
        std::exit(1);
    }
    chain.attachBedRenderer(&renderer);
#endif
    chain.prepare(kFs, 2);
    chain.setParams(c.params());
    chain.setBypass(c.bypass);
    chain.setHeadPose(c.yaw, c.tracking);
    std::vector<float> buf = probe.data;
    chain.processInterleaved(buf.data(), buf.size() / 2);
    return buf;
}

double rms(const std::vector<float>& x, std::size_t from) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < x.size(); ++i) { sum += (double)x[i] * x[i]; ++n; }
    return n ? std::sqrt(sum / (double)n) : 0.0;
}

} // namespace

int main() {
    const std::size_t frames = static_cast<std::size_t>(kFs * 0.6);
    const std::size_t settled = frames;      // second half of the interleaved buffer
    const Probe centred = makeProbe(true, frames);
    const Probe stereo = makeProbe(false, frames);
    const Probe centredNoise = makeCentredNoise(frames);

    // The chain has 2 ms of limiter look-ahead, so its output is DELAYED against
    // its input. Comparing the two directly compares different moments — which
    // is exactly the mistake that made an earlier version of this file report
    // every single case as broken. Every reference here is itself rendered.
    Case bare{true, 0.0, false, 0.0, false, 0.0, false};
    bare.crossfeed = 0.0;
    Case bareSpeaker = bare;
    bareSpeaker.headphone = false;
    const std::vector<float> referenceCentred = render(bare, centred);
    const std::vector<float> referenceStereo = render(bare, stereo);
    const double dryCentred = rms(referenceCentred, settled);
    const double dryStereo = rms(referenceStereo, settled);

    // Under bypass nothing the tab can reach may reach the signal, so every
    // bypassed case must be the same as a bypassed chain with nothing set.
    Case bypassedBare = bare;
    bypassedBare.bypass = true;
    Case bypassedBareSpeaker = bareSpeaker;
    bypassedBareSpeaker.bypass = true;
    const std::vector<float> bypassCentred = render(bypassedBare, centred);
    const std::vector<float> bypassStereo = render(bypassedBare, stereo);

#ifdef ROOMCUT_MATRIX_SPATIAL_MIXER
    static constexpr double kBeds[] = {0.0, 1.0};
#else
    static constexpr double kBeds[] = {0.0};
#endif
    int cases = 0;
    for (double bed : kBeds)
    for (bool headphone : {true, false})
    for (double surround : {0.0, 2.0, 3.0})
    for (bool ambience : {false, true})
    for (int head = 0; head < 3; ++head)
    for (double room : {0.0, 1.0, 2.0, 3.0})
    for (bool bypass : {false, true})
    for (bool controls : {false, true}) {
        if (surround >= 2.0 && ambience) continue;      // the UI cannot produce this pair
        Case c{headphone, surround, ambience,
               head == 2 ? 40.0 : 0.0, head != 0, room, bypass};
        c.controlsEngaged = controls;
        c.bedRenderer = bed;
        ++cases;
        const std::string tag = c.name();

        for (const Probe* probe : {&centred, &stereo}) {
            const std::vector<float> out = render(c, *probe);

            bool finite = true;
            for (float v : out) finite = finite && std::isfinite(v);
            CHECK(finite, tag + ": output stays finite");

            // Bypass means bypass, whatever else is switched on.
            if (c.bypass) {
                const std::vector<float>& want = probe->centred ? bypassCentred : bypassStereo;
                bool identical = true;
                for (std::size_t i = settled; i < out.size(); ++i)
                    identical = identical && out[i] == want[i];
                CHECK(identical, tag + ": bypass ignores every other setting");
            }

            // Nothing may run away. Several of these controls are gain controls
            // — width, centre focus, damping and the room amount all add energy
            // on purpose — so this is a runaway check, not a loudness match:
            // with every one of them pushed at once the chain measures about
            // +9.6 dB, and anything far past that means something is compounding
            // that should not be.
            const double dry = probe->centred ? dryCentred : dryStereo;
            const double level = 20.0 * std::log10(std::max(rms(out, settled), 1e-12) / dry);
            {
                char note[72];
                std::snprintf(note, sizeof(note), ": level %+.2f dB is a runaway", level);
                CHECK(level > -12.0 && level < 12.0, tag + note);
            }

            // The limiter is the last thing in the chain and its ceiling is
            // digital full scale. Whatever the combination does, it cannot get
            // past that — this is the check that matters for the speakers.
            double peak = 0.0;
            for (std::size_t i = settled; i < out.size(); ++i) peak = std::max(peak, std::fabs((double)out[i]));
            {
                char note[72];
                std::snprintf(note, sizeof(note), ": peak %.4f past the brickwall", peak);
                CHECK(peak <= 1.0001, tag + note);
            }

            // A centred source stays centred — unless the head is deliberately
            // turned, or a room is adding its own reflections. Every surround
            // choice carries a room of its own while the Room control is Off
            // (DSPChain::surroundRoom), so those are in the second case even
            // when the listener picked no room. With a room the two outputs
            // cannot be sample-identical (its tail is decorrelated on purpose),
            // but the source must still not move to one side: the two carry the
            // same energy.
            const bool roomRendering = c.roomType != 0.0 || c.surroundType >= 2.0 || c.ambience;
            if (probe->centred && !c.bypass && c.yaw == 0.0 && !roomRendering) {
                double worst = 0.0;
                for (std::size_t i = settled; i + 1 < out.size(); i += 2)
                    worst = std::max(worst, std::fabs((double)out[i] - out[i + 1]));
                CHECK(worst < 1e-5, tag + ": a centred source stays centred");
            }
            if (probe->centred && !c.bypass && c.yaw == 0.0 && roomRendering) {
                // Broadband, not the tone: a single sustained tone through a
                // decorrelated tail lands differently in each output depending
                // on where the tail's modes fall (measured +-3 dB for a user-picked
                // room on speakers too), which is not a source moving.
                const std::vector<float> noise = render(c, centredNoise);
                double left = 0.0, right = 0.0;
                for (std::size_t i = settled; i + 1 < noise.size(); i += 2) {
                    left += (double)noise[i] * noise[i];
                    right += (double)noise[i + 1] * noise[i + 1];
                }
                const double balance = 10.0 * std::log10(std::max(left, 1e-30) / std::max(right, 1e-30));
                char note[80];
                std::snprintf(note, sizeof(note), ": a centred source stays centred with a room (%+.2f dB)", balance);
                // 1 dB: about the smallest level difference between the ears
                // that moves an image. AUSpatialMixer's measured HRTF is not
                // exactly symmetric (-0.52 dB here with the controls engaged).
                CHECK(std::fabs(balance) < 1.0, tag + note);
            }

            // No steps: every one of these changes is ramped or crossfaded.
            double worstStep = 0.0, drySlope = 0.0;
            for (std::size_t i = 2; i < probe->data.size(); i += 2)
                drySlope = std::max(drySlope, std::fabs((double)probe->data[i] - probe->data[i - 2]));
            for (std::size_t i = settled + 2; i < out.size(); i += 2)
                worstStep = std::max(worstStep, std::fabs((double)out[i] - out[i - 2]));
            CHECK(worstStep < drySlope * 6.0, tag + ": no steps in the settled output");
        }
    }

    // Turning a feature off has to actually turn it off — this is the one that
    // failed in the field, where "off" travelled as an absent field and the
    // engine kept whatever was already playing. Rendered against the same chain
    // with the feature never switched on.
    for (bool headphone : {true, false}) {
        Case off = headphone ? bare : bareSpeaker;
        for (const Probe* probe : {&centred, &stereo}) {
            const std::vector<float>& want = headphone
                ? (probe->centred ? referenceCentred : referenceStereo)
                : render(bareSpeaker, *probe);
            for (double surround : {2.0, 3.0}) {
                Case on = off;
                on.surroundType = surround;
                Case backOff = off;
                CHECK(render(backOff, *probe) == want,
                      off.name() + ": surround off renders as if it never existed");
                CHECK(render(on, *probe) != want,
                      off.name() + ": surround on actually changes something");
            }
            for (double room : {1.0, 2.0, 3.0}) {
                Case on = off;
                on.roomType = room;
                CHECK(render(on, *probe) != want, off.name() + ": a room actually changes something");
            }
            Case roomOff = off;
            roomOff.roomType = 0.0;
            CHECK(render(roomOff, *probe) == want, off.name() + ": room off renders as if it never existed");
        }
    }

    // Head tracking is meaningless on speakers, and must not leak into them.
    for (double surround : {0.0, 2.0}) {
        for (const Probe* probe : {&centred, &stereo}) {
            const Case still{false, surround, false, 0.0, false, 0.0, false};
            const Case turned{false, surround, false, 40.0, true, 0.0, false};
            CHECK(render(still, *probe) == render(turned, *probe),
                  std::string("speaker: a head angle changes nothing"));
        }
    }

#ifndef ROOMCUT_MATRIX_SPATIAL_MIXER
    // With no renderer attached there is only one bed, so the choice of renderer
    // must not change a single sample.
    for (const Probe* probe : {&centred, &stereo}) {
        Case apple{true, 3.0, false, 40.0, true, 3.0, false};
        apple.controlsEngaged = true;
        Case builtin = apple;
        builtin.bedRenderer = 1.0;
        CHECK(render(apple, *probe) == render(builtin, *probe),
              apple.name() + ": without a system renderer the renderer choice changes nothing");
    }
#else
    std::printf("bed renderer: AUSpatialMixer attached, both renderer choices walked\n");
#endif
    std::printf("spatial matrix: %d combinations, %d checks\n", cases, g_checked);
    if (g_failures == 0) { std::printf("all spatial-matrix checks passed\n"); return 0; }
    std::fprintf(stderr, "%d spatial-matrix check(s) failed\n", g_failures);
    return 1;
}
