// SpatialMixerBedRenderer: AUSpatialMixer as the headphone bed renderer.
// Offline, no audio device. Directions are read with the same measurement the
// built-in renderer's baseline uses (core/tests/BinauralMeasure.hpp); the
// expectations are what the P0 M1 spike measured for this unit.
#include "SpatialMixerBedRenderer.hpp"
#include "SpatialMixerDiffuseField.hpp"

#include "BinauralMeasure.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/DSPChain.hpp"
#include "dsp/Upmixer.hpp"

#include <algorithm>
#include <complex>
#include <memory>
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

struct Channel { const char* name; std::size_t index; double azimuth; };
constexpr Channel k51[] = {{"C", 0, 0}, {"L", 1, -30}, {"R", 2, 30}, {"Ls", 3, -110}, {"Rs", 4, 110}};
constexpr Channel k71[] = {{"C", 0, 0}, {"L", 1, -30}, {"R", 2, 30}, {"Ls", 3, -90}, {"Rs", 4, 90}, {"Lb", 5, -135}, {"Rb", 6, 135}};

// Renders `blocks` blocks with `fill(channel, frame)` as input.
template <typename Fill>
Ears renderBlocks(SpatialMixerBedRenderer& renderer, int layout, double yaw, std::size_t blocks, Fill fill) {
    const std::size_t block = renderer.blockFrames();
    std::vector<std::vector<float>> input(7, std::vector<float>(block));
    std::vector<const float*> pointers(7);
    for (std::size_t c = 0; c < 7; ++c) pointers[c] = input[c].data();
    std::vector<float> left(block), right(block);
    Ears ears;
    for (std::size_t b = 0; b < blocks; ++b) {
        for (std::size_t c = 0; c < 7; ++c)
            for (std::size_t i = 0; i < block; ++i) input[c][i] = fill(c, b * block + i);
        renderer.render(layout, pointers.data(), yaw, left.data(), right.data());
        ears.left.insert(ears.left.end(), left.begin(), left.end());
        ears.right.insert(ears.right.end(), right.begin(), right.end());
    }
    return ears;
}

Ears impulse(SpatialMixerBedRenderer& renderer, int layout, std::size_t channel, double yaw, double fs) {
    // Settle on the layout first (the first render of a layout flushes it).
    const std::size_t block = renderer.blockFrames();
    renderBlocks(renderer, layout, yaw, 4 + static_cast<std::size_t>(std::ceil(0.01 * fs / block)), [](std::size_t, std::size_t) { return 0.0f; });
    const std::size_t blocks = static_cast<std::size_t>(std::ceil(0.05 * fs / block));
    return renderBlocks(renderer, layout, yaw, blocks, [&](std::size_t c, std::size_t t) { return c == channel && t == 0 ? 1.0f : 0.0f; });
}

} // namespace

