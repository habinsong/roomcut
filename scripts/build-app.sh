#!/usr/bin/env bash
# build-app.sh — build the native menu-bar app (SwiftPM) and assemble
# build/Roomcut.app. LSUIElement makes it menu-bar-only (no Dock icon);
# ad-hoc signed for local use (Developer ID + notarization is the release
# pipeline, docs/07).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG="${1:-release}"

# The app targets macOS 26 and uses the Liquid Glass APIs (glassEffect, …), so
# it MUST build with a toolchain whose SDK is 26+. The default CommandLineTools
# Swift is older and cannot compile these paths — require Xcode 26 unless the
# caller already set a 26+ DEVELOPER_DIR.
if [[ -z "${DEVELOPER_DIR:-}" ]]; then
  CUR_SDK="$(xcrun --show-sdk-version 2>/dev/null || echo 0)"
  if [[ "${CUR_SDK%%.*}" -lt 26 ]]; then
    if [[ -d "/Applications/Xcode.app/Contents/Developer" ]] \
       && [[ "$(DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun --show-sdk-version 2>/dev/null | cut -d. -f1)" -ge 26 ]]; then
      export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
      echo "build-app: using Xcode SDK $(xcrun --show-sdk-version) (Liquid Glass)"
    else
      echo "build-app: ERROR — need a macOS 26+ SDK (Xcode 26) for the Liquid Glass build." >&2
      echo "  Active SDK is ${CUR_SDK}. Install Xcode 26 or set DEVELOPER_DIR to a 26+ toolchain." >&2
      exit 1
    fi
  fi
fi

swift build -c "${CONFIG}" --package-path "${REPO_ROOT}"
BIN="$(swift build -c "${CONFIG}" --package-path "${REPO_ROOT}" --show-bin-path)/Roomcut"

APP="${REPO_ROOT}/build/Roomcut.app"
rm -rf "${APP}"
mkdir -p "${APP}/Contents/MacOS"
cp "${BIN}" "${APP}/Contents/MacOS/Roomcut"

cat > "${APP}/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.roomcut.app</string>
    <key>CFBundleName</key>
    <string>Roomcut</string>
    <key>CFBundleExecutable</key>
    <string>Roomcut</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIconName</key>
    <string>AppIcon</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.7</string>
    <key>LSMinimumSystemVersion</key>
    <string>26.0</string>
    <key>LSUIElement</key>
    <true/>
    <key>NSMicrophoneUsageDescription</key>
    <string>Room Tune이 iPhone 마이크로 방의 음향을 측정합니다.</string>
</dict>
</plist>
PLIST

# Now Playing helper: compile the ObjC dylib and bundle it with the perl
# launcher under Resources/. The dylib resolves the private MediaRemote
# framework at runtime inside a child /usr/bin/perl process — the main app
# binary never links it. Sign the dylib ad-hoc; perl is not hardened so it
# loads ad-hoc dylibs without a library-validation failure (NP-2 verified).
NP_DIR="${REPO_ROOT}/apps/macos/NowPlayingHelper"
RES="${APP}/Contents/Resources"
mkdir -p "${RES}"
clang -dynamiclib -fobjc-arc -O2 \
  -framework Foundation -framework AppKit -framework CoreFoundation \
  "${NP_DIR}/RoomcutNowPlaying.m" \
  -o "${RES}/RoomcutNowPlaying.dylib"
cp "${NP_DIR}/roomcut-nowplaying.pl" "${RES}/roomcut-nowplaying.pl"
codesign --force --sign - "${RES}/RoomcutNowPlaying.dylib"

# App icon: build a multi-resolution AppIcon.icns from icon/roomcut_icon.png.
# The source is already a rounded "squircle" with transparent margins (macOS
# icon shape), so we only downscale into the standard iconset sizes.
ICON_SRC="${REPO_ROOT}/icon/roomcut_icon.png"
if [[ -f "${ICON_SRC}" ]]; then
  ICONSET="$(mktemp -d)/AppIcon.iconset"
  mkdir -p "${ICONSET}"
  for size in 16 32 128 256 512; do
    sips -z "${size}" "${size}" "${ICON_SRC}" --out "${ICONSET}/icon_${size}x${size}.png" >/dev/null
    sips -z "$((size * 2))" "$((size * 2))" "${ICON_SRC}" --out "${ICONSET}/icon_${size}x${size}@2x.png" >/dev/null
  done
  iconutil -c icns "${ICONSET}" -o "${RES}/AppIcon.icns"
  rm -rf "$(dirname "${ICONSET}")"
else
  echo "build-app: note — ${ICON_SRC} not found; building without an app icon." >&2
fi

codesign --force --sign - "${APP}"
echo "Assembled ${APP}"
