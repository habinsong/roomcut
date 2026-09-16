// test_engine_diagnostics.cpp — the dev-only capture surface: what the command
// line accepts, what it refuses, and whether the float32 WAV it writes is the
// file scripts/analyze-dump.py expects.
#include "EngineDiagnostics.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static bool parse(std::vector<const char*> args, EngineOptions& out, std::string& error) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const char* a : args) argv.push_back(const_cast<char*>(a));
    return parseEngineOptions((int)argv.size(), argv.data(), out, error);
}

static void testNoArgumentsCaptureNothingAndStayFlat() {
    EngineOptions options; std::string error;
    CHECK(parse({"engine"}, options, error), "no arguments is valid");
    CHECK(!options.dumping(), "capture is off unless asked for");
    CHECK(!options.eqGiven, "no dev EQ was given");
    for (double g : options.params.eqGainsDb) CHECK(g == 0.0, "the chain stays flat");
}

static void testDumpPathAndEqCurveAreRead() {
    EngineOptions options; std::string error;
    CHECK(parse({"engine", "--dump", "/tmp/out.wav", "--eq",
                 "1,-2,3,-4,5,-6,7,-8,9,-10.5"}, options, error), "both flags parse");
    CHECK(options.dumping() && options.dumpPath == "/tmp/out.wav", "dump path is kept");
    CHECK(options.eqGiven, "the dev EQ is marked as given so it wins over resumed state");
    CHECK(options.params.eqGainsDb[0] == 1.0 && options.params.eqGainsDb[9] == -10.5,
          "band gains are read in order");
}

static void testRefusedArgumentsReportWhyAndDoNotRun() {
    struct Case { std::vector<const char*> args; const char* what; };
    const Case cases[] = {
        {{"engine", "--eq", "1,2,3"}, "a short EQ list is refused"},
        {{"engine", "--eq", "1,2,3,4,5,6,7,8,9,10,11"}, "a long EQ list is refused"},
        {{"engine", "--eq", "1,2,3,4,5,6,7,8,9,x"}, "a non-numeric gain is refused"},
        {{"engine", "--eq"}, "a flag with no value is refused"},
        {{"engine", "--dump"}, "a dump with no path is refused"},
        {{"engine", "--record"}, "an unknown flag is refused"},
    };
    for (const auto& c : cases) {
        EngineOptions options; std::string error;
        CHECK(!parse(c.args, options, error), c.what);
        CHECK(!error.empty(), "a refusal always says why");
    }
}

static void testWrittenWavIsReadableFloat32() {
    const uint32_t frames = 4, channels = 2, rate = 48000;
    const float samples[frames * channels] = {0.0f, -0.5f, 0.25f, 1.0f, -1.0f, 0.125f, 0.5f, -0.25f};
    const char* path = "/tmp/roomcut-diagnostics-test.wav";
    CHECK(writeWavF32(path, samples, frames, channels, rate), "the capture is written");

    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr, "the file exists");
    if (f == nullptr) return;
    std::vector<unsigned char> bytes;
    unsigned char chunk[512];
    while (std::size_t got = std::fread(chunk, 1, sizeof(chunk), f)) bytes.insert(bytes.end(), chunk, chunk + got);
    std::fclose(f);

    const uint32_t dataBytes = frames * channels * (uint32_t)sizeof(float);
    CHECK(bytes.size() == 44 + dataBytes, "header is 44 bytes and the data follows");
    CHECK(std::memcmp(bytes.data(), "RIFF", 4) == 0, "RIFF");
    CHECK(std::memcmp(bytes.data() + 8, "WAVEfmt ", 8) == 0, "WAVE fmt");
    CHECK(std::memcmp(bytes.data() + 36, "data", 4) == 0, "data chunk");
    uint16_t format = 0, ch = 0, bits = 0;
    uint32_t sampleRate = 0, written = 0;
    std::memcpy(&format, bytes.data() + 20, 2);
    std::memcpy(&ch, bytes.data() + 22, 2);
    std::memcpy(&sampleRate, bytes.data() + 24, 4);
    std::memcpy(&bits, bytes.data() + 34, 2);
    std::memcpy(&written, bytes.data() + 40, 4);
    CHECK(format == 3 && bits == 32, "float32 PCM");
    CHECK(ch == channels && sampleRate == rate, "channels and rate round-trip");
    CHECK(written == dataBytes, "the data size matches the frames written");
    std::vector<float> back(frames * channels);
    std::memcpy(back.data(), bytes.data() + 44, dataBytes);
    for (std::size_t i = 0; i < back.size(); ++i) CHECK(back[i] == samples[i], "samples round-trip");
    std::remove(path);
}

static void testAnUnwritablePathFails() {
    const float sample = 0.0f;
    CHECK(!writeWavF32("/this/directory/does/not/exist/out.wav", &sample, 1, 1, 48000),
          "a capture that cannot be written reports failure");
}

static void testBedRendererIsChosenOnTheCommandLine() {
    EngineOptions options; std::string error;
    CHECK(parse({"engine"}, options, error) && !options.systemBedRenderer, "the built-in bed renderer is the default");
    CHECK(parse({"engine", "--bed-renderer", "system"}, options, error) && options.systemBedRenderer, "system selects AUSpatialMixer");
    CHECK(parse({"engine", "--bed-renderer", "builtin"}, options, error) && !options.systemBedRenderer, "builtin selects the stage's own render");
    EngineOptions refused; std::string why;
    CHECK(!parse({"engine", "--bed-renderer", "apple"}, refused, why) && why.find("--bed-renderer") != std::string::npos,
          "an unknown renderer is refused and named");
    CHECK(!parse({"engine", "--bed-renderer"}, refused, why), "the flag needs a value");
}

int main() {
    testNoArgumentsCaptureNothingAndStayFlat();
    testBedRendererIsChosenOnTheCommandLine();
    testDumpPathAndEqCurveAreRead();
    testRefusedArgumentsReportWhyAndDoNotRun();
    testWrittenWavIsReadableFloat32();
    testAnUnwritablePathFails();
    if (failures == 0) {
        std::printf("all engine diagnostics tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d engine diagnostics check(s) failed\n", failures);
    return 1;
}