// Up to 96 kHz the units run at the stream rate, untouched. Above it, where the
// unit's interaural delay saturates, they run at 88.2 / 96 kHz, and the
// renderer's block and latency say so.
static void test_every_rate_to_768k_opens() {
    struct Rate { double fs, unit; std::size_t block; };
    constexpr Rate rates[] = {{44100.0, 44100.0, 64},   {48000.0, 48000.0, 64},   {88200.0, 88200.0, 64},
                              {96000.0, 96000.0, 64},   {176400.0, 88200.0, 128}, {192000.0, 96000.0, 128},
                              {352800.0, 88200.0, 256}, {384000.0, 96000.0, 256}, {705600.0, 88200.0, 512},
                              {768000.0, 96000.0, 512}};
    for (const Rate& rate : rates) {
        SpatialMixerBedRenderer renderer;
        std::string error;
        const bool ok = renderer.prepare(rate.fs, error);
        std::printf("  %6.0f Hz: units at %5.0f Hz, block %3zu, latency %3zu frames (%.3f ms)\n", rate.fs, renderer.unitRate(),
                    renderer.blockFrames(), renderer.latencyFrames(),
                    1e3 * static_cast<double>(renderer.blockFrames() + renderer.latencyFrames()) / rate.fs);
        char msg[160];
        std::snprintf(msg, sizeof msg, "opens at %.0f Hz (%s)", rate.fs, error.c_str());
        CHECK(ok && renderer.canRender(Upmixer::k51) && renderer.canRender(Upmixer::k71), msg);
        CHECK(!renderer.canRender(Upmixer::kOff), "nothing to render without an upmix");
        std::snprintf(msg, sizeof msg, "%.0f Hz: units at %.0f Hz, block %zu, a latency only when converting, within the stage's limits",
                      rate.fs, rate.unit, rate.block);
        CHECK(renderer.unitRate() == rate.unit && renderer.blockFrames() == rate.block
              && (renderer.latencyFrames() > 0) == (rate.fs != rate.unit)
              && renderer.blockFrames() <= ExternalBed::kMaxBlockFrames && renderer.latencyFrames() <= ExternalBed::kMaxRendererLatencyFrames, msg);
    }
    SpatialMixerBedRenderer renderer;
    std::string error;
    CHECK(!renderer.prepare(800000.0, error) && !renderer.canRender(Upmixer::k51) && !error.empty(), "refuses a rate above 768 kHz");
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

// Every channel of both layouts lands where it does at 48 kHz, at every rate:
// the same ITD (the cue the unit loses above 96 kHz when run natively, 483 us
// instead of 723 at 90 degrees and 192 kHz), the same level difference, the
// same side.
static void test_every_rate_keeps_the_directions() {
    SpatialMixerBedRenderer reference;
    std::string error;
    CHECK(reference.prepare(48000.0, error), error.c_str());
    for (double fs : {44100.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0, 705600.0, 768000.0}) {
        SpatialMixerBedRenderer renderer;
        CHECK(renderer.prepare(fs, error), error.c_str());
        double worstItd = 0.0, worstIld = 0.0;
        const char* worstName = "";
        bool sides = true;
        for (int layout : {Upmixer::k51, Upmixer::k71}) {
            const auto* channels = layout == Upmixer::k71 ? k71 : k51;
            const std::size_t count = layout == Upmixer::k71 ? 7u : 5u;
            for (std::size_t i = 0; i < count; ++i) {
                const Direction d = measure(impulse(renderer, layout, channels[i].index, 0.0, fs), fs);
                const Direction r = measure(impulse(reference, layout, channels[i].index, 0.0, 48000.0), 48000.0);
                if (std::fabs(d.itdSeconds - r.itdSeconds) > worstItd) {
                    worstItd = std::fabs(d.itdSeconds - r.itdSeconds);
                    worstName = channels[i].name;
                }
                worstIld = std::max(worstIld, std::fabs(d.ildDb - r.ildDb));
                sides = sides && d.singleSource && d.side == r.side;
            }
        }
        std::printf("  %6.0f Hz (units at %5.0f): against 48 kHz, ITD within %.2f us (worst %s), ILD within %.2f dB\n",
                    fs, renderer.unitRate(), worstItd * 1e6, worstName, worstIld);
        char msg[160];
        std::snprintf(msg, sizeof msg, "%.0f Hz: every 5.1 and 7.1 channel keeps its 48 kHz ITD (within 5 us), ILD (within 1 dB) and side", fs);
        CHECK(worstItd <= 5.0e-6 && worstIld <= 1.0 && sides, msg);
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
    for (double fs : {48000.0, 192000.0, 768000.0}) {
        for (double hz : {200.0, 1000.0}) {
            SpatialMixerBedRenderer renderer;
            std::string error;
            CHECK(renderer.prepare(fs, error), error.c_str());
            const std::size_t block = renderer.blockFrames();
            auto sineOnLs = [&](std::size_t c, std::size_t t) {
                return c == 3 ? static_cast<float>(0.3 * std::sin(2.0 * kPi * hz * static_cast<double>(t) / fs)) : 0.0f;
            };
            const std::size_t third = static_cast<std::size_t>(std::ceil(0.3 * fs / block));
            Ears out = renderBlocks(renderer, Upmixer::k51, 0.0, third, sineOnLs);
            // Continue the sine's phase across the calls.
            auto shifted = [&](std::size_t offset) { return [=](std::size_t c, std::size_t t) { return sineOnLs(c, t + offset); }; };
            Ears middle = renderBlocks(renderer, Upmixer::k71, 0.0, third, shifted(third * block));
            Ears last = renderBlocks(renderer, Upmixer::k51, 0.0, third, shifted(2 * third * block));
            out.left.insert(out.left.end(), middle.left.begin(), middle.left.end());
            out.left.insert(out.left.end(), last.left.begin(), last.left.end());
            const std::size_t switch1 = third * block, switch2 = 2 * third * block;
            const std::size_t fade = static_cast<std::size_t>(0.05 * fs);
            double settled = 0.0, during = 0.0;
            for (std::size_t i = block * 8; i < out.left.size(); ++i) {
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

// Attached to the real chain: the latency is the block plus the renderer's
// own lag plus the limiter, the level is held to the dry programme, the output
// stays finite, and it really is the renderer rendering.
static void test_the_chain_carries_the_renderer() {
    for (double fs : {48000.0, 768000.0}) {
        SpatialMixerBedRenderer renderer;
        std::string error;
        CHECK(renderer.prepare(fs, error), error.c_str());
        const std::size_t delay = renderer.blockFrames() + renderer.latencyFrames();
        DSPChain plain, chain;
        plain.prepare(fs, 2);
        chain.attachBedRenderer(&renderer);
        chain.prepare(fs, 2);
        ChainParams params;
        params.spatialMode = 1.0;
        params.surroundType = Upmixer::k71;
        chain.setParams(params);
        chain.reset();
        CHECK(std::fabs(chain.latencySeconds() - plain.latencySeconds() - static_cast<double>(delay) / fs) < 1e-12,
              "the chain reports the renderer's block and lag");

        plain.setParams(params);
        plain.reset();

        // Programme-like: the noise stops at 16 kHz (two fourth-order low-passes),
        // as music does. White noise at 768 kHz is mostly ultrasonic, which the
        // units never render.
        std::mt19937 rng(3);
        std::normal_distribution<double> white(0.0, 0.05);
        Biquad band[4];
        for (std::size_t k = 0; k < 4; ++k) band[k].set(BiquadType::LowPass, fs, 16000.0, 0.0, k % 2 ? 1.3066 : 0.5412);
        const std::size_t frames = static_cast<std::size_t>(fs * 2.0);
        std::vector<float> buffer(frames * 2);
        double dryEnergy = 0.0, wetEnergy = 0.0;
        for (std::size_t i = 0; i < frames; ++i) {
            const double common = white(rng);
            float l = static_cast<float>(0.6 * common + 0.4 * white(rng));
            float r = static_cast<float>(0.6 * common + 0.4 * white(rng));
            l = band[1].processSample(band[0].processSample(l, 0), 0);
            r = band[3].processSample(band[2].processSample(r, 0), 0);
            buffer[2 * i] = l;
            buffer[2 * i + 1] = r;
            if (i >= frames / 2) dryEnergy += buffer[2 * i] * buffer[2 * i] + buffer[2 * i + 1] * buffer[2 * i + 1];
        }
        std::vector<float> builtIn = buffer;
        plain.processInterleaved(builtIn.data(), frames);
        chain.processInterleaved(buffer.data(), frames);

        // The renderer must actually be the one rendering: against the built-in
        // bed shifted by the same delay, the output has to differ substantially.
        double residual = 0.0, reference = 0.0;
        for (std::size_t i = frames / 2; i < frames; ++i) {
            for (std::size_t c = 0; c < 2; ++c) {
                const double d = buffer[2 * i + c] - builtIn[2 * (i - delay) + c];
                residual += d * d;
                reference += builtIn[2 * (i - delay) + c] * builtIn[2 * (i - delay) + c];
            }
        }
        const double residualDb = 10.0 * std::log10(residual / reference);
        bool finite = true;
        for (std::size_t i = 0; i < frames; ++i) {
            finite = finite && std::isfinite(buffer[2 * i]) && std::isfinite(buffer[2 * i + 1]);
            if (i >= frames / 2) wetEnergy += buffer[2 * i] * buffer[2 * i] + buffer[2 * i + 1] * buffer[2 * i + 1];
        }
        const double levelDb = 10.0 * std::log10(wetEnergy / dryEnergy);
        std::printf("  chain 7.1 through AUSpatialMixer at %6.0f Hz: +%zu frames, level %+.2f dB against the dry programme, residual against the built-in bed %+.1f dB\n",
                    fs, delay, levelDb, residualDb);
        CHECK(residualDb > -10.0, "the attached renderer, not the built-in bed, renders the upmix");
        CHECK(finite, "chain output stays finite");
        CHECK(std::fabs(levelDb) < 1.0, "the stage's level match holds the renderer to the dry level (P0-FR6: +-1 dB)");
    }
}

namespace {

// One AUSpatialMixer with a single far-field source at (azimuth, elevation), set up
// the way the renderer sets its units up. Test-only: the renderer places sources
// at the bed layout's angles and nowhere else.
Ears singleSource(double azimuth, double elevation, double fs) {
    AudioComponentDescription description{kAudioUnitType_Mixer, kAudioUnitSubType_SpatialMixer, kAudioUnitManufacturer_Apple, 0, 0};
    AudioUnit au = nullptr;
    AudioComponentInstanceNew(AudioComponentFindNext(nullptr, &description), &au);
    AudioStreamBasicDescription format{};
    format.mSampleRate = fs;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    format.mBitsPerChannel = 32;
    format.mChannelsPerFrame = 1;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = format.mBytesPerPacket = sizeof(float);
    AudioStreamBasicDescription stereo = format;
    stereo.mChannelsPerFrame = 2;
    const UInt32 one = 1, algorithm = kSpatializationAlgorithm_UseOutputType, mode = kSpatialMixerSourceMode_AmbienceBed,
                 output = kSpatialMixerOutputType_Headphones, noReverb = 0, frames = 512;
    AudioChannelLayout layout{};
    layout.mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
    layout.mNumberChannelDescriptions = 1;
    layout.mChannelDescriptions[0].mChannelLabel = kAudioChannelLabel_UseCoordinates;
    layout.mChannelDescriptions[0].mChannelFlags = kAudioChannelFlags_SphericalCoordinates;
    layout.mChannelDescriptions[0].mCoordinates[kAudioChannelCoordinates_Azimuth] = static_cast<Float32>(azimuth);
    layout.mChannelDescriptions[0].mCoordinates[kAudioChannelCoordinates_Elevation] = static_cast<Float32>(elevation);
    layout.mChannelDescriptions[0].mCoordinates[kAudioChannelCoordinates_Distance] = 1.0f;
    struct Feed { std::size_t frame = 0; } feed;
    AURenderCallbackStruct callback{[](void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 n, AudioBufferList* io) -> OSStatus {
        auto* f = static_cast<Feed*>(ref);
        auto* data = static_cast<float*>(io->mBuffers[0].mData);
        for (UInt32 i = 0; i < n; ++i) data[i] = (f->frame + i == 0) ? 1.0f : 0.0f;
        f->frame += n;
        return noErr;
    }, &feed};
    AudioUnitSetProperty(au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &one, sizeof one);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof format);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &stereo, sizeof stereo);
    AudioUnitSetProperty(au, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0, &layout, sizeof layout);
    AudioUnitSetProperty(au, kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input, 0, &algorithm, sizeof algorithm);
    AudioUnitSetProperty(au, kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input, 0, &mode, sizeof mode);
    AudioUnitSetProperty(au, kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global, 0, &output, sizeof output);
    AudioUnitSetProperty(au, kAudioUnitProperty_UsesInternalReverb, kAudioUnitScope_Global, 0, &noReverb, sizeof noReverb);
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &frames, sizeof frames);
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback);
    AudioUnitInitialize(au);
    AudioUnitSetParameter(au, kSpatialMixerParam_ReverbBlend, kAudioUnitScope_Input, 0, 0.0f, 0);
    Ears ears;
    std::vector<float> left(frames), right(frames);
    const std::size_t total = static_cast<std::size_t>(0.25 * fs);
    for (std::size_t done = 0; done < total; done += frames) {
        struct { UInt32 count; AudioBuffer buffers[2]; } list{2, {{1, frames * sizeof(float), left.data()}, {1, frames * sizeof(float), right.data()}}};
        AudioTimeStamp stamp{};
        stamp.mSampleTime = static_cast<Float64>(done);
        stamp.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags = 0;
        AudioUnitRender(au, &flags, &stamp, 0, frames, reinterpret_cast<AudioBufferList*>(&list));
        ears.left.insert(ears.left.end(), left.begin(), left.end());
        ears.right.insert(ears.right.end(), right.begin(), right.end());
    }
    AudioUnitUninitialize(au);
    AudioComponentInstanceDispose(au);
    return ears;
}

// Band level in dB at `centreHz` (1/6 octave wide) of both ears' power.
double sixthOctaveDb(const std::vector<double>& left, const std::vector<double>& right, double centreHz, double fs, std::size_t n) {
    double sum = 0.0;
    int bins = 0;
    for (std::size_t i = 1; i < n / 2; ++i) {
        const double f = static_cast<double>(i) * fs / static_cast<double>(n);
        if (f >= centreHz * std::pow(2.0, -1.0 / 12.0) && f < centreHz * std::pow(2.0, 1.0 / 12.0)) {
            sum += 0.5 * (left[i] + right[i]);
            ++bins;
        }
    }
    return 10.0 * std::log10(bins ? sum / bins : 1e-30);
}

} // namespace

// The table the correction is fitted to was measured on one macOS. Measure the
// unit here, the same way, and fail if it has moved.
static void test_the_diffuse_field_table_still_describes_the_unit() {
    const double fs = 48000.0;
    constexpr std::size_t n = 32768;
    std::vector<double> power(std::size(kSpatialMixerDiffuseField48k), 0.0);
    double weight = 0.0;
    for (double el = -45.0; el <= 90.0; el += 15.0) {
        const double ring = std::cos(el * kPi / 180.0);
        const int count = el >= 90.0 ? 1 : std::max(1, static_cast<int>(std::lround(72.0 * ring)));
        const double w = el >= 90.0 ? (1.0 - std::cos(7.5 * kPi / 180.0)) * 72.0 : ring;
        for (int k = 0; k < count; ++k) {
            const Ears ears = singleSource(-180.0 + 360.0 * k / count, el, fs);
            const auto pl = powerSpectrum(ears.left, n), pr = powerSpectrum(ears.right, n);
            for (std::size_t b = 0; b < power.size(); ++b)
                power[b] += w / count * 72.0 * std::pow(10.0, sixthOctaveDb(pl, pr, kSpatialMixerDiffuseField48k[b].freqHz, fs, n) / 10.0);
            weight += w / count * 72.0;
        }
    }
    double worst = 0.0, worstHz = 0.0;
    for (std::size_t b = 0; b < power.size(); ++b) {
        if (kSpatialMixerDiffuseField48k[b].freqHz > 16200.0) continue;
        const double d = std::fabs(10.0 * std::log10(power[b] / weight) - kSpatialMixerDiffuseField48k[b].db);
        if (d > worst) { worst = d; worstHz = kSpatialMixerDiffuseField48k[b].freqHz; }
    }
    std::printf("  diffuse field measured again (494 directions): worst difference from the table %.3f dB at %.0f Hz\n", worst, worstHz);
    CHECK(worst < 0.5, "the unit's diffuse-field response is still the one the correction was fitted to (50 Hz - 16 kHz, 0.5 dB)");
}

// What the fitted bands leave of the diffuse-field response, at every unit rate.
static void test_the_correction_flattens_the_diffuse_field() {
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0}) {
        SpatialMixerBedRenderer renderer;
        std::string error;
        CHECK(renderer.prepare(fs, error), error.c_str());
        double mean = 0.0;
        int count = 0;
        for (const auto& point : kSpatialMixerDiffuseField48k)
            if (point.freqHz <= 16200.0) { mean += point.db; ++count; }
        mean /= count;
        double before = 0.0, after = 0.0, worstAfter = 0.0;
        for (const auto& point : kSpatialMixerDiffuseField48k) {
            if (point.freqHz > 16200.0) continue;
            double corrected = point.db - mean;
            before += corrected * corrected;
            for (std::size_t b = 0; b < renderer.diffuseFieldBandCount(); ++b)
                corrected += parametric_fit::bandDb(renderer.diffuseFieldBands()[b], point.freqHz, renderer.unitRate());
            after += corrected * corrected;
            worstAfter = std::max(worstAfter, std::fabs(corrected));
        }
        before = std::sqrt(before / count);
        after = std::sqrt(after / count);
        std::printf("  %6.0f Hz: %zu bands take the diffuse field from %.2f to %.2f dB RMS (worst %.2f dB), 50 Hz - 16 kHz\n",
                    fs, renderer.diffuseFieldBandCount(), before, after, worstAfter);
        CHECK(after < 0.6 && after < 0.5 * before, "the correction leaves the diffuse field within 0.6 dB RMS and halves it at least");
    }
}

