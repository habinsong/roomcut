// SpatialMixerBedRenderer: AUSpatialMixer as the headphone bed renderer.
// Offline, no audio device. Directions are read with the same measurement the
// built-in renderer's baseline uses (core/tests/BinauralMeasure.hpp); the
// expectations are what the P0 M1 spike measured for this unit.
#include "SpatialMixerBedRenderer.hpp"

#include "BinauralMeasure.hpp"
#include "dsp/DSPChain.hpp"
#include "dsp/Upmixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;
using namespace roomcut::binaural;

namespace {

constexpr std::size_t kBlock = SpatialMixerBedRenderer::kBlockFrames;

struct Channel { const char* name; std::size_t index; double azimuth; };
constexpr Channel k51[] = {{"C", 0, 0}, {"L", 1, -30}, {"R", 2, 30}, {"Ls", 3, -110}, {"Rs", 4, 110}};
constexpr Channel k71[] = {{"C", 0, 0}, {"L", 1, -30}, {"R", 2, 30}, {"Ls", 3, -90}, {"Rs", 4, 90}, {"Lb", 5, -135}, {"Rb", 6, 135}};

// Renders `blocks` blocks with `fill(channel, frame)` as input.
template <typename Fill>
Ears renderBlocks(SpatialMixerBedRenderer& renderer, int layout, double yaw, std::size_t blocks, Fill fill) {
    std::vector<std::vector<float>> input(7, std::vector<float>(kBlock));
    std::vector<const float*> pointers(7);
    for (std::size_t c = 0; c < 7; ++c) pointers[c] = input[c].data();
    float left[kBlock], right[kBlock];
    Ears ears;
    for (std::size_t b = 0; b < blocks; ++b) {
        for (std::size_t c = 0; c < 7; ++c)
            for (std::size_t i = 0; i < kBlock; ++i) input[c][i] = fill(c, b * kBlock + i);
        renderer.render(layout, pointers.data(), yaw, left, right);
        ears.left.insert(ears.left.end(), left, left + kBlock);
        ears.right.insert(ears.right.end(), right, right + kBlock);
    }
    return ears;
}

Ears impulse(SpatialMixerBedRenderer& renderer, int layout, std::size_t channel, double yaw, double fs) {
    // Settle on the layout first (the first render of a layout flushes it).
    renderBlocks(renderer, layout, yaw, 4, [](std::size_t, std::size_t) { return 0.0f; });
    const std::size_t blocks = static_cast<std::size_t>(std::ceil(0.05 * fs / kBlock));
    return renderBlocks(renderer, layout, yaw, blocks, [&](std::size_t c, std::size_t t) { return c == channel && t == 0 ? 1.0f : 0.0f; });
}

} // namespace

static void test_rates_above_192k_are_refused() {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        SpatialMixerBedRenderer renderer;
        std::string error;
        const bool ok = renderer.prepare(fs, error);
        char msg[160];
        std::snprintf(msg, sizeof msg, "opens at %.0f Hz (%s)", fs, error.c_str());
        CHECK(ok && renderer.canRender(Upmixer::k51) && renderer.canRender(Upmixer::k71), msg);
        CHECK(!renderer.canRender(Upmixer::kOff), "nothing to render without an upmix");
    }
    for (double fs : {352800.0, 384000.0, 768000.0}) {
        SpatialMixerBedRenderer renderer;
        std::string error;
        const bool ok = renderer.prepare(fs, error);
        char msg[160];
        std::snprintf(msg, sizeof msg, "refuses %.0f Hz, where the unit's ITD collapses", fs);
        CHECK(!ok && !renderer.canRender(Upmixer::k51) && !renderer.canRender(Upmixer::k71) && !error.empty(), msg);
    }
}

