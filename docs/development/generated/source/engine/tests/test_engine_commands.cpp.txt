#include "Control.hpp"
#include "ControlValidation.hpp"
#include "EngineReplies.hpp"
#include "EngineSoundState.hpp"
#include "EngineStateStore.hpp"
#include "ParameterCodec.hpp"
#include "RealtimeParams.hpp"
#include "SoundCommands.hpp"
#include "dsp/Analyzer.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

using namespace roomcut;
static std::atomic<int> failures{0};
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

class Service {
public:
    RealtimeParams published;
    std::atomic<unsigned> saveFailures{0};
    mach_port_t port = MACH_PORT_NULL;
    explicit Service(const std::filesystem::path& path) : store_(path.string()) {
        state_.realOutputUID = "saved-device"; state_.preferredOutputUID = "pinned-device";
        state_.keepRoomcutDefault = true; state_.volumeBoost = 1.5;
        CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) == KERN_SUCCESS, "allocate test service");
        CHECK(mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS, "create service send right");
        worker_ = std::thread([this] { run(); });
    }
    ~Service() {
        stopped_ = true; worker_.join();
        mach_port_deallocate(mach_task_self(), port);
        mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_RECEIVE, -1);
    }
    bool send(RoomcutControlMsgBuffer& request, mach_msg_size_t size, uint32_t id, uint32_t* status = nullptr) {
        mach_port_t replyPort = MACH_PORT_NULL;
        if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &replyPort) != KERN_SUCCESS) return false;
        request.raw.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
        request.raw.header.msgh_remote_port = port;
        request.raw.header.msgh_local_port = replyPort;
        request.raw.header.msgh_id = id;
        request.reply.msgType = id;
        auto result = mach_msg(&request.raw.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, size, 0,
                               MACH_PORT_NULL, 1000, MACH_PORT_NULL);
        RoomcutControlMsgBuffer response{};
        if (result == KERN_SUCCESS) result = mach_msg(&response.raw.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
            0, sizeof(response), replyPort, 1000, MACH_PORT_NULL);
        const bool valid = result == KERN_SUCCESS && normalizeControlReply(response, id);
        if (valid && status) *status = response.reply.status;
        if (result == KERN_SUCCESS) mach_msg_destroy(&response.raw.header);
        mach_port_mod_refs(mach_task_self(), replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        return valid;
    }
private:
    EngineSoundState sound_{ChainParams::flat(), "flat"};
    PersistentState state_;
    EngineStateStore store_;
    std::atomic<bool> stopped_{false};
    std::thread worker_;
    void run() {
        while (!stopped_) {
            RoomcutControlMsgBuffer request{};
            const auto received = mach_msg(&request.raw.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                0, sizeof(request), port, 50, MACH_PORT_NULL);
            if (received == MACH_RCV_TIMED_OUT) continue;
            CHECK(received == KERN_SUCCESS, "receive real Mach command");
            if (received != KERN_SUCCESS) return;
            if (!normalizeControlRequest(request)) { mach_msg_destroy(&request.raw.header); continue; }
            kern_return_t result = KERN_SUCCESS;
            switch (request.raw.header.msgh_id) {
            case ROOMCUT_MSG_SET_PRESET: case ROOMCUT_MSG_SET_PARAMS: case ROOMCUT_MSG_SET_COMPARISON: {
                const auto status = applySoundCommand(request, sound_);
                if (status == 0) {
                    published.publish(sound_.settings());
                    capturePersistentSound(state_, sound_.parameters(), sound_.presetID());
                    if (!store_.save(state_)) ++saveFailures;
                }
                result = controlReplyAck(request.raw.header, request.raw.header.msgh_id, status);
                break;
            }
            case ROOMCUT_MSG_GET_PARAMS:
                result = controlReplyParams(request.getParams, makeParamsReply(sound_)); break;
            case ROOMCUT_MSG_GET_COMPARISON:
                result = controlReplyComparison(request.getComparison, makeComparisonReply(sound_, {}, true, false)); break;
            case ROOMCUT_MSG_STATE: {
                EngineStatusSnapshot snapshot;
                snapshot.lifecycle = ROOMCUT_ENGINE_STREAMING;
                snapshot.outputRunning = true; snapshot.outputDeviceUID = "test-device";
                snapshot.keepDefault = state_.keepRoomcutDefault; snapshot.volumeBoost = state_.volumeBoost;
                result = controlReplyState(request.stateRequest, makeStateReply(sound_, snapshot));
                break;
            }
            default: mach_msg_destroy(&request.raw.header); break;
            }
            CHECK(result == KERN_SUCCESS, "reply using existing Mach transport");
        }
    }
};

