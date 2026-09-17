// ChannelProbe: the listening-test burst on one upmix channel (PRD P0-4). What
// the listener is told they hear has to be what reaches the renderer: that one
// channel, at the level asked for, whatever the programme is doing, for exactly
// as long as asked — and nothing at all where no 5.1/7.1 headphone bed renders.
#include "ChannelProbe.hpp"
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

double channelValue(const UpmixFrame& f, int channel) {
    const double values[] = {f.centre, f.frontL, f.frontR, f.sideL, f.sideR, f.backL, f.backR};
    return values[channel];
}

struct Stereo { std::vector<float> left, right; };

Stereo programme(double fs, double seconds, double level, unsigned seed = 5) {
    Stereo s;
    std::mt19937 rng(seed);
    std::normal_distribution<double> white(0.0, level);
    const std::size_t n = static_cast<std::size_t>(fs * seconds);
    for (std::size_t i = 0; i < n; ++i) {
        s.left.push_back(static_cast<float>(white(rng)));
        s.right.push_back(static_cast<float>(white(rng)));
    }
    return s;
}

// Runs the stage over `in`, starting a probe at frame `at`.
Stereo runStage(SurroundStage& stage, const Stereo& in, std::size_t at, int channel, double seconds, double levelDb) {
    Stereo out = in;
    for (std::size_t i = 0; i < in.left.size(); ++i) {
        if (i == at) stage.startProbe(channel, seconds, levelDb);
        float frame[2] = {in.left[i], in.right[i]};
        stage.processFrame(frame, 2);
        out.left[i] = frame[0];
        out.right[i] = frame[1];
    }
    return out;
}

void prepareStage(SurroundStage& stage, double fs, int layout, bool headphone) {
    stage.prepare(fs);
    stage.setHeadphone(headphone);
    stage.setLayout(layout);
    stage.setEnabled(true);
    stage.reset();
}

// Stands in for AUSpatialMixer and keeps what it was handed.
class RecordingRenderer : public BedRenderer {
public:
    std::size_t blockFrames() const override { return 64; }
    bool canRender(int) const override { return true; }
    void render(int, const float* const* ch, double, float* left, float* right) override {
        for (std::size_t i = 0; i < 64; ++i) {
            for (std::size_t c = 0; c < 7; ++c) {
                energy[c] += static_cast<double>(ch[c][i]) * ch[c][i];
            }
            left[i] = right[i] = ch[0][i];
        }
    }
    double energy[7] = {};
};

} // namespace

static void test_the_burst_is_one_channel_at_the_level_asked_for() {
    for (double fs : {44100.0, 48000.0, 192000.0, 768000.0}) {
        ChannelProbe probe;
        probe.prepare(fs);
        for (int channel = 0; channel < ChannelProbe::kChannels; ++channel) {
            const double seconds = channel == 0 ? 8.0 : 0.25;
            probe.start(channel, seconds, -20.0);
            const std::size_t expected = static_cast<std::size_t>(std::llround(seconds * fs));
            std::size_t frames = 0;
            double power = 0.0, others = 0.0, first = -1.0, last = 0.0;
            while (probe.active()) {
                const UpmixFrame f = probe.next();
                for (int c = 0; c < ChannelProbe::kChannels; ++c)
                    if (c != channel) others += std::fabs(channelValue(f, c));
                const double v = channelValue(f, channel);
                if (first < 0.0) first = std::fabs(v);
                last = v;
                power += v * v;
                ++frames;
            }
            CHECK(frames == expected, "the burst lasts exactly as long as asked");
            CHECK(others == 0.0, "every other channel is silent");
            CHECK(first == 0.0 && last == 0.0, "it ramps in from zero and out to zero");
            if (channel == 0) {
                const double db = 10.0 * std::log10(power / static_cast<double>(frames));
                std::printf("  %6.0f Hz: 8 s burst at -20 dBFS measures %.2f dB RMS\n", fs, db);
                CHECK(std::fabs(db + 20.0) < 0.5, "the level is the one asked for, to 0.5 dB, at every rate");
            }
        }
        const UpmixFrame silent = probe.next();
        CHECK(!probe.active() && silent.centre == 0.0, "once finished it stays silent");
    }
}

static void test_bad_requests_stop_it() {
    ChannelProbe probe;
    probe.prepare(48000.0);
    for (int channel : {-1, 7, 100}) {
        probe.start(2, 1.0, -20.0);
        probe.start(channel, 1.0, -20.0);
        CHECK(!probe.active(), "a channel outside 0-6 stops the burst");
    }
    probe.start(2, 0.0, -20.0);
    CHECK(!probe.active(), "a length that is not positive starts nothing");
    probe.start(2, 1.0, std::nan(""));
    CHECK(!probe.active(), "a level that is not finite starts nothing");
    probe.start(3, 100.0, -20.0);
    std::size_t frames = 0;
    while (probe.active()) { probe.next(); ++frames; }
    CHECK(frames == static_cast<std::size_t>(ChannelProbe::kMaxSeconds * 48000.0), "and a long one is held to 10 s");
}