// Every channel, surrounds included, is a single-source direction near its
// lateral angle — the opposite of the built-in renderer's diffuse surrounds.
static void test_every_channel_has_a_direction() {
    const double fs = 48000.0;
    SpatialMixerBedRenderer renderer;
    std::string error;
    CHECK(renderer.prepare(fs, error), error.c_str());
    for (int layout : {Upmixer::k51, Upmixer::k71}) {
        const auto* channels = layout == Upmixer::k71 ? k71 : k51;
        const std::size_t count = layout == Upmixer::k71 ? 7u : 5u;
        for (std::size_t i = 0; i < count; ++i) {
            const Channel& ch = channels[i];
            const Direction d = measure(impulse(renderer, layout, ch.index, 0.0, fs), fs);
            const double expectedSide = ch.azimuth > 0 ? 1.0 : (ch.azimuth < 0 ? -1.0 : 0.0);
            std::printf("  %s %-2s az %+5.0f: ITD %+7.1f us, lateral %5.1f (want %5.1f), corr %+.3f/%+.3f, ILD %+6.2f dB\n",
                        layout == Upmixer::k71 ? "7.1" : "5.1", ch.name, ch.azimuth, d.itdSeconds * 1e6, d.lateralDegrees,
                        lateralOf(ch.azimuth), d.peak, d.trough, d.ildDb);
            char msg[140];
            std::snprintf(msg, sizeof msg, "%s %s is a single source within 10 deg of its lateral angle, on its side",
                          layout == Upmixer::k71 ? "7.1" : "5.1", ch.name);
            CHECK(d.singleSource && std::fabs(d.lateralDegrees - lateralOf(ch.azimuth)) <= 10.0
                  && (expectedSide == 0.0 || d.side == expectedSide), msg);
        }
    }
}

// Turning the head right moves the centre to the left, and every channel's
// ears change — the surrounds follow the head too.
static void test_every_channel_follows_the_head() {
    const double fs = 48000.0;
    SpatialMixerBedRenderer renderer;
    std::string error;
    CHECK(renderer.prepare(fs, error), error.c_str());
    const Direction right = measure(impulse(renderer, Upmixer::k71, 0, 40.0, fs), fs);
    const Direction left = measure(impulse(renderer, Upmixer::k71, 0, -40.0, fs), fs);
    std::printf("  head +40: centre lateral %.1f side %+.0f | head -40: lateral %.1f side %+.0f\n",
                right.lateralDegrees, right.side, left.lateralDegrees, left.side);
    CHECK(right.side == -1.0 && std::fabs(right.lateralDegrees - 40.0) <= 10.0, "head turned right: the centre is 40 deg to the left");
    CHECK(left.side == 1.0 && std::fabs(left.lateralDegrees - 40.0) <= 10.0, "head turned left: the centre is 40 deg to the right");
    for (const Channel& ch : k71) {
        const Ears ahead = impulse(renderer, Upmixer::k71, ch.index, 0.0, fs);
        const Ears turned = impulse(renderer, Upmixer::k71, ch.index, 40.0, fs);
        char msg[100];
        std::snprintf(msg, sizeof msg, "%s changes with the head", ch.name);
        CHECK(ahead.left != turned.left || ahead.right != turned.right, msg);
    }
}

