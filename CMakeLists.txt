# Roomcut native build: driver + engine + core (and core tests).
# The menu-bar app (apps/macos) builds separately via SwiftPM — see the root
# Package.swift (`swift build` / `scripts/build-app.sh`).
cmake_minimum_required(VERSION 3.24)

project(Roomcut
  VERSION 1.0.0
  DESCRIPTION "System-wide audio controller for macOS"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT APPLE)
  message(FATAL_ERROR "Roomcut targets macOS only.")
endif()

# Shared protocol headers (driver + engine + core all include these).
add_library(roomcut_protocol INTERFACE)
target_include_directories(roomcut_protocol INTERFACE
  ${CMAKE_SOURCE_DIR}/shared/protocol)

# RoomcutCore — DSP / presets / AI schema (header-first; sources added per phase).
add_library(RoomcutCore INTERFACE)
target_include_directories(RoomcutCore INTERFACE
  ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(RoomcutCore INTERFACE roomcut_protocol)

# Subcomponents are added as each phase lands its first buildable target.
# add_subdirectory(core/tests)          # Phase 6 (built under ROOMCUT_BUILD_TESTS)

option(ROOMCUT_BUILD_TESTS "Build Roomcut unit tests + dev verification tools" OFF)
if(ROOMCUT_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(driver/RoomcutHAL)     # Phase 1
add_subdirectory(engine)                # Phase 3 (driver-sim built under ROOMCUT_BUILD_TESTS)

if(ROOMCUT_BUILD_TESTS)
  add_subdirectory(core/tests)
endif()
