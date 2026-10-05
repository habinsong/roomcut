/*
 * SpatialMixerDiffuseField.cpp — see SpatialMixerDiffuseField.hpp.
 */
#include "SpatialMixerDiffuseField.hpp"

#include <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include <pthread.h>
#include <sys/sysctl.h>

namespace roomcut {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kBandCount = std::size(kDiffuseFieldBandsHz);

struct Ears { std::vector<double> left, right; bool personalized = false; };

// One impulse from (azimuth, elevation), rendered by a fresh unit set up the way
// the bed renderer sets its own up (SpatialMixerBedRenderer::openUnit), for a
// quarter of a second.
Ears singleSource(double azimuth, double elevation, double fs, bool wantPersonalized) {
    Ears ears;
    AudioComponentDescription description{kAudioUnitType_Mixer, kAudioUnitSubType_SpatialMixer, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    AudioUnit au = nullptr;
    if (component == nullptr || AudioComponentInstanceNew(component, &au) != noErr || au == nullptr) return ears;
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
                 output = kSpatialMixerOutputType_Headphones, noReverb = 0, frames = 512,
                 hrtfMode = wantPersonalized ? kSpatialMixerPersonalizedHRTFMode_On : kSpatialMixerPersonalizedHRTFMode_Off;
    AudioChannelLayout stereoLayout{};
    stereoLayout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
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
    AudioUnitSetProperty(au, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Output, 0, &stereoLayout, sizeof stereoLayout);
    AudioUnitSetProperty(au, kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input, 0, &algorithm, sizeof algorithm);
    AudioUnitSetProperty(au, kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input, 0, &mode, sizeof mode);
    AudioUnitSetProperty(au, kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global, 0, &output, sizeof output);
    AudioUnitSetProperty(au, kAudioUnitProperty_UsesInternalReverb, kAudioUnitScope_Global, 0, &noReverb, sizeof noReverb);
    AudioUnitSetProperty(au, kAudioUnitProperty_SpatialMixerPersonalizedHRTFMode, kAudioUnitScope_Global, 0, &hrtfMode, sizeof hrtfMode);
    AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &frames, sizeof frames);
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback);
    if (AudioUnitInitialize(au) == noErr) {
        UInt32 inUse = 0, size = sizeof inUse;
        ears.personalized = AudioUnitGetProperty(au, kAudioUnitProperty_SpatialMixerAnyInputIsUsingPersonalizedHRTF,
                                                 kAudioUnitScope_Global, 0, &inUse, &size) == noErr && inUse != 0;
        AudioUnitSetParameter(au, kSpatialMixerParam_ReverbBlend, kAudioUnitScope_Input, 0, 0.0f, 0);
        std::vector<float> left(frames), right(frames);
        const std::size_t total = static_cast<std::size_t>(0.25 * fs);
        for (std::size_t done = 0; done < total; done += frames) {
            struct { UInt32 count; AudioBuffer buffers[2]; } list{2, {{1, frames * sizeof(float), left.data()}, {1, frames * sizeof(float), right.data()}}};
            AudioTimeStamp stamp{};
            stamp.mSampleTime = static_cast<Float64>(done);
            stamp.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags flags = 0;
            if (AudioUnitRender(au, &flags, &stamp, 0, frames, reinterpret_cast<AudioBufferList*>(&list)) != noErr) {
                ears.left.clear();
                ears.right.clear();
                break;
            }
            ears.left.insert(ears.left.end(), left.begin(), left.end());
            ears.right.insert(ears.right.end(), right.begin(), right.end());
        }
        AudioUnitUninitialize(au);
    }
    AudioComponentInstanceDispose(au);
    return ears;
}

void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

std::vector<double> powerSpectrum(const std::vector<double>& x, std::size_t n) {
    std::vector<std::complex<double>> a(n);
    for (std::size_t i = 0; i < x.size() && i < n; ++i) a[i] = x[i];
    fft(a);
    std::vector<double> p(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) p[k] = std::norm(a[k]);
    return p;
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

std::string osBuild() {
    char build[64] = {};
    std::size_t size = sizeof(build);
    if (sysctlbyname("kern.osversion", build, &size, nullptr, 0) != 0) return "unknown";
    return build;
}

std::vector<ResponsePoint> load(const std::string& path) {
    std::ifstream input(path);
    std::vector<ResponsePoint> out;
    ResponsePoint point;
    while (input >> point.freqHz >> point.db) {
        if (!std::isfinite(point.freqHz) || !std::isfinite(point.db)) return {};
        out.push_back(point);
    }
    if (out.size() != kBandCount) return {};
    for (std::size_t b = 0; b < kBandCount; ++b)
        if (std::fabs(out[b].freqHz - kDiffuseFieldBandsHz[b]) > 0.05) return {};
    return out;
}

void save(const std::string& path, const std::vector<ResponsePoint>& response) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        for (const auto& point : response) output << point.freqHz << ' ' << point.db << '\n';
        if (!output) return;
    }
    std::rename(temporary.c_str(), path.c_str());
}

} // namespace

