// ExternalBed: SurroundStage handing its headphone bed to a block renderer.
// A fake renderer stands in for AUSpatialMixer. It folds the bed back to stereo
// (C + L + Ls + Lb on the left, C + R + Rs + Rb on the right), which by the
// Upmixer's partition property reconstructs the programme — so what the stage
// should output with the renderer in charge is known exactly: the input, one
// block later.
#include "ComparisonProcessor.hpp"
#include "DSPChain.hpp"
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

class FoldingRenderer : public BedRenderer {
public:
    explicit FoldingRenderer(std::size_t block, bool can51 = true, bool can71 = true)
        : block_(block), can51_(can51), can71_(can71) {}
    std::size_t blockFrames() const override { return block_; }
    bool canRender(int layout) const override {
        return (layout == Upmixer::k51 && can51_) || (layout == Upmixer::k71 && can71_);
    }
    void render(int layout, const float* const* ch, double yaw, float* left, float* right) override {
        ++calls;
        lastLayout = layout;
        lastYaw = yaw;
        const bool back = layout == Upmixer::k71;
        for (std::size_t i = 0; i < block_; ++i) {
            left[i] = ch[0][i] + ch[1][i] + ch[3][i] + (back ? ch[5][i] : 0.0f);
            right[i] = ch[0][i] + ch[2][i] + ch[4][i] + (back ? ch[6][i] : 0.0f);
        }
    }
    std::size_t calls = 0;
    int lastLayout = -1;
    double lastYaw = 0.0;

private:
    std::size_t block_;
    bool can51_, can71_;
};

struct Stereo { std::vector<float> left, right; };

Stereo programme(double fs, double seconds, double correlation, unsigned seed = 11) {
    Stereo s;
    std::mt19937 rng(seed);
    std::normal_distribution<double> white(0.0, 0.1);
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    for (std::size_t i = 0; i < n; ++i) {
        const double common = white(rng), a = white(rng), b = white(rng);
        s.left.push_back(static_cast<float>(correlation * common + (1.0 - correlation) * a));
        s.right.push_back(static_cast<float>(correlation * common + (1.0 - correlation) * b));
    }
    return s;
}

Stereo sine(double fs, double seconds, double hz) {
    Stereo s;
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    for (std::size_t i = 0; i < n; ++i) {
        const float v = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979323846 * hz * i / fs));
        s.left.push_back(v);
        s.right.push_back(v);
    }
    return s;
}

struct StageSetup { bool headphone = true; int layout = Upmixer::k51; bool enabled = true; double yaw = 0.0; };

void configure(SurroundStage& stage, double fs, const StageSetup& setup, BedRenderer* renderer) {
    stage.attachBedRenderer(renderer);
    stage.prepare(fs);
    stage.setHeadphone(setup.headphone);
    stage.setLayout(setup.layout);
    stage.setEnabled(setup.enabled);
    stage.setYawDegrees(setup.yaw);
    stage.reset();
}

Stereo run(SurroundStage& stage, const Stereo& in) {
    Stereo out = in;
    for (std::size_t i = 0; i < in.left.size(); ++i) {
        float frame[2] = {in.left[i], in.right[i]};
        stage.processFrame(frame, 2);
        out.left[i] = frame[0];
        out.right[i] = frame[1];
    }
    return out;
}

} // namespace

// Attached but never used (no layout it can render, speakers, or idle): the
// output is the unattached stage's output, one block later, sample for sample.
static void test_an_unused_renderer_is_a_pure_delay() {
    const double fs = 48000.0;
    const std::size_t block = 64;
    const Stereo in = programme(fs, 0.5, 0.6);
    const StageSetup setups[] = {{true, Upmixer::k51, true, 0.0}, {true, Upmixer::k71, true, 25.0},
                                 {false, Upmixer::k51, true, 0.0}, {true, Upmixer::kOff, false, 0.0},
                                 {true, Upmixer::kOff, true, 40.0}};
    for (const StageSetup& setup : setups) {
        FoldingRenderer renderer(block, false, false);
        SurroundStage plain, delayed;
        configure(plain, fs, setup, nullptr);
        configure(delayed, fs, setup, &renderer);
        const Stereo a = run(plain, in), b = run(delayed, in);
        bool identical = true;
        for (std::size_t i = 0; i < block; ++i) identical = identical && b.left[i] == 0.0f && b.right[i] == 0.0f;
        for (std::size_t i = block; i < in.left.size(); ++i)
            identical = identical && b.left[i] == a.left[i - block] && b.right[i] == a.right[i - block];
        char msg[140];
        std::snprintf(msg, sizeof msg, "headphone %d layout %d enabled %d: an unused renderer only delays by %zu frames",
                      setup.headphone, setup.layout, setup.enabled, block);
        CHECK(identical, msg);
        CHECK(renderer.calls == 0, "a renderer that cannot render is never called");
        CHECK(delayed.latencyFrames() == block && plain.latencyFrames() == 0, "latency is the block while attached, 0 otherwise");
    }
}

