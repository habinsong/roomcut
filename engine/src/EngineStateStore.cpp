#include "EngineStateStore.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>

namespace roomcut {

std::string EngineStateStore::defaultPath() {
    const char* path = std::getenv("ROOMCUT_STATE_FILE");
    if (path && path[0]) return path;
    const char* userHome = std::getenv("HOME");
    return userHome && userHome[0]
        ? std::string(userHome) + "/Library/Application Support/Roomcut/engine.state" : std::string();
}

PersistentState EngineStateStore::load() const {
    PersistentState state;
    std::ifstream input(path_);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if (key == "realOutputUID") state.realOutputUID = value;
        else if (key == "preferredOutputUID") state.preferredOutputUID = value;
        else if (key == "preset") state.presetId = value;
        else if (key == "params") state.paramsLine = value;
        else if (key == "parametric") state.parametricLine = value;
        else if (key == "dynamics") state.dynamicsLine = value;
        else if (key == "keepDefault") state.keepRoomcutDefault = value == "1";
        else if (key == "volumeBoost") state.volumeBoost = clampVolumeBoost(std::strtod(value.c_str(), nullptr));
    }
    return state;
}

bool EngineStateStore::save(const PersistentState& state) const {
    if (path_.empty()) return false;
    const auto failed = [&] {
        std::fprintf(stderr, "[engine] state: cannot save %s\n", path_.c_str());
        return false;
    };
    for (const auto* text : {&state.realOutputUID, &state.preferredOutputUID, &state.presetId,
                             &state.paramsLine, &state.parametricLine, &state.dynamicsLine}) {
        if (text->find_first_of("\r\n") != std::string::npos || text->find('\0') != std::string::npos) return failed();
    }
    const auto path = std::filesystem::path(path_);
    const auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return failed();
    const auto pattern = (directory / ".roomcut-state.XXXXXX").string();
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    const int fd = ::mkstemp(temporary.data());
    if (fd < 0) return failed();
    FILE* output = ::fdopen(fd, "w");
    if (!output) { ::close(fd); ::unlink(temporary.data()); return failed(); }

    bool ok = true;
    auto put = [&](const char* key, const std::string& value) {
        if (!value.empty() && std::fprintf(output, "%s=%s\n", key, value.c_str()) < 0) ok = false;
    };
    put("realOutputUID", state.realOutputUID);
    put("preferredOutputUID", state.preferredOutputUID);
    if (state.keepRoomcutDefault) put("keepDefault", "1");
    if (state.volumeBoost > 1) {
        if (std::fprintf(output, "volumeBoost=%.4f\n", clampVolumeBoost(state.volumeBoost)) < 0) ok = false;
    }
    put("preset", state.presetId);
    if (!state.presetId.empty()) put("params", state.paramsLine);
    put("parametric", state.parametricLine);
    put("dynamics", state.dynamicsLine);
    if (std::fclose(output) != 0) ok = false;
    if (ok && std::rename(temporary.data(), path_.c_str()) == 0) return true;
    ::unlink(temporary.data());
    return failed();
}

} // namespace roomcut