static void soundRoundTrip(const std::filesystem::path& directory) {
    const auto path = directory / "sound.state";
    Service service(path);
    EngineStateStore store(path.string());
    RoomcutGetParamsReply params{};
    CHECK(controlGetParams(service.port, 1000, &params) == KERN_SUCCESS && std::strcmp(params.presetId, "flat") == 0,
          "initial parameters use the unchanged wire format");
    const auto initialRevision = params.paramsRevision;
    auto current = ChainParams::flat();
    current.preampDb = -3.25; current.eqGainsDb[4] = 5.5; current.spatialWidth = -25;
    current.parametric[1] = {true, 2, 8000, 2.5, 0.75};
    RoomcutControlMsgBuffer request{};
    encodeParameters(current, request.setParams);
    uint32_t status = 99;
    CHECK(service.send(request, sizeof(RoomcutSetParamsRequest), ROOMCUT_MSG_SET_PARAMS, &status) && status == 0,
          "live parameter command is accepted");
    CHECK(controlGetParams(service.port, 1000, &params) == KERN_SUCCESS && decodeParameters(params) == current &&
          params.paramsRevision == initialRevision + 1, "accepted command updates all parameters and revision once");
    ComparisonSettings delivered;
    CHECK(service.published.readLatest(delivered) && delivered.current == current && !delivered.enabled,
          "render publication contains the complete accepted state");
    const auto saved = store.load();
    ChainParams restored;
    CHECK(parseParamsLine(saved.paramsLine, &restored) && parseParametricLine(saved.parametricLine, &restored) && restored == current,
          "accepted sound survives real state-store round trip");
    CHECK(saved.realOutputUID == "saved-device" && saved.preferredOutputUID == "pinned-device" &&
          saved.keepRoomcutDefault && saved.volumeBoost == 1.5, "sound persistence preserves unrelated routing state");

    RoomcutComparisonRequest comparison{};
    auto reference = current; reference.preampDb = -9;
    current.eqGainsDb[2] = -4;
    encodeParameters(current, comparison.current); encodeParameters(reference, comparison.reference);
    comparison.enabled = 1;
    CHECK(controlSetComparison(service.port, comparison, 1000, &status) == KERN_SUCCESS && status == 0, "comparison command is accepted atomically");
    RoomcutComparisonReply compared{};
    CHECK(controlGetComparison(service.port, 1000, &compared) == KERN_SUCCESS && compared.enabled &&
          decodeParameters(compared.current) == current && decodeParameters(compared.reference) == reference &&
          compared.state == static_cast<uint32_t>(LevelMatchState::Measuring), "comparison reply preserves both states and pending render revision");
    const auto revision = compared.revision;
    comparison.kind = ROOMCUT_COMPARISON_PRESET;
    std::snprintf(comparison.presetId, sizeof(comparison.presetId), "%s", "missing-preset");
    comparison.reference.preampDb = -12;
    CHECK(controlSetComparison(service.port, comparison, 1000, &status) == KERN_SUCCESS && status == 1, "unknown comparison preset is rejected");
    CHECK(controlGetComparison(service.port, 1000, &compared) == KERN_SUCCESS && compared.revision == revision &&
          decodeParameters(compared.reference) == reference, "rejected preset changes neither reference nor revision");
    CHECK(controlSetPreset(service.port, "flat", 1000, &status) == KERN_SUCCESS && status == 0, "builtin preset still applies");
    CHECK(store.load().presetId == "flat" && store.load().paramsLine.empty(), "builtin persistence removes an obsolete custom line");

    request = {};
    request.setParams.preampDb = -4.5;
    request.setParams.limiterReleaseMs = 100;
    CHECK(service.send(request, offsetof(RoomcutSetParamsRequest, spatialWidth), ROOMCUT_MSG_SET_PARAMS, &status), "legacy parameter payload remains accepted");
    CHECK(controlGetParams(service.port, 1000, &params) == KERN_SUCCESS && params.preampDb == -4.5 && params.spatialWidth == 0,
          "legacy omitted fields do not consume the Mach trailer");
    const auto lastRevision = params.paramsRevision;
    request = {};
    std::memset(request.setPreset.presetId, 'x', sizeof(request.setPreset.presetId));
    CHECK(!service.send(request, sizeof(RoomcutSetPresetRequest), ROOMCUT_MSG_SET_PRESET), "unterminated preset is rejected before command application");
    CHECK(controlGetParams(service.port, 1000, &params) == KERN_SUCCESS && params.paramsRevision == lastRevision,
          "malformed command preserves live state");
    RoomcutStateReply state{};
    CHECK(controlGetState(service.port, 1000, &state) == KERN_SUCCESS && state.state == ROOMCUT_STATE_RUNNING &&
          state.paramsRevision == lastRevision && state.keepDefault && state.volumeBoost == 1.5 &&
          std::strcmp(state.outputDeviceUID, "test-device") == 0, "state response shares the accepted revision and routing snapshot");
}

