/*
 * roomcut-devicectl.cpp — dev-only CLI over DeviceSelection (Phase 5).
 *
 * Exists so the recovery tests can inspect and flip the system default output
 * device from a script (macOS ships no CLI for this), exercising the exact
 * code path the engine's recovery uses. Built only with ROOMCUT_BUILD_TESTS;
 * not shipped.
 *
 *   roomcut-devicectl list         all output devices (* = system default)
 *   roomcut-devicectl get          UID of the current default output
 *   roomcut-devicectl set <uid>    make <uid> the default output
 */
#include "DeviceSelection.hpp"

#include <cstdio>
#include <cstring>

using namespace roomcut;

int main(int argc, char** argv) {
    const char* cmd = argc > 1 ? argv[1] : "list";

    if (std::strcmp(cmd, "list") == 0) {
        AudioDeviceID def = defaultOutputDevice();
        for (const auto& d : listOutputDevices()) {
            std::printf("%c %-40s %s%s\n", d.id == def ? '*' : ' ',
                        d.uid.c_str(), d.name.c_str(),
                        d.builtin ? " [builtin]" : "");
        }
        return 0;
    }
    if (std::strcmp(cmd, "get") == 0) {
        AudioDeviceID def = defaultOutputDevice();
        if (def == kAudioObjectUnknown) {
            std::fprintf(stderr, "no default output device\n");
            return 1;
        }
        std::printf("%s\n", deviceUID(def).c_str());
        return 0;
    }
    if (std::strcmp(cmd, "set") == 0 && argc > 2) {
        for (const auto& d : listOutputDevices()) {
            if (d.uid == argv[2]) {
                OSStatus err = setDefaultOutputDevice(d.id);
                if (err != noErr) {
                    std::fprintf(stderr, "set failed: %d\n", (int)err);
                    return 1;
                }
                return 0;
            }
        }
        std::fprintf(stderr, "no output device with UID '%s'\n", argv[2]);
        return 1;
    }

    std::fprintf(stderr, "usage: %s [list | get | set <uid>]\n", argv[0]);
    return 2;
}
