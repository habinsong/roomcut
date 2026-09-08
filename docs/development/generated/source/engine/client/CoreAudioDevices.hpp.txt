#ifndef ROOMCUT_CLIENT_CORE_AUDIO_DEVICES_HPP
#define ROOMCUT_CLIENT_CORE_AUDIO_DEVICES_HPP

#include <CoreAudio/CoreAudio.h>
#include <cstdint>
#include <string>
#include <vector>

namespace roomcut::devices {

// Non-realtime HAL access only. Engine state, C ABI conversion and Mach
// connection ownership stay with the client. No device or engine cache here.
struct OutputDevice { AudioDeviceID id; std::string uid; std::string name; };
struct AudioFormat { uint32_t bitDepth; double sampleRate; double latencyMs; };
struct FormatOption { double sampleRate; uint32_t bitDepth; };

std::vector<OutputDevice> realOutputs();
AudioDeviceID realOutput(const char* uid);
AudioDeviceID roomcutOutput();
AudioDeviceID defaultOutput();
// 0 = change accepted, 1 = already selected, <0 = error.
int setDefaultOutput(AudioDeviceID device);

int audioFormat(AudioDeviceID device, AudioFormat* out);
// Returns the total option count, or a negative error.
int formatOptions(AudioDeviceID device, std::vector<FormatOption>& out);
int setFormat(AudioDeviceID device, double sampleRate, unsigned bitDepth);

bool volume(AudioDeviceID device, double* out);
int setVolume(AudioDeviceID device, double scalar);
int balance(AudioDeviceID device, double* out);
int setBalance(AudioDeviceID device, double pan);

} // namespace roomcut::devices
#endif
