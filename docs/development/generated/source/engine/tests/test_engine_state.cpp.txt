#include "EngineStateStore.hpp"
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static void testParameterCodec() {
    auto params = ChainParams::flat();
    params.preampDb = -3;
    params.eqGainsDb[5] = 6;
    params.outputGainDb = -2;
    params.limiterReleaseMs = 250;
    params.spatialWidth = 30;
    params.centerFocus = 40;
    params.crossfeed = 15;
    params.roomReduce = 10;
    params.spatialMode = 3;
    params.highpassHz = 80;
    params.compAmount = 25;
    params.parametric[1].enabled = true;
    params.parametric[1].freqHz = 1234.125;
    params.parametric[1].gainDb = -2.5;
    params.parametric[1].q = 0.75;
    params.parametric[3].freqHz = 400; // disabled edits must also survive restart
    ChainParams restored;
    CHECK(parseParamsLine(serializeParamsLine(params), &restored), "scalar state decodes");
    CHECK(parseParametricLine(serializeParametricLine(params), &restored), "parametric state decodes");
    CHECK(restored == params, "all current controls round-trip through the legacy file format");

    auto precise = params;
    precise.preampDb = -3.123456789012;
    precise.eqGainsDb[0] = 1e-12;
    precise.parametric[1].freqHz = 1234.123456789;
    precise.parametric[1].gainDb = std::nextafter(2.5, 3.0);
    precise.parametric[1].q = 0.76543219876;
    CHECK(parseParamsLine(serializeParamsLine(precise), &restored) &&
          parseParametricLine(serializeParametricLine(precise), &restored) && restored == precise,
          "fractional DSP values survive persistence without decimal rounding");

    auto disabled = ChainParams::flat();
    disabled.parametric[0].freqHz = 500;
    CHECK(!serializeParametricLine(disabled).empty(), "disabled band edits are retained");
    CHECK(serializeParametricLine(ChainParams::flat()).empty(), "default bands need no extra line");

    const auto before = restored;
    CHECK(!parseParamsLine("0 1 2", &restored) && restored == before, "truncated scalars cannot partially change state");
    CHECK(!parseParamsLine("nan 0 0 0 0 0 0 0 0 0 0 100 0", &restored) && restored == before,
          "non-finite values are rejected before application");
    CHECK(!parseParametricLine("1 0 500 -3 2", &restored) && restored == before,
          "truncated band banks cannot partially change state");

    CHECK(parseParamsLine("-2 0 0 0 0 0 3 0 0 0 0 100 -1", &restored), "pre-spatial state remains readable");
    CHECK(restored.preampDb == -2 && restored.eqGainsDb[5] == 3 && restored.outputGainDb == -1,
          "legacy numeric positions are unchanged");
    CHECK(restored.spatialWidth == 0 && restored.compAmount == 0, "missing legacy fields retain neutral defaults");
}

static void testStore(const std::filesystem::path& directory) {
    EngineStateStore store((directory / "engine.state").string());
    CHECK(store.load().presetId.empty(), "missing state has safe defaults");
    PersistentState state;
    state.realOutputUID = "device-A";
    state.preferredOutputUID = "device-B";
    state.presetId = "custom";
    state.keepRoomcutDefault = true;
    state.volumeBoost = 1.5;
    state.paramsLine = serializeParamsLine(ChainParams::flat());
    CHECK(store.save(state), "complete state is saved");
    auto restored = store.load();
    CHECK(restored.realOutputUID == state.realOutputUID && restored.preferredOutputUID == state.preferredOutputUID
          && restored.presetId == state.presetId && restored.paramsLine == state.paramsLine
          && restored.volumeBoost == 1.5 && restored.keepRoomcutDefault, "stored device and DSP state round-trip");

    auto invalid = state;
    invalid.realOutputUID = "device\npreset=injected";
    CHECK(!store.save(invalid), "line breaks cannot inject state keys");
    CHECK(store.load().realOutputUID == state.realOutputUID, "a failed save preserves the previous file");

    auto targetDirectory = directory / "not-a-file";
    std::filesystem::create_directory(targetDirectory);
    EngineStateStore badTarget(targetDirectory.string());
    CHECK(!badTarget.save(state), "a failed rename is reported");
    CHECK(std::filesystem::is_directory(targetDirectory), "failed replacement does not remove its target");
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        CHECK(entry.path().filename().string().find(".roomcut-state.") != 0, "temporary files are cleaned on success and failure");
    }

    // Long lines and CRLF from manually edited/older files are not split into keys.
    {
        std::ofstream output(directory / "engine.state");
        output << "realOutputUID=" << std::string(700, 'x') << "\r\npreset=flat\r\nvolumeBoost=nan\r\n";
    }
    restored = store.load();
    CHECK(restored.realOutputUID.size() == 700 && restored.presetId == "flat", "state reader preserves complete lines");
    CHECK(restored.volumeBoost == 1, "invalid boost recovers to unity");
}

static void testAtomicReaders(const std::filesystem::path& directory) {
    EngineStateStore store((directory / "concurrent.state").string());
    PersistentState a, b;
    a.realOutputUID = a.preferredOutputUID = a.presetId = "A";
    b.realOutputUID = b.preferredOutputUID = b.presetId = "B";
    CHECK(store.save(a), "initial atomic-reader state saved");
    std::atomic<bool> done{false};
    bool writesOK = true;
    std::thread writer([&] {
        for (int i = 0; i < 3000; ++i) writesOK &= store.save(i % 2 ? a : b);
        done.store(true, std::memory_order_release);
    });
    bool coherent = true;
    unsigned reads = 0;
    do {
        const auto state = store.load();
        coherent &= (state.presetId == "A" || state.presetId == "B")
            && state.realOutputUID == state.presetId && state.preferredOutputUID == state.presetId;
        ++reads;
    } while (!done.load(std::memory_order_acquire));
    writer.join();
    CHECK(writesOK && coherent && reads > 0, "concurrent readers see complete old or new state, never an empty/torn file");
}

int main() {
    char pattern[] = "/tmp/roomcut-state-test.XXXXXX";
    const char* created = ::mkdtemp(pattern);
    if (!created) return 1;
    const std::filesystem::path directory(created);
    testParameterCodec();
    testStore(directory);
    testAtomicReaders(directory);
    std::filesystem::remove_all(directory);
    if (failures == 0) std::puts("all engine state tests passed");
    return failures == 0 ? 0 : 1;
}
