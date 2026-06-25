# RoomcutCore tests. Enabled with -DROOMCUT_BUILD_TESTS=ON.
# Host-side unit tests; no CoreAudio/coreaudiod involvement.

add_executable(test_ring test_ring.c)
target_link_libraries(test_ring PRIVATE roomcut_protocol)
target_compile_options(test_ring PRIVATE -Wall -Wextra)
find_package(Threads REQUIRED)
target_link_libraries(test_ring PRIVATE Threads::Threads)

add_test(NAME ring COMMAND test_ring)

# RingRegion (Mach memory-entry handoff) test. Compiles the engine's RingRegion
# directly; no CoreAudio device needed — it maps the same entry twice in-process.
add_executable(test_ring_region
  ${CMAKE_SOURCE_DIR}/engine/tests/test_ring_region.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/RingRegion.cpp)
target_include_directories(test_ring_region PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(test_ring_region PRIVATE roomcut_protocol)
target_compile_options(test_ring_region PRIVATE -Wall -Wextra)

add_test(NAME ring_region COMMAND test_ring_region)

# HELLO handshake end-to-end: real mach_msg port-descriptor transfer of the
# memory-entry send-right between an engine thread and a driver thread.
add_executable(test_handshake
  ${CMAKE_SOURCE_DIR}/engine/tests/test_handshake.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/Handshake.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/RingRegion.cpp
  ${CMAKE_SOURCE_DIR}/driver/RoomcutHAL/src/RoomcutTransport.c)
target_include_directories(test_handshake PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include
  ${CMAKE_SOURCE_DIR}/driver/RoomcutHAL/include)
target_link_libraries(test_handshake PRIVATE roomcut_protocol Threads::Threads)
target_compile_options(test_handshake PRIVATE -Wall -Wextra)

add_test(NAME handshake COMMAND test_handshake)

# Heartbeat / HEALTH_CHECK liveness: live peer round-trip + dead-peer timeout
# (the EngineLost signal).
add_executable(test_heartbeat
  ${CMAKE_SOURCE_DIR}/engine/tests/test_heartbeat.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/Heartbeat.cpp)
target_include_directories(test_heartbeat PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(test_heartbeat PRIVATE roomcut_protocol Threads::Threads)
target_compile_options(test_heartbeat PRIVATE -Wall -Wextra)

add_test(NAME heartbeat COMMAND test_heartbeat)

# Control plane params readback: app/CLI side request helpers against a small
# in-process Mach service, no CoreAudio device.
add_executable(test_control
  ${CMAKE_SOURCE_DIR}/engine/tests/test_control.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/Control.cpp)
target_include_directories(test_control PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(test_control PRIVATE roomcut_protocol Threads::Threads)
target_compile_options(test_control PRIVATE -Wall -Wextra)

add_test(NAME control COMMAND test_control)

# Lifecycle state machines: pure (state,event) transitions + §4.5 wire
# projection. No IPC/device — fully deterministic.
add_executable(test_lifecycle
  ${CMAKE_SOURCE_DIR}/engine/tests/test_lifecycle.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/Lifecycle.cpp)
target_include_directories(test_lifecycle PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(test_lifecycle PRIVATE roomcut_protocol)
target_compile_options(test_lifecycle PRIVATE -Wall -Wextra)

add_test(NAME lifecycle COMMAND test_lifecycle)

# LinearResampler: streaming linear-interpolation resampler for the render path.
# Header-only; no IPC/device. Verifies contiguous consumption + interpolation.
add_executable(test_resampler
  ${CMAKE_SOURCE_DIR}/engine/tests/test_resampler.cpp)
target_include_directories(test_resampler PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include)
target_compile_options(test_resampler PRIVATE -Wall -Wextra)

add_test(NAME resampler COMMAND test_resampler)

# DSP: Biquad frequency response (analytic + empirical) — header-only, no deps.
add_executable(test_biquad ${CMAKE_SOURCE_DIR}/core/tests/test_biquad.cpp)
target_include_directories(test_biquad PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_biquad PRIVATE -Wall -Wextra)

add_test(NAME biquad COMMAND test_biquad)

# DSP: 10-band graphic EQ — header-only, no deps.
add_executable(test_graphic_eq ${CMAKE_SOURCE_DIR}/core/tests/test_graphic_eq.cpp)
target_include_directories(test_graphic_eq PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_graphic_eq PRIVATE -Wall -Wextra)

add_test(NAME graphic_eq COMMAND test_graphic_eq)

# DSP: look-ahead brickwall limiter — header-only, no deps.
add_executable(test_limiter ${CMAKE_SOURCE_DIR}/core/tests/test_limiter.cpp)
target_include_directories(test_limiter PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_limiter PRIVATE -Wall -Wextra)

add_test(NAME limiter COMMAND test_limiter)

add_executable(test_spatial ${CMAKE_SOURCE_DIR}/core/tests/test_spatial.cpp)
target_include_directories(test_spatial PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_spatial PRIVATE -Wall -Wextra)

add_test(NAME spatial COMMAND test_spatial)

# DSP: light single-knob compressor (Dialogue mode) — header-only, no deps.
add_executable(test_compressor ${CMAKE_SOURCE_DIR}/core/tests/test_compressor.cpp)
target_include_directories(test_compressor PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_compressor PRIVATE -Wall -Wextra)

add_test(NAME compressor COMMAND test_compressor)

# DSP: N-band parametric EQ — header-only, no deps.
add_executable(test_parametric ${CMAKE_SOURCE_DIR}/core/tests/test_parametric.cpp)
target_include_directories(test_parametric PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_parametric PRIVATE -Wall -Wextra)

add_test(NAME parametric COMMAND test_parametric)

add_executable(test_analyzer ${CMAKE_SOURCE_DIR}/core/tests/test_analyzer.cpp)
target_include_directories(test_analyzer PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_analyzer PRIVATE -Wall -Wextra)

add_test(NAME analyzer COMMAND test_analyzer)

# DSP: full chain (preamp→HPF→EQ→spatial→comp→limiter, preset/bypass/crossfade).
add_executable(test_dsp_chain ${CMAKE_SOURCE_DIR}/core/tests/test_dsp_chain.cpp)
target_include_directories(test_dsp_chain PRIVATE ${CMAKE_SOURCE_DIR}/core/dsp)
target_compile_options(test_dsp_chain PRIVATE -Wall -Wextra)

add_test(NAME dsp_chain COMMAND test_dsp_chain)

# Presets: validator bounds enforcement + built-in preset sanity.
add_executable(test_presets ${CMAKE_SOURCE_DIR}/core/tests/test_presets.cpp)
target_include_directories(test_presets PRIVATE
  ${CMAKE_SOURCE_DIR}/core/dsp
  ${CMAKE_SOURCE_DIR}/core/presets)
target_compile_options(test_presets PRIVATE -Wall -Wextra)

add_test(NAME presets COMMAND test_presets)

# Phase 5: pure render-target policy (never Roomcut; default → saved → builtin).
# Links DeviceSelection.cpp, so it needs the CoreAudio framework on the host.
find_library(TEST_CoreAudio_FRAMEWORK CoreAudio REQUIRED)
find_library(TEST_CoreFoundation_FRAMEWORK CoreFoundation REQUIRED)
add_executable(test_device_policy
  ${CMAKE_SOURCE_DIR}/engine/tests/test_device_policy.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/DeviceSelection.cpp)
target_include_directories(test_device_policy PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(test_device_policy PRIVATE
  ${TEST_CoreAudio_FRAMEWORK} ${TEST_CoreFoundation_FRAMEWORK})
target_compile_options(test_device_policy PRIVATE -Wall -Wextra)

add_test(NAME device_policy COMMAND test_device_policy)

# Volume ramp: per-sample linear interpolation eliminates zipper noise
# (ENGINE_AUDIT.md #6). Pure math — no CoreAudio/IPC.
add_executable(test_volume_ramp
  ${CMAKE_SOURCE_DIR}/engine/tests/test_volume_ramp.cpp)
target_compile_options(test_volume_ramp PRIVATE -Wall -Wextra)

add_test(NAME volume_ramp COMMAND test_volume_ramp)