// On the built-in bed: once what was already in the diffuse bus has played out
// (its latest arrival is 32 ms) the programme has no part in the output — the
// level match does not ride the burst — and the centre channel lands in the
// centre.
static void test_the_stage_renders_only_the_burst() {
    const double fs = 48000.0;
    const Stereo loud = programme(fs, 1.5, 0.3), silence = programme(fs, 1.5, 0.0);
    const std::size_t at = static_cast<std::size_t>(0.5 * fs), length = static_cast<std::size_t>(0.5 * fs);
    SurroundStage a, b;
    prepareStage(a, fs, Upmixer::k71, true);
    prepareStage(b, fs, Upmixer::k71, true);
    const Stereo overLoud = runStage(a, loud, at, 0, 0.5, -20.0);
    const Stereo overSilence = runStage(b, silence, at, 0, 0.5, -20.0);
    bool same = true, centred = true;
    double power = 0.0;
    const std::size_t bus = static_cast<std::size_t>(0.040 * fs);
    for (std::size_t i = at; i < at + length; ++i) {
        if (i >= at + bus)
            same = same && overLoud.left[i] == overSilence.left[i] && overLoud.right[i] == overSilence.right[i];
        centred = centred && std::fabs(overSilence.left[i] - overSilence.right[i]) < 1e-6;
        power += static_cast<double>(overSilence.left[i]) * overSilence.left[i];
    }
    const double db = 10.0 * std::log10(power / static_cast<double>(length));
    std::printf("  built-in bed, centre burst at -20 dBFS: %.2f dB at the left ear, the same over loud programme: %s\n",
                db, same ? "yes" : "no");
    CHECK(same, "from 40 ms into the burst the programme does not reach the output at all");
    CHECK(centred, "the centre channel is heard in the centre");
    CHECK(db > -32.0 && db < -8.0, "at a level near the one asked for, over silence");
    CHECK(overLoud.left[at + length + 100] != overSilence.left[at + length + 100], "and afterwards the programme is back");

    // A surround channel goes to its own side of the diffuse bus.
    SurroundStage c;
    prepareStage(c, fs, Upmixer::k71, true);
    const Stereo side = runStage(c, silence, at, 3, 0.5, -20.0);
    double l = 0.0, r = 0.0;
    for (std::size_t i = at; i < at + length + static_cast<std::size_t>(0.05 * fs); ++i) {
        l += static_cast<double>(side.left[i]) * side.left[i];
        r += static_cast<double>(side.right[i]) * side.right[i];
    }
    CHECK(l > 0.0 && r > 0.0 && l != r, "a surround channel is rendered, and not as a centred one");
}

static void test_nothing_is_heard_where_no_upmixed_headphone_bed_renders() {
    const double fs = 48000.0;
    const Stereo in = programme(fs, 1.0, 0.2);
    const std::size_t at = static_cast<std::size_t>(0.2 * fs);
    struct Case { int layout; bool headphone; const char* what; };
    for (const Case& c : {Case{Upmixer::kOff, true, "stereo headphones"}, Case{Upmixer::k71, false, "speakers"}}) {
        SurroundStage probed, plain;
        prepareStage(probed, fs, c.layout, c.headphone);
        prepareStage(plain, fs, c.layout, c.headphone);
        const Stereo a = runStage(probed, in, at, 0, 0.5, -10.0);
        const Stereo b = runStage(plain, in, at, -1, 0.5, -10.0);
        CHECK(a.left == b.left && a.right == b.right, c.what);
    }
    // The time still runs meanwhile: a burst asked for in stereo is over by the
    // time the layout changes.
    SurroundStage stage;
    prepareStage(stage, fs, Upmixer::kOff, true);
    runStage(stage, in, 0, 0, 0.3, -10.0);
    stage.setLayout(Upmixer::k71);
    CHECK(!stage.probing(), "a burst that nobody could hear is not saved for later");
    // 5.1 has no back channels to put a burst in.
    SurroundStage five;
    prepareStage(five, fs, Upmixer::k51, true);
    const Stereo silent = runStage(five, programme(fs, 1.0, 0.0), at, 5, 0.5, -10.0);
    double power = 0.0;
    for (float v : silent.left) power += static_cast<double>(v) * v;
    CHECK(power < 1e-12, "a back channel in 5.1 is silence");
}