// 5.1 -> 7.1 -> 5.1 on a surround sine (its angle differs between the two
// units). The same bound as the direction harness: no step beyond 1.25x the
// settled slope.
static void test_layout_changes_do_not_click() {
    for (double fs : {48000.0, 192000.0}) {
        for (double hz : {200.0, 1000.0}) {
            SpatialMixerBedRenderer renderer;
            std::string error;
            CHECK(renderer.prepare(fs, error), error.c_str());
            auto sineOnLs = [&](std::size_t c, std::size_t t) {
                return c == 3 ? static_cast<float>(0.3 * std::sin(2.0 * kPi * hz * static_cast<double>(t) / fs)) : 0.0f;
            };
            const std::size_t third = static_cast<std::size_t>(std::ceil(0.3 * fs / kBlock));
            Ears out = renderBlocks(renderer, Upmixer::k51, 0.0, third, sineOnLs);
            // Continue the sine's phase across the calls.
            auto shifted = [&](std::size_t offset) { return [=](std::size_t c, std::size_t t) { return sineOnLs(c, t + offset); }; };
            Ears middle = renderBlocks(renderer, Upmixer::k71, 0.0, third, shifted(third * kBlock));
            Ears last = renderBlocks(renderer, Upmixer::k51, 0.0, third, shifted(2 * third * kBlock));
            out.left.insert(out.left.end(), middle.left.begin(), middle.left.end());
            out.left.insert(out.left.end(), last.left.begin(), last.left.end());
            const std::size_t switch1 = third * kBlock, switch2 = 2 * third * kBlock;
            const std::size_t fade = static_cast<std::size_t>(0.05 * fs);
            double settled = 0.0, during = 0.0;
            for (std::size_t i = kBlock * 8; i < out.left.size(); ++i) {
                const double step = std::fabs(out.left[i] - out.left[i - 1]);
                const bool nearSwitch = (i >= switch1 && i < switch1 + fade) || (i >= switch2 && i < switch2 + fade);
                (nearSwitch ? during : settled) = std::max(nearSwitch ? during : settled, step);
            }
            std::printf("  layout switch at %6.0f Hz, %4.0f Hz sine: step %.5f vs settled %.5f (x%.3f)\n", fs, hz, during, settled, during / settled);
            char msg[100];
            std::snprintf(msg, sizeof msg, "switching units does not click (%.0f Hz sine at %.0f Hz)", hz, fs);
            CHECK(during <= settled * 1.25, msg);
        }
    }
}

// Attached to the real chain: the latency is the block plus the limiter, the
// level is held to the dry programme, the output stays finite.
static void test_the_chain_carries_the_renderer() {
    const double fs = 48000.0;
    SpatialMixerBedRenderer renderer;
    std::string error;
    CHECK(renderer.prepare(fs, error), error.c_str());
    DSPChain plain, chain;
    plain.prepare(fs, 2);
    chain.attachBedRenderer(&renderer);
    chain.prepare(fs, 2);
    ChainParams params;
    params.spatialMode = 1.0;
    params.surroundType = Upmixer::k71;
    chain.setParams(params);
    chain.reset();
    CHECK(std::fabs(chain.latencySeconds() - plain.latencySeconds() - static_cast<double>(kBlock) / fs) < 1e-12,
          "the chain reports the renderer's block");

    std::mt19937 rng(3);
    std::normal_distribution<double> white(0.0, 0.05);
    const std::size_t frames = static_cast<std::size_t>(fs * 2.0);
    std::vector<float> buffer(frames * 2);
    double dryEnergy = 0.0, wetEnergy = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double common = white(rng);
        buffer[2 * i] = static_cast<float>(0.6 * common + 0.4 * white(rng));
        buffer[2 * i + 1] = static_cast<float>(0.6 * common + 0.4 * white(rng));
        if (i >= frames / 2) dryEnergy += buffer[2 * i] * buffer[2 * i] + buffer[2 * i + 1] * buffer[2 * i + 1];
    }
    chain.processInterleaved(buffer.data(), frames);
    bool finite = true;
    for (std::size_t i = 0; i < frames; ++i) {
        finite = finite && std::isfinite(buffer[2 * i]) && std::isfinite(buffer[2 * i + 1]);
        if (i >= frames / 2) wetEnergy += buffer[2 * i] * buffer[2 * i] + buffer[2 * i + 1] * buffer[2 * i + 1];
    }
    const double levelDb = 10.0 * std::log10(wetEnergy / dryEnergy);
    std::printf("  chain 7.1 through AUSpatialMixer: level %+.2f dB against the dry programme\n", levelDb);
    CHECK(finite, "chain output stays finite");
    CHECK(std::fabs(levelDb) < 1.0, "the stage's level match holds the renderer to the dry level (P0-FR6: +-1 dB)");
}

int main() {
    test_rates_above_192k_are_refused();
    test_every_channel_has_a_direction();
    test_every_channel_follows_the_head();
    test_layout_changes_do_not_click();
    test_the_chain_carries_the_renderer();
    if (g_failures == 0) std::printf("test_spatial_mixer_bed: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