// The reason the correction exists: the product chain's tone against the dry
// programme (per-ear power, third octaves 250 Hz - 16 kHz, levels matched over
// 250 Hz - 4 kHz), with the correction and without it.
static void test_the_chain_tone_follows_the_dry_programme() {
    const double fs = 48000.0;
    constexpr double centres[] = {250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                  2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000};
    auto measureTone = [&](bool correction) {
        SpatialMixerBedRenderer renderer;
        std::string error;
        CHECK(renderer.prepare(fs, error), error.c_str());
        renderer.setDiffuseFieldCorrection(correction);
        auto chain = std::make_unique<DSPChain>();
        chain->attachBedRenderer(&renderer);
        chain->prepare(fs, 2);
        ChainParams params;
        params.spatialMode = 1.0;
        params.surroundType = Upmixer::k71;
        chain->setParams(params);
        chain->reset();
        std::mt19937 rng(7);
        std::normal_distribution<double> white(0.0, 1.0);
        double pink[3][6] = {};
        auto pinkNext = [&](int which) {   // six one-poles an octave-and-a-half apart, summed
            double sum = 0.0;
            for (int k = 0; k < 6; ++k) {
                const double a = 1.0 - std::exp(-2.0 * kPi * 40.0 * std::pow(2.8, k) / fs);
                pink[which][k] += a * (white(rng) - pink[which][k]);
                sum += pink[which][k] / std::sqrt(a);
            }
            return 0.03 * sum;
        };
        constexpr std::size_t n = 4096;
        std::vector<double> dryPower(std::size(centres), 0.0), wetPower(std::size(centres), 0.0);
        std::vector<float> block(n * 2);
        const std::size_t blocks = static_cast<std::size_t>(6.0 * fs / n);
        for (std::size_t k = 0; k < blocks; ++k) {
            for (std::size_t i = 0; i < n; ++i) {
                const double c = pinkNext(0);
                block[2 * i] = static_cast<float>(std::sqrt(0.6) * c + std::sqrt(0.4) * pinkNext(1));
                block[2 * i + 1] = static_cast<float>(std::sqrt(0.6) * c + std::sqrt(0.4) * pinkNext(2));
            }
            const std::vector<float> dry = block;
            chain->processInterleaved(block.data(), n);
            if (k < 4) continue;
            for (std::size_t ch = 0; ch < 2; ++ch) {
                std::vector<std::complex<double>> a(n), b(n);
                for (std::size_t i = 0; i < n; ++i) {
                    const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
                    a[i] = dry[2 * i + ch] * window;
                    b[i] = block[2 * i + ch] * window;
                }
                fft(a);
                fft(b);
                for (std::size_t band = 0; band < std::size(centres); ++band) {
                    for (std::size_t bin = 1; bin < n / 2; ++bin) {
                        const double f = bin * fs / n;
                        if (f < centres[band] * std::pow(2.0, -1.0 / 6.0) || f >= centres[band] * std::pow(2.0, 1.0 / 6.0)) continue;
                        dryPower[band] += std::norm(a[bin]);
                        wetPower[band] += std::norm(b[bin]);
                    }
                }
            }
        }
        std::vector<double> db(std::size(centres));
        double mean = 0.0;
        int count = 0;
        for (std::size_t band = 0; band < db.size(); ++band) {
            db[band] = 10.0 * std::log10(wetPower[band] / dryPower[band]);
            if (centres[band] <= 4000.0) { mean += db[band]; ++count; }
        }
        double rms = 0.0, worst = 0.0;
        for (double& v : db) { v -= mean / count; rms += v * v; worst = std::max(worst, std::fabs(v)); }
        return std::make_pair(std::sqrt(rms / db.size()), worst);
    };
    const auto corrected = measureTone(true), uncorrected = measureTone(false);
    std::printf("  chain 7.1 tone against the dry programme: %.2f dB RMS (worst %.2f) corrected, %.2f dB RMS (worst %.2f) uncorrected\n",
                corrected.first, corrected.second, uncorrected.first, uncorrected.second);
    CHECK(corrected.first < 1.8, "with the correction the chain's tone stays within 1.8 dB RMS of the dry programme");
    CHECK(corrected.first < uncorrected.first - 0.3 && corrected.second < uncorrected.second - 2.0,
          "and the correction is what gets it there");
}

int main() {
    test_every_rate_to_768k_opens();
    test_every_channel_has_a_direction();
    test_every_rate_keeps_the_directions();
    test_every_channel_follows_the_head();
    test_layout_changes_do_not_click();
    test_the_chain_carries_the_renderer();
    test_the_diffuse_field_table_still_describes_the_unit();
    test_the_correction_flattens_the_diffuse_field();
    test_the_chain_tone_follows_the_dry_programme();
    if (g_failures == 0) std::printf("test_spatial_mixer_bed: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
