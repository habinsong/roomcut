#include "VolumeController.hpp"
#include "DeviceVolume.hpp"
#include <algorithm>
#include <cmath>

namespace roomcut {
struct VolumeController::State {
    AudioDeviceID source = kAudioObjectUnknown;
    DeviceVolume hardware;
    std::atomic<float> boost{1}, renderGain{1};
    float lastScalar = -1;
    bool active = true, retryHardware = false;
};

VolumeController::VolumeController() : state_(std::make_shared<State>()),
    queue_(dispatch_queue_create("com.roomcut.engine.volume", DISPATCH_QUEUE_SERIAL)) {}
VolumeController::~VolumeController() { stop(); dispatch_release(queue_); }

void VolumeController::apply(State& state, bool force) {
    if (!state.active) return;
    const float scalar = roomcutMasterGain(state.source, state.lastScalar >= 0 ? state.lastScalar : 1.0f);
    if (!force && !state.retryHardware && scalar == state.lastScalar) return;
    state.lastScalar = scalar;
    const bool hardware = state.hardware.apply(scalar);
    state.retryHardware = !hardware;
    state.renderGain.store((hardware && scalar > 0 ? 1.0f : scalar) * state.boost.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
}

void VolumeController::setSource(AudioDeviceID device) {
    if (stopped_) return;
    const auto state = state_;
    if (source_ != device) {
        volume_.reset(); mute_.reset();
        source_ = device;
        dispatch_sync(queue_, ^{ state->source = device; apply(*state, true); });
    }
    auto changed = ^(UInt32, const AudioObjectPropertyAddress*) {
        if (state->source == device) apply(*state, false);
    };
    if (!volume_.registered()) volume_.observe(device,
        {kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain}, queue_, changed);
    if (!mute_.registered()) mute_.observe(device,
        {kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain}, queue_, changed);
}
void VolumeController::setTarget(AudioDeviceID device) {
    if (stopped_) return;
    const auto state = state_;
    dispatch_sync(queue_, ^{
        state->hardware.setDevice(device);
        apply(*state, true);
    });
}
void VolumeController::setBoost(float boost) {
    if (stopped_) return;
    boost = std::isfinite(boost) ? std::clamp(boost, 1.0f, 2.0f) : 1.0f;
    const auto state = state_;
    dispatch_sync(queue_, ^{ state->boost.store(boost, std::memory_order_relaxed); apply(*state, true); });
}
float VolumeController::boost() const { return state_->boost.load(std::memory_order_relaxed); }
float VolumeController::renderGain() const { return state_->renderGain.load(std::memory_order_relaxed); }
void VolumeController::poll() {
    if (stopped_) return;
    setSource(source_); // Retry failed subscriptions; the read is also the missed-event fallback.
    const auto state = state_;
    dispatch_sync(queue_, ^{ apply(*state, false); });
}
void VolumeController::refresh() {
    if (stopped_) return;
    const auto state = state_;
    dispatch_sync(queue_, ^{ apply(*state, true); });
}
void VolumeController::stop() {
    if (stopped_) return;
    stopped_ = true;
    volume_.reset(); mute_.reset();
    const auto state = state_;
    dispatch_sync(queue_, ^{ state->active = false; });
}
} // namespace roomcut