std::vector<ResponsePoint> measureSpatialMixerDiffuseField(double fs, bool wantPersonalized, bool* personalized) {
    constexpr std::size_t n = 32768;
    std::vector<double> power(kBandCount, 0.0);
    double weight = 0.0;
    int sources = 0, personal = 0;
    for (double el = -45.0; el <= 90.0; el += 15.0) {
        const double ring = std::cos(el * kPi / 180.0);
        const int count = el >= 90.0 ? 1 : std::max(1, static_cast<int>(std::lround(72.0 * ring)));
        const double w = el >= 90.0 ? (1.0 - std::cos(7.5 * kPi / 180.0)) * 72.0 : ring;
        for (int k = 0; k < count; ++k) {
            const Ears ears = singleSource(-180.0 + 360.0 * k / count, el, fs, wantPersonalized);
            if (ears.left.empty()) return {};
            ++sources;
            personal += ears.personalized ? 1 : 0;
            const auto pl = powerSpectrum(ears.left, n), pr = powerSpectrum(ears.right, n);
            for (std::size_t b = 0; b < kBandCount; ++b)
                power[b] += w / count * 72.0 * std::pow(10.0, sixthOctaveDb(pl, pr, kDiffuseFieldBandsHz[b], fs, n) / 10.0);
            weight += w / count * 72.0;
        }
    }
    // A measurement that switched sets part of the way through describes neither.
    if (personal != 0 && personal != sources) return {};
    if (personalized) *personalized = personal != 0;
    std::vector<ResponsePoint> out(kBandCount);
    for (std::size_t b = 0; b < kBandCount; ++b) {
        out[b] = {kDiffuseFieldBandsHz[b], 10.0 * std::log10(power[b] / weight)};
        if (!std::isfinite(out[b].db)) return {};
    }
    return out;
}

struct SpatialMixerCalibration::Shared {
    using Key = std::pair<long, bool>;   // unit rate, personalized HRTF
    std::mutex mutex;
    std::map<Key, std::vector<ResponsePoint>> known;
    std::map<Key, bool> started;   // also keeps a failed or mismatched measurement from repeating
    std::vector<Measured> finished;
};

SpatialMixerCalibration::SpatialMixerCalibration(std::string directory)
    : directory_(std::move(directory)), build_(osBuild()), shared_(std::make_shared<Shared>()) {}

std::string SpatialMixerCalibration::path(double unitRate, bool personalized) const {
    if (directory_.empty()) return {};
    return directory_ + "/spatial-mixer-diffuse-" + build_ + "-" + std::to_string(std::lround(unitRate))
        + (personalized ? "-personalized" : "-generic") + ".txt";
}

std::vector<ResponsePoint> SpatialMixerCalibration::response(double unitRate, bool personalized) {
    if (!(unitRate > 0.0)) return {};
    const Shared::Key key{std::lround(unitRate), personalized};
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (auto found = shared_->known.find(key); found != shared_->known.end()) return found->second;
    if (auto saved = load(path(unitRate, personalized)); !saved.empty()) return shared_->known[key] = std::move(saved);
    if (shared_->started[key]) return {};
    shared_->started[key] = true;
    // The units may come up with the other set than asked for; the result is kept
    // under the set it was measured with, and this one is not asked again.
    std::thread([shared = shared_, unitRate, personalized, files = std::make_pair(path(unitRate, false), path(unitRate, true))] {
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
        bool measuredPersonalized = personalized;
        auto measured = measureSpatialMixerDiffuseField(unitRate, personalized, &measuredPersonalized);
        const std::string& file = measuredPersonalized ? files.second : files.first;
        if (!measured.empty() && !file.empty()) save(file, measured);
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (measured.empty()) {
            shared->finished.push_back({unitRate, personalized, {}});
            return;
        }
        const Shared::Key measuredKey{std::lround(unitRate), measuredPersonalized};
        shared->known[measuredKey] = measured;
        shared->started[measuredKey] = true;
        shared->finished.push_back({unitRate, measuredPersonalized, std::move(measured)});
    }).detach();
    return {};
}

std::optional<SpatialMixerCalibration::Measured> SpatialMixerCalibration::takeFinished() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->finished.empty()) return std::nullopt;
    Measured out = std::move(shared_->finished.front());
    shared_->finished.erase(shared_->finished.begin());
    return out;
}

} // namespace roomcut
