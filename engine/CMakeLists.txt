# RoomcutAudioEngine — the helper process that owns the shared ring + Mach
# service and (Phase 3) reads the driver's audio across the process boundary.
#
# Targets:
#   RoomcutAudioEngine    the real engine binary (bootstrap_check_in service,
#                         HELLO/HEALTH dispatch, ring reader thread)
#   roomcut-driver-sim    a dev-only out-of-sandbox stand-in for the driver, used
#                         to verify the cross-process handoff end to end. Built
#                         only when ROOMCUT_BUILD_TESTS is ON.
#
# The transport sources (RingRegion/Handshake/Heartbeat/Lifecycle) are shared
# with the host unit tests in core/tests; here they are compiled into the real
# binaries so the same code path runs in production.

set(ROOMCUT_ENGINE_TRANSPORT
  ${CMAKE_CURRENT_SOURCE_DIR}/src/RingRegion.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Handshake.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Heartbeat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Control.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Lifecycle.cpp)

# Output rendering (Phase 4) + device selection/recovery (Phase 5): the real
# output device + DSP live in the engine binary only (the driver-sim doesn't
# render). CoreAudio/AudioUnit frameworks.
set(ROOMCUT_ENGINE_OUTPUT
  ${CMAKE_CURRENT_SOURCE_DIR}/src/OutputDevice.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceSelection.cpp)

find_library(AudioUnit_FRAMEWORK AudioUnit REQUIRED)
find_library(CoreAudio_FRAMEWORK CoreAudio REQUIRED)
find_library(AudioToolbox_FRAMEWORK AudioToolbox REQUIRED)
find_library(CoreFoundation_FRAMEWORK CoreFoundation REQUIRED)

add_executable(RoomcutAudioEngine
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
  ${ROOMCUT_ENGINE_TRANSPORT}
  ${ROOMCUT_ENGINE_OUTPUT})
target_include_directories(RoomcutAudioEngine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/core/dsp
  ${CMAKE_SOURCE_DIR}/core/presets)
target_link_libraries(RoomcutAudioEngine PRIVATE
  roomcut_protocol RoomcutCore
  ${AudioUnit_FRAMEWORK} ${CoreAudio_FRAMEWORK} ${AudioToolbox_FRAMEWORK}
  ${CoreFoundation_FRAMEWORK})
target_compile_options(RoomcutAudioEngine PRIVATE -Wall -Wextra)
find_package(Threads REQUIRED)
target_link_libraries(RoomcutAudioEngine PRIVATE Threads::Threads)

# roomcutctl — control CLI over the engine's Mach service (status / preset /
# bypass / health). Product tool: the same wire the menu-bar app will use.
add_executable(roomcutctl
  ${CMAKE_CURRENT_SOURCE_DIR}/src/roomcutctl.cpp
  ${ROOMCUT_ENGINE_TRANSPORT})
target_include_directories(roomcutctl PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/core/dsp
  ${CMAKE_SOURCE_DIR}/core/presets)
target_link_libraries(roomcutctl PRIVATE roomcut_protocol RoomcutCore Threads::Threads)
target_compile_options(roomcutctl PRIVATE -Wall -Wextra)

if(ROOMCUT_BUILD_TESTS)
  add_executable(roomcut-driver-sim
    ${CMAKE_CURRENT_SOURCE_DIR}/src/roomcut-driver-sim.cpp
    ${ROOMCUT_ENGINE_TRANSPORT})
  target_include_directories(roomcut-driver-sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  # RoomcutCore: header-only, used for GraphicEQ::kCenters (--tones test signal).
  target_link_libraries(roomcut-driver-sim PRIVATE roomcut_protocol RoomcutCore Threads::Threads)
  target_compile_options(roomcut-driver-sim PRIVATE -Wall -Wextra)

  # Dev CLI to inspect/flip the system default output device (Phase 5 recovery
  # tests drive it from scripts; macOS has no built-in CLI for this).
  add_executable(roomcut-devicectl
    ${CMAKE_CURRENT_SOURCE_DIR}/src/roomcut-devicectl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceSelection.cpp)
  target_include_directories(roomcut-devicectl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(roomcut-devicectl PRIVATE
    ${CoreAudio_FRAMEWORK} ${CoreFoundation_FRAMEWORK})
  target_compile_options(roomcut-devicectl PRIVATE -Wall -Wextra)
endif()