// With the renderer in charge, the folding renderer puts the programme back
// together: the stage outputs its input one block later.
static void test_the_renderer_takes_over_the_bed() {
    for (double fs : {44100.0, 48000.0, 192000.0}) {
        for (int layout : {Upmixer::k51, Upmixer::k71}) {
            for (std::size_t block : {std::size_t{32}, std::size_t{64}, std::size_t{256}}) {
                FoldingRenderer renderer(block);
                SurroundStage stage;
                configure(stage, fs, {true, layout, true, 0.0}, &renderer);
                const Stereo in = programme(fs, 0.6, 0.6);
                const Stereo out = run(stage, in);
                double worst = 0.0;
                const std::size_t settled = static_cast<std::size_t>(fs * 0.3);
                for (std::size_t i = settled; i < in.left.size(); ++i) {
                    worst = std::max(worst, static_cast<double>(std::fabs(out.left[i] - in.left[i - block])));
                    worst = std::max(worst, static_cast<double>(std::fabs(out.right[i] - in.right[i - block])));
                }
                char msg[140];
                std::snprintf(msg, sizeof msg, "fs %.0f layout %d block %zu: output is the input %zu frames later (worst %.2e)",
                              fs, layout, block, block, worst);
                CHECK(worst < 1.0e-4, msg);
                CHECK(renderer.calls == in.left.size() / block, "one render call per block, not per frame");
                CHECK(renderer.lastLayout == layout, "the renderer is told the layout");
                CHECK(stage.externalBedGain() == 1.0, "the external bed is fully in charge");
            }
        }
    }
}

static void test_head_yaw_reaches_the_renderer() {
    FoldingRenderer renderer(64);
    SurroundStage stage;
    configure(stage, 48000.0, {true, Upmixer::k71, true, 37.0}, &renderer);
    run(stage, programme(48000.0, 0.05, 0.6));
    CHECK(renderer.lastYaw == 37.0, "the stage's head yaw goes to the renderer");
}

// Moving between the external and the built-in render (5.1 external, 7.1 not
// supported by this renderer) fades rather than steps. Same bound as the
// stage's own layout-switch test: four times the dry sine's slope.
static void test_handing_over_is_click_free() {
    const double fs = 48000.0;
    for (double hz : {200.0, 1000.0}) {
        FoldingRenderer renderer(64, true, false);
        SurroundStage stage;
        configure(stage, fs, {true, Upmixer::k51, true, 0.0}, &renderer);
        const Stereo in = sine(fs, 1.0, hz);
        double drySlope = 0.0;
        for (std::size_t i = 1; i < in.left.size(); ++i) drySlope = std::max(drySlope, static_cast<double>(std::fabs(in.left[i] - in.left[i - 1])));
        double worstStep = 0.0;
        float previous = 0.0f;
        for (std::size_t i = 0; i < in.left.size(); ++i) {
            if (i == static_cast<std::size_t>(fs * 0.3)) stage.setLayout(Upmixer::k71);   // external -> built-in
            if (i == static_cast<std::size_t>(fs * 0.6)) stage.setLayout(Upmixer::k51);   // built-in -> external
            float frame[2] = {in.left[i], in.right[i]};
            stage.processFrame(frame, 2);
            if (i > static_cast<std::size_t>(fs * 0.1)) worstStep = std::max(worstStep, static_cast<double>(std::fabs(frame[0] - previous)));
            previous = frame[0];
        }
        char msg[120];
        std::snprintf(msg, sizeof msg, "%.0f Hz: handing the bed over and back does not click (step %.4f, dry slope %.4f)", hz, worstStep, drySlope);
        CHECK(worstStep < drySlope * 4.0, msg);
    }
}