static void replyProjection() {
    EngineSoundState sound(ChainParams::flat(), "flat");
    RoomcutControlMsgBuffer unsupported{};
    unsupported.raw.header.msgh_id = ROOMCUT_MSG_STATE;
    CHECK(applySoundCommand(unsupported, sound) == 1 && sound.revision() == 0,
          "non-sound commands cannot mutate the sound model");
    EngineStatusSnapshot runtime;
    runtime.lifecycle = ROOMCUT_ENGINE_STREAMING;
    runtime.manualBypass = true; runtime.outputDeviceUID = "stale-device";
    runtime.framesRendered = (1ull << 40) + 17; runtime.ringUnderruns = 29;
    runtime.limiterReductionDb = 2.5f; runtime.renderPeak = 0.375f;
    const auto state = makeStateReply(sound, runtime);
    CHECK(state.state == ROOMCUT_STATE_BYPASS && state.manualBypass && !state.outputDeviceUID[0], "stopped output omits stale UID and running bypass is projected");
    CHECK(state.framesRendered == runtime.framesRendered && state.ringUnderruns == 29 &&
          state.limiterGainReductionDb == 2.5f && state.renderPeak == 0.375f, "meter and 64-bit counters retain their wire values");
    CHECK(presentedEngineState(ROOMCUT_ENGINE_RECOVERING, true, true) == ROOMCUT_STATE_RECOVER,
          "bypass does not turn a recovering engine into a running one");
    sound.applyComparison(ChainParams::flat(), ChainParams::flat(), true);
    ComparisonMetrics meters{LevelMatchState::Matched, 3, 1, sound.settings().revision};
    CHECK(makeComparisonReply(sound, meters, true, false).state == static_cast<uint32_t>(LevelMatchState::Matched), "matching render revision is ready");
    --meters.revision;
    CHECK(makeComparisonReply(sound, meters, true, false).state == static_cast<uint32_t>(LevelMatchState::Measuring), "old render result remains pending");
    CHECK(makeComparisonReply(sound, meters, false, false).state == static_cast<uint32_t>(LevelMatchState::NoSignal), "stopped output reports no signal");
    CHECK(makeComparisonReply(sound, meters, false, true).state == static_cast<uint32_t>(LevelMatchState::Bypassed), "manual bypass keeps its established precedence");

    AnalysisSnapshot analysis;
    analysis.valid = true; analysis.sampleRate = 96000; analysis.channels = 2; analysis.framesAnalyzed = 8192;
    analysis.peakDb = -2; analysis.rmsDb = -14; analysis.crestFactor = 12;
    analysis.lowEnergy = 0.1f; analysis.lowMidEnergy = 0.2f; analysis.midEnergy = 0.3f; analysis.highEnergy = 0.4f;
    analysis.spectralCentroid = 3210; analysis.stereoWidth = 0.7f; analysis.midSideRatio = 0.8f; analysis.correlation = -0.4f;
    analysis.muddiness = 11; analysis.harshness = 22; analysis.sibilance = 33; analysis.voicePresence = 44;
    analysis.reverbEstimate = 55; analysis.dynamicRange = 66;
    for (unsigned i = 0; i < analysis.spectrum.size(); ++i) analysis.spectrum[i] = static_cast<float>(i) - 100.0f;
    const auto result = makeAnalysisReply(analysis);
#define FIELD(name) CHECK(result.name == analysis.name, "analysis wire field: " #name)
    FIELD(valid); FIELD(sampleRate); FIELD(channels); FIELD(framesAnalyzed); FIELD(peakDb); FIELD(rmsDb); FIELD(crestFactor);
    FIELD(lowEnergy); FIELD(lowMidEnergy); FIELD(midEnergy); FIELD(highEnergy); FIELD(spectralCentroid); FIELD(stereoWidth);
    FIELD(midSideRatio); FIELD(correlation); FIELD(muddiness); FIELD(harshness); FIELD(sibilance); FIELD(voicePresence);
    FIELD(reverbEstimate); FIELD(dynamicRange);
#undef FIELD
    for (unsigned i = 0; i < analysis.spectrum.size(); ++i) CHECK(result.spectrum[i] == analysis.spectrum[i], "all spectrum bins survive conversion");
}

int main() {
    char pattern[] = "/tmp/roomcut-command-test.XXXXXX";
    const auto* path = ::mkdtemp(pattern);
    if (!path) return 1;
    const std::filesystem::path directory(path);
    soundRoundTrip(directory);
    replyProjection();
    const auto blocked = directory / "not-a-file";
    std::filesystem::create_directory(blocked);
    {
        Service service(blocked);
        uint32_t status = 99;
        RoomcutControlMsgBuffer request{};
        auto parameters = ChainParams::flat(); parameters.preampDb = -8;
        encodeParameters(parameters, request.setParams);
        CHECK(service.send(request, sizeof(RoomcutSetParamsRequest), ROOMCUT_MSG_SET_PARAMS, &status) && status == 0 && service.saveFailures == 1,
              "existing session-apply contract survives a reported storage failure");
        RoomcutGetParamsReply reply{};
        CHECK(controlGetParams(service.port, 1000, &reply) == KERN_SUCCESS && reply.preampDb == -8 && reply.paramsRevision == 1,
              "storage failure does not roll back the already accepted session state");
        CHECK(std::filesystem::is_directory(blocked), "failed persistence preserves its existing target");
    }
    std::filesystem::remove_all(directory);
    if (!failures) std::puts("all engine command/reply tests passed (real Mach, mailbox and state store)");
    return failures ? 1 : 0;
}
