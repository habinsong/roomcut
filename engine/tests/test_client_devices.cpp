#include "ClientDeviceFixture.hpp"

static void readbackResults() {
    reset();
    CHECK(roomcutClientOutputDeviceCount() == 1, "output list excludes the Roomcut virtual device");
    char uid[32]{}, name[32]{};
    CHECK(roomcutClientOutputDeviceInfo(0, uid, sizeof(uid), name, sizeof(name)) == 0 &&
          std::strcmp(uid, "test-real") == 0 && std::strcmp(name, "test-real") == 0, "C strings contain selected device metadata");
    char shortUID[5]{};
    CHECK(roomcutClientOutputDeviceInfo(0, shortUID, sizeof(shortUID), nullptr, 0) == 0 &&
          std::strcmp(shortUID, "test") == 0, "small C buffer stays truncated and terminated");
    CHECK(roomcutClientOutputDeviceInfo(-1, nullptr, 0, nullptr, 0) == -3 &&
          roomcutClientOutputDeviceInfo(1, nullptr, 0, nullptr, 0) == -3, "out-of-range index preserves error contract");
    RoomcutClientAudioFormat format{};
    CHECK(roomcutClientAudioFormat("test-real", &format) == 0 && format.bitDepth == 24 &&
          format.sampleRate == 48000 && format.latencyMs == 5, "format combines device, stream, safety and buffer latency");
    CHECK(roomcutClientAudioFormat("test-real", nullptr) == -3, "null format output is rejected");
    CHECK(roomcutClientAudioFormat("missing", &format) == -1 && format.sampleRate == 48000,
          "failed device resolution preserves format output");
    std::array<RoomcutClientDeviceFormat, 3> options{};
    CHECK(roomcutClientDeviceFormatOptions("test-real", nullptr, 0) == 2, "format count query needs no output buffer");
    CHECK(roomcutClientDeviceFormatOptions("test-real", options.data(), 1) == 2 &&
          options[0].sampleRate == 48000 && options[1].sampleRate == 0, "format output capacity limits writes, not total count");
    auto range = availableFormat(0);
    range.mSampleRateRange = {48000, 96000};
    physicalFormats.insert(physicalFormats.begin() + 1, range);
    CHECK(roomcutClientDeviceFormatOptions("test-real", options.data(), options.size()) == 3 &&
          options[0].sampleRate == 48000 && options[1].sampleRate == 88200 && options[2].sampleRate == 96000,
          "continuous rate expansion and duplicate removal preserve ordering");
    CHECK(roomcutClientRoomcutIsDefault() == 0, "default readback identifies real output");
    defaultOutput = virtualID;
    CHECK(roomcutClientRoomcutIsDefault() == 1, "default readback identifies virtual output");
    virtualDevice.present = false;
    CHECK(roomcutClientRoomcutIsDefault() == -1, "missing virtual device is not a false default match");
    CHECK(totalWrites == 0, "metadata and default readback never write devices");
}

static void defaultOutputResults() {
    reset();
    CHECK(roomcutClientMakeDefaultOutput() == 0 && defaultOutput == virtualID, "select virtual output");
    CHECK(roomcutClientMakeDefaultOutput() == 1 && defaultWrites == 1, "already-selected virtual output is a no-op");
    CHECK(roomcutClientRestoreRealDefault() == 0 && defaultOutput == realID, "restore selected real output");
    CHECK(roomcutClientRestoreRealDefault() == 1 && defaultWrites == 2, "already-selected real output is a no-op");
    reset(); failDefault = true;
    CHECK(roomcutClientMakeDefaultOutput() < 0 && defaultOutput == realID, "default-output failure preserves its error");
    reset(); virtualDevice.present = false;
    CHECK(roomcutClientMakeDefaultOutput() < 0 && defaultWrites == 0, "missing virtual device cannot trigger a write");
}

static void formatResults() {
    reset();
    CHECK(roomcutClientSetDeviceFormat("test-real", 96000, 24) == 0, "accept advertised format");
    CHECK(physicalFormat.mSampleRate == 96000 && real.sampleRate == 96000 && virtualDevice.sampleRate == 96000,
          "all three format stages receive the requested rate");
    reset(); failPhysical = true;
    CHECK(roomcutClientSetDeviceFormat("test-real", 96000, 24) < 0 && real.rateWrites == 0 && virtualDevice.rateWrites == 0,
          "physical failure stops rate writes");
    reset(); real.failRate = true;
    const int realFailure = roomcutClientSetDeviceFormat("test-real", 96000, 24);
    std::printf("real rate rejected: result=%d, physical=%.0f, real=%.0f, virtual=%.0f\n",
                realFailure, physicalFormat.mSampleRate, real.sampleRate, virtualDevice.sampleRate);
    CHECK(realFailure < 0 && virtualDevice.rateWrites == 0, "real nominal-rate failure is not hidden and stops virtual write");
    reset(); virtualDevice.failRate = true;
    CHECK(roomcutClientSetDeviceFormat("test-real", 96000, 24) < 0, "virtual nominal-rate failure is not success");
    CHECK(real.sampleRate == 96000 && virtualDevice.sampleRate == 48000, "partial format state is observable, not falsely rolled back");
    for (double rate : {0.0, -48000.0, 12345.0, std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity()}) {
        reset();
        CHECK(roomcutClientSetDeviceFormat("test-real", rate, 24) < 0 && totalWrites == 0,
              "invalid or unadvertised rate cannot write a device");
    }
    reset();
    CHECK(roomcutClientSetDeviceFormat("test-real", 96000, 32) < 0 && totalWrites == 0, "unadvertised depth cannot write a device");
    reset(); virtualDevice.present = false;
    CHECK(roomcutClientSetDeviceFormat("test-real", 96000, 24) == 0 && real.sampleRate == 96000,
          "real-only format setting remains supported");
}