static void test_a_block_too_large_is_not_attached() {
    const double fs = 48000.0;
    FoldingRenderer renderer(ExternalBed::kMaxBlockFrames + 1);
    SurroundStage plain, stage;
    configure(plain, fs, {true, Upmixer::k51, true, 0.0}, nullptr);
    configure(stage, fs, {true, Upmixer::k51, true, 0.0}, &renderer);
    const Stereo in = programme(fs, 0.2, 0.6);
    const Stereo a = run(plain, in), b = run(stage, in);
    CHECK(stage.latencyFrames() == 0, "an oversized block is refused");
    CHECK(a.left == b.left && a.right == b.right && renderer.calls == 0, "and the stage renders as if nothing were attached");
}

static void test_latency_is_reported_by_the_chain() {
    const double fs = 48000.0;
    FoldingRenderer a(64), b(64);
    DSPChain plain, chain;
    plain.prepare(fs, 2);
    chain.attachBedRenderer(&a);
    chain.prepare(fs, 2);
    const double extra = chain.latencySeconds() - plain.latencySeconds();
    CHECK(std::fabs(extra - 64.0 / fs) < 1e-12, "DSPChain reports the block as latency");
    ComparisonProcessor comparison;
    comparison.attachBedRenderers(&a, &b);
    comparison.prepare(fs, 2);
    CHECK(std::fabs(comparison.latencySeconds() - chain.latencySeconds()) < 1e-12, "and so does the A/B processor");
}

// The A/B processor drives both chains a frame at a time; the renderer must
// still see whole blocks, and neither chain may shift in time.
static void test_the_ab_frame_path_still_renders_blocks() {
    const double fs = 48000.0;
    const std::size_t block = 64;
    FoldingRenderer current(block), reference(block);
    ComparisonProcessor comparison;
    comparison.attachBedRenderers(&current, &reference);
    ComparisonSettings settings;
    settings.current.spatialMode = 1.0;
    settings.current.surroundType = Upmixer::k51;
    settings.reference = settings.current;
    settings.reference.preampDb = -3.0;
    settings.enabled = true;
    comparison.prepare(fs, 2, settings);
    const Stereo in = programme(fs, 0.5, 0.6);
    std::vector<float> buffer(in.left.size() * 2);
    for (std::size_t i = 0; i < in.left.size(); ++i) { buffer[2 * i] = in.left[i]; buffer[2 * i + 1] = in.right[i]; }
    comparison.processInterleaved(buffer.data(), in.left.size());
    bool finite = true;
    for (float v : buffer) finite = finite && std::isfinite(v);
    CHECK(finite, "A/B output stays finite");
    CHECK(current.calls == in.left.size() / block && reference.calls == in.left.size() / block,
          "both chains render in blocks although A/B drives them a frame at a time");
}

static void test_the_render_path_does_not_allocate() {
    const double fs = 48000.0;
    FoldingRenderer renderer(64, true, false);
    SurroundStage stage;
    configure(stage, fs, {true, Upmixer::k51, true, 0.0}, &renderer);
    const Stereo in = programme(fs, 0.3, 0.6);
    g_allocated = 0;
    g_recording = true;
    for (std::size_t i = 0; i < in.left.size(); ++i) {
        if (i == 5000) stage.setLayout(Upmixer::k71);
        if (i == 9000) stage.setLayout(Upmixer::k51);
        if (i == 11000) stage.setYawDegrees(-60.0);
        float frame[2] = {in.left[i], in.right[i]};
        stage.processFrame(frame, 2);
    }
    g_recording = false;
    CHECK(g_allocated == 0, "rendering through an external bed allocates nothing");
}

int main() {
    test_an_unused_renderer_is_a_pure_delay();
    test_the_renderer_takes_over_the_bed();
    test_head_yaw_reaches_the_renderer();
    test_handing_over_is_click_free();
    test_a_block_too_large_is_not_attached();
    test_latency_is_reported_by_the_chain();
    test_the_ab_frame_path_still_renders_blocks();
    test_the_render_path_does_not_allocate();
    if (g_failures == 0) std::printf("test_external_bed: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
