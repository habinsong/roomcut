# Roomcut.driver — the Audio Server Plug-in (HAL bundle).
#
# Produces build/driver/RoomcutHAL/Roomcut.driver, a .driver bundle laid out as
#   Roomcut.driver/Contents/Info.plist
#   Roomcut.driver/Contents/MacOS/Roomcut   (the loadable module)
# and ad-hoc code-signs it (a real Developer ID signature is applied at release;
# see docs/07-install-uninstall.md — signing is mandatory on modern macOS).

add_library(RoomcutHAL MODULE
  src/RoomcutPlugIn.c
  src/RoomcutProperties.c
  src/RoomcutIO.c
  src/RoomcutTransport.c)

target_include_directories(RoomcutHAL PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(RoomcutHAL PRIVATE roomcut_protocol)

find_library(CoreFoundation_FRAMEWORK CoreFoundation REQUIRED)
find_library(CoreAudio_FRAMEWORK CoreAudio REQUIRED)
target_link_libraries(RoomcutHAL PRIVATE
  ${CoreFoundation_FRAMEWORK}
  ${CoreAudio_FRAMEWORK})

# Treat warnings seriously in the sandbox-critical driver.
target_compile_options(RoomcutHAL PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Phase 2 verification: debug-only IO instrumentation (RMS/peak via throttled
# os_log). OFF by default — driver-side RMS/peak must never ship (docs decision).
option(ROOMCUT_DRIVER_DEBUG "Build the driver with debug-only IO instrumentation" OFF)
if(ROOMCUT_DRIVER_DEBUG)
  target_compile_definitions(RoomcutHAL PRIVATE ROOMCUT_DRIVER_DEBUG=1)
endif()

# Lay the module out as a proper .driver bundle.
set_target_properties(RoomcutHAL PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION "driver"
  OUTPUT_NAME "Roomcut"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  # CFBundlePackageType BNDL (not the default APPL) for a plug-in bundle.
  MACOSX_BUNDLE_BUNDLE_NAME "Roomcut")

# Ad-hoc sign the finished bundle so coreaudiod will at least load it locally.
# Override ROOMCUT_CODESIGN_IDENTITY with a Developer ID for distribution.
set(ROOMCUT_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for Roomcut.driver")
add_custom_command(TARGET RoomcutHAL POST_BUILD
  COMMAND codesign --force --sign "${ROOMCUT_CODESIGN_IDENTITY}"
          --timestamp=none "$<TARGET_BUNDLE_DIR:RoomcutHAL>"
  COMMENT "Code-signing Roomcut.driver (identity: ${ROOMCUT_CODESIGN_IDENTITY})"
  VERBATIM)