// With an external renderer the burst is what the renderer is handed.
static void test_the_external_renderer_is_handed_the_burst() {
    const double fs = 48000.0;
    for (int channel = 0; channel < 7; ++channel) {
        RecordingRenderer renderer;
        SurroundStage stage;
        stage.attachBedRenderer(&renderer);
        prepareStage(stage, fs, Upmixer::k71, true);
        const Stereo silence = programme(fs, 0.6, 0.0);
        runStage(stage, silence, 0, channel, 0.5, -20.0);
        double others = 0.0;
        for (int c = 0; c < 7; ++c) if (c != channel) others += renderer.energy[c];
        const double db = 10.0 * std::log10(renderer.energy[channel] / (0.5 * fs));
        CHECK(others == 0.0 && std::fabs(db + 20.0) < 1.5, "the renderer gets that channel alone, at the level asked for");
    }
}

static void test_the_chain_carries_it_without_allocating() {
    const double fs = 48000.0;
    auto chain = std::make_unique<DSPChain>();
    chain->prepare(fs, 2);
    ChainParams params = ChainParams::flat();
    params.spatialMode = 1.0;
    params.surroundType = Upmixer::k71;
    chain->setParams(params);
    chain->reset();
    std::vector<float> block(2 * 512, 0.0f);
    for (int k = 0; k < 20; ++k) chain->processInterleaved(block.data(), 512);   // settle the crossfade
    g_recording = true;
    g_allocated = 0;
    chain->startChannelProbe(1, 0.25, -20.0);
    double left = 0.0, right = 0.0;
    for (int k = 0; k < 24; ++k) {
        std::fill(block.begin(), block.end(), 0.0f);
        chain->processInterleaved(block.data(), 512);
        for (std::size_t i = 0; i < 512; ++i) {
            left += static_cast<double>(block[2 * i]) * block[2 * i];
            right += static_cast<double>(block[2 * i + 1]) * block[2 * i + 1];
        }
    }
    g_recording = false;
    std::printf("  chain, front-left burst over silence: left %.1f dB, right %.1f dB, %zu bytes allocated\n",
                10.0 * std::log10(left / (24 * 512)), 10.0 * std::log10(right / (24 * 512)), g_allocated);
    CHECK(left > right * 2.0, "the front-left channel is heard to the left through the whole chain");
    CHECK(g_allocated == 0, "starting and rendering a burst allocates nothing");
}

// Without A/B only the current chain renders, so a burst sent to both waits in
// the other one. It must not play out later, when A/B is turned on and swapped.
static void test_an_idle_ab_chain_does_not_keep_a_burst_for_later() {
    const double fs = 48000.0;
    ComparisonSettings settings;
    settings.current = ChainParams::flat();
    settings.current.spatialMode = 1.0;
    settings.current.surroundType = Upmixer::k71;
    settings.reference = settings.current;
    settings.reference.centerWidth = 60.0;   // different, so the swap below is a swap
    auto ab = std::make_unique<ComparisonProcessor>();
    ab->prepare(fs, 2, settings);
    std::vector<float> block(2 * 480, 0.0f);
    auto energy = [&](double seconds) {
        double sum = 0.0;
        for (int k = 0; k < static_cast<int>(seconds * 100); ++k) {
            std::fill(block.begin(), block.end(), 0.0f);
            ab->processInterleaved(block.data(), 480);
            for (float v : block) sum += static_cast<double>(v) * v;
        }
        return sum / (seconds * fs);
    };
    energy(0.3);
    ab->startChannelProbe(1, 3.0, -20.0);
    const double burst = energy(0.3);
    settings.enabled = true;
    ab->setComparison(settings);
    energy(0.2);
    std::swap(settings.current, settings.reference);
    ab->setComparison(settings);
    energy(0.8);   // the swap's crossfade and the room tail of the burst that was playing
    const double after = energy(0.5);
    std::printf("  A/B turned on and swapped mid-burst: %.1f dB against the burst\n", 10.0 * std::log10(after / burst + 1e-30));
    CHECK(burst > 0.0 && after < burst * 1e-4, "the chain that was idle has no burst waiting in it");
}

int main() {
    test_the_burst_is_one_channel_at_the_level_asked_for();
    test_bad_requests_stop_it();
    test_the_stage_renders_only_the_burst();
    test_nothing_is_heard_where_no_upmixed_headphone_bed_renders();
    test_the_external_renderer_is_handed_the_burst();
    test_the_chain_carries_it_without_allocating();
    test_an_idle_ab_chain_does_not_keep_a_burst_for_later();
    if (g_failures == 0) std::printf("test_channel_probe: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