static void balanceResults() {
    for (double input : {-2.0, -1.0, 0.0, 1.0, 2.0}) {
        reset();
        CHECK(roomcutClientBalanceSet(input) == 0, "finite pan is accepted with existing endpoint clamping");
        double readback = -99;
        CHECK(roomcutClientBalanceGet(&readback) == 0 && readback == std::fmin(1.0, std::fmax(-1.0, input)),
              "endpoint balance round-trips without increasing volume");
        CHECK(std::fmax(real.channels[0].volume, real.channels[1].volume) == 0.4f, "near channel keeps its reference level");
    }
    reset(); real.channels[0].volume = 0.8f; real.channels[1].volume = 0.4f;
    CHECK(roomcutClientBalanceSet(0.25) == 0, "set right balance");
    CHECK(std::fabs(real.channels[0].volume - 0.6f) < 1e-6 && real.channels[1].volume == 0.8f,
          "balance uses the current maximum channel level");
    double pan = -99;
    CHECK(roomcutClientBalanceGet(&pan) == 0 && std::fabs(pan - 0.25) < 1e-6, "read back the applied balance");
    CHECK(roomcutClientBalanceSet(-0.5) == 0 && real.channels[0].volume == 0.8f && real.channels[1].volume == 0.4f,
          "reverse balance without increasing reference level");
    reset(); real.channels[0].volume = real.channels[1].volume = 0;
    const int silence = roomcutClientBalanceSet(0.5);
    std::printf("silent channels: result=%d, left=%.2f, right=%.2f\n", silence, real.channels[0].volume, real.channels[1].volume);
    CHECK(silence == 0 && real.channels[0].volume == 0 && real.channels[1].volume == 0,
          "moving balance cannot unmute silent channels at full level");
    for (double input : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        reset();
        CHECK(roomcutClientBalanceSet(input) == -3 && totalWrites == 0, "invalid pan cannot write channels");
    }
    for (int index : {0, 1}) {
        reset(); real.channels[index].readable = false;
        CHECK(roomcutClientBalanceSet(0.5) != 0 && totalWrites == 0, "read failure cannot default to maximum volume");
        reset(); real.channels[index].shortRead = true;
        CHECK(roomcutClientBalanceSet(0.5) != 0 && totalWrites == 0, "short channel payload cannot write channels");
        reset(); real.channels[index].volume = std::numeric_limits<float>::quiet_NaN();
        CHECK(roomcutClientBalanceSet(0.5) != 0 && totalWrites == 0, "non-finite channel level cannot write channels");
        pan = 0.75;
        CHECK(roomcutClientBalanceGet(&pan) != 0 && pan == 0.75, "invalid readback preserves caller output");
        reset(); real.channels[index].writable = false;
        CHECK(roomcutClientBalanceSet(0.5) != 0 && totalWrites == 0, "preflight both channels before any write");
    }
    reset(); real.channels[0].failWrite = true;
    CHECK(roomcutClientBalanceSet(0.5) < 0 && real.channels[1].writes == 0, "first channel failure stops the second write");
    reset(); real.channels[1].failWrite = true;
    CHECK(roomcutClientBalanceSet(0.5) < 0 && real.channels[0].volume == 0.2f && real.channels[1].volume == 0.4f,
          "second channel failure reports partial state without claiming rollback");
    reset(); real.present = false;
    CHECK(roomcutClientBalanceSet(0.5) < 0 && totalWrites == 0, "a vanished selected output cannot redirect balance to another device");
    reset();
    { std::lock_guard<std::mutex> lock(engineMutex); rejectState = true; }
    CHECK(roomcutClientBalanceSet(0.5) < 0 && totalWrites == 0, "unknown output state prevents uncoordinated balance writes");
    reset(); real.present = false;
    { std::lock_guard<std::mutex> lock(engineMutex); selectReal = false; }
    CHECK(roomcutClientBalanceSet(0.5) == 0 && virtualDevice.channels[0].volume == 0.2f,
          "known unselected state retains the virtual balance route");
}

int main() {
    Service service;
    readbackResults();
    defaultOutputResults();
    formatResults();
    balanceResults();
    if (!failures) std::puts("all client device command tests passed (real C/Mach, fake HAL/service lookup)");
    return failures ? 1 : 0;
}
