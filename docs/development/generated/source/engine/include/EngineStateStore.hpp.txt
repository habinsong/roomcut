#ifndef ROOMCUT_ENGINE_STATE_STORE_HPP
#define ROOMCUT_ENGINE_STATE_STORE_HPP

#include "EngineState.hpp"
#include <utility>

namespace roomcut {

// Non-real-time persistence boundary. The explicit path keeps tests away from
// installed state. Saves replace complete files atomically in the same directory.
class EngineStateStore {
public:
    explicit EngineStateStore(std::string path) : path_(std::move(path)) {}
    static std::string defaultPath();
    PersistentState load() const;
    bool save(const PersistentState& state) const;
private:
    std::string path_;
};

} // namespace roomcut
#endif
