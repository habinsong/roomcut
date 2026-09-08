#include "ClientDeviceFixture.hpp"

int main() {
    Service service;
    reset();
    { std::lock_guard<std::mutex> lock(engineMutex); boostStatus = 7; }
    const int rejected = roomcutClientVolumeSet(1.5);
    std::printf("rejected boost: result=%d, real=%.2f, virtual=%.2f\n", rejected, real.volume, virtualDevice.volume);
    CHECK(rejected == 7 && unchanged(), "boost rejection propagates without changing device scalars");

    reset(); real.failVolume = true;
    CHECK(roomcutClientVolumeSet(0.6) < 0, "real volume failure is not hidden by a successful virtual write");
    CHECK(virtualDevice.volumeWrites == 0, "stop remaining device writes after a real failure");
    reset(); virtualDevice.failVolume = true;
    CHECK(roomcutClientVolumeSet(0.6) < 0, "virtual failure is not hidden by a successful real write");
    CHECK(real.volume == 0.6f && virtualDevice.volume == 0.4f, "report actual partial state without claiming rollback");

    reset(); real.muted = real.failMute = true;
    CHECK(roomcutClientVolumeSet(0.6) < 0 && real.muted, "failed unmute is not reported as successful audible volume");
    reset(); real.failQuery = true;
    CHECK(roomcutClientVolumeSet(0.6) < 0 && virtualDevice.volumeWrites == 0,
          "a failed property query is an error, not unsupported hardware");
    reset(); real.muteWritable = false;
    CHECK(roomcutClientVolumeSet(0.6) == 0 && real.muteWrites == 0,
          "already-unmuted read-only mute does not prevent a valid scalar write");

    reset();
    { std::lock_guard<std::mutex> lock(engineMutex); supportsBoost = false; }
    CHECK(roomcutClientVolumeSet(1.8) != 0 && unchanged(), "unsupported boost cannot silently apply 100 percent");
    CHECK(roomcutClientVolumeSet(0.6) == 0, "legacy engine keeps ordinary volume support");
    reset(); real.writable = false;
    CHECK(roomcutClientVolumeSet(0.6) == 0 && real.volumeWrites == 0 && virtualDevice.volume == 0.6f,
          "fixed hardware volume keeps the virtual software-volume route");
    reset(); real.hasVolume = false;
    CHECK(roomcutClientVolumeSet(0.6) == 0 && real.volumeWrites == 0 && virtualDevice.volume == 0.6f,
          "hardware without a master scalar keeps virtual software volume");
    reset(); virtualDevice.writable = false;
    CHECK(roomcutClientVolumeSet(0.6) != 0, "a present non-settable virtual control cannot be hidden by real success");
    reset(); virtualDevice.present = false;
    CHECK(roomcutClientVolumeSet(0.6) == 0 && real.volume == 0.6f, "existing real-only fallback remains supported");
    reset(); real.present = false;
    CHECK(roomcutClientVolumeSet(0.6) < 0 && virtualDevice.volumeWrites == 0,
          "a vanished selected device cannot report successful volume");
    reset(); real.present = false;
    { std::lock_guard<std::mutex> lock(engineMutex); selectReal = false; }
    CHECK(roomcutClientVolumeSet(0.6) == 0 && virtualDevice.volume == 0.6f, "unselected engine keeps virtual-only volume");

    for (double input : {0.0, 0.6, 1.0, 1.5, 2.0, -0.5, 3.0}) {
        reset();
        CHECK(roomcutClientVolumeSet(input) == 0, "valid or clamped finite volume is accepted");
        double readback = -1;
        CHECK(roomcutClientVolumeGet(&readback) == 0 && std::fabs(readback - std::fmin(2.0, std::fmax(0.0, input))) < 1e-6,
              "C API readback matches effective volume across hardware and boost");
    }
    for (double input : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
        reset();
        CHECK(roomcutClientVolumeSet(input) == -3 && unchanged(), "non-finite input cannot set maximum hardware volume");
    }
    reset();
    { std::lock_guard<std::mutex> lock(engineMutex); dropBoost = true; }
    CHECK(roomcutClientVolumeSet(1.5) < 0 && unchanged(), "lost boost reply cannot fall through into hardware writes");
    { std::lock_guard<std::mutex> lock(engineMutex); CHECK(boostWrites == 2, "lost reply retry remains bounded"); }
    reset(); real.failVolume = true;
    CHECK(roomcutClientVolumeSet(1.5) < 0 && unchanged(), "failed hardware step leaves scalars unchanged");
    { std::lock_guard<std::mutex> lock(engineMutex); CHECK(appliedBoost == 1.5, "completed boost is not misreported as rolled back"); }
    reset();
    { std::lock_guard<std::mutex> lock(engineMutex); rejectState = true; appliedBoost = 1.8; }
    CHECK(roomcutClientVolumeSet(0.6) < 0 && unchanged(), "unknown existing boost prevents an uncoordinated write");
    if (!failures) std::puts("all client volume tests passed (real C shim/Mach, fake HAL/service lookup)");
    return failures ? 1 : 0;
}
