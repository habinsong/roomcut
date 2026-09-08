#!/usr/bin/env bash
# install.sh — install Roomcut from the prebuilt artifacts bundled next to this
# script (the GitHub release zip). No building, no Xcode. Run with sudo:
#
#     sudo ./install.sh
#
# Installs: Roomcut.app -> /Applications, Roomcut.driver -> the system HAL dir,
# the engine + a system LaunchDaemon, then restarts coreaudiod so "Roomcut
# Output" appears. The engine runs always-on (no sudoers rule).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LABEL="com.roomcut.engine"
PLIST="/Library/LaunchDaemons/${LABEL}.plist"
INSTALL_ROOT="/Library/Application Support/Roomcut"
BIN_DIR="${INSTALL_ROOT}/bin"
LOG_DIR="/Library/Logs/Roomcut"
HAL_DIR="/Library/Audio/Plug-Ins/HAL"
APP_DEST="/Applications/Roomcut.app"

APP_SRC="${HERE}/Roomcut.app"
DRIVER_SRC="${HERE}/Roomcut.driver"
ENGINE_SRC="${HERE}/RoomcutAudioEngine"
CTL_SRC="${HERE}/roomcutctl"
DEVICECTL_SRC="${HERE}/roomcut-devicectl"
PLIST_SRC="${HERE}/com.roomcut.engine.plist"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "error: run with sudo  →  sudo ./install.sh" >&2
  exit 1
fi

for p in "${APP_SRC}" "${DRIVER_SRC}" "${ENGINE_SRC}" "${PLIST_SRC}"; do
  if [[ ! -e "${p}" ]]; then
    echo "error: missing artifact next to install.sh: ${p}" >&2
    echo "       extract the full release zip and run install.sh from inside it." >&2
    exit 1
  fi
done

echo "Stopping any running Roomcut engine…"
/bin/launchctl bootout "system/${LABEL}" 2>/dev/null || true

echo "Installing app → ${APP_DEST}"
rm -rf "${APP_DEST}"
cp -R "${APP_SRC}" "${APP_DEST}"

echo "Installing driver → ${HAL_DIR}/Roomcut.driver"
mkdir -p "${HAL_DIR}"
rm -rf "${HAL_DIR:?}/Roomcut.driver"
cp -R "${DRIVER_SRC}" "${HAL_DIR}/Roomcut.driver"
chown -R root:wheel "${HAL_DIR}/Roomcut.driver"

echo "Installing engine → ${BIN_DIR}"
mkdir -p "${BIN_DIR}" "${LOG_DIR}"
cp "${ENGINE_SRC}" "${BIN_DIR}/RoomcutAudioEngine"
[[ -e "${CTL_SRC}" ]] && cp "${CTL_SRC}" "${BIN_DIR}/roomcutctl" || true
[[ -e "${DEVICECTL_SRC}" ]] && cp "${DEVICECTL_SRC}" "${BIN_DIR}/roomcut-devicectl" || true
chown -R root:wheel "${INSTALL_ROOT}"
chmod 0755 "${BIN_DIR}"/*
chown root:wheel "${LOG_DIR}"

echo "Installing LaunchDaemon → ${PLIST}"
cp "${PLIST_SRC}" "${PLIST}"
chown root:wheel "${PLIST}"
chmod 0644 "${PLIST}"
plutil -lint "${PLIST}" >/dev/null

# Keep an uninstaller next to the install so users can remove it later.
[[ -e "${HERE}/uninstall.sh" ]] && cp "${HERE}/uninstall.sh" "${INSTALL_ROOT}/uninstall.sh" || true

# Files unzipped from a GitHub download carry the quarantine xattr — strip it so
# the ad-hoc-signed driver loads and the app opens without a Gatekeeper prompt.
echo "Clearing quarantine…"
xattr -dr com.apple.quarantine "${APP_DEST}" 2>/dev/null || true
xattr -dr com.apple.quarantine "${HAL_DIR}/Roomcut.driver" 2>/dev/null || true
xattr -dr com.apple.quarantine "${BIN_DIR}"/* 2>/dev/null || true

echo "Loading engine daemon…"
/bin/launchctl enable "system/${LABEL}" 2>/dev/null || true
loaded=0
for attempt in 1 2 3; do
  /bin/launchctl bootstrap system "${PLIST}" 2>/dev/null || true
  sleep 1
  if /bin/launchctl print "system/${LABEL}" >/dev/null 2>&1; then
    loaded=1
    break
  fi
  sleep $((attempt))
done
[[ "${loaded}" -eq 1 ]] || echo "warning: engine daemon did not report loaded; check ${LOG_DIR}/engine.err.log" >&2

echo "Restarting coreaudiod (system audio glitches for ~1s)…"
killall -9 coreaudiod 2>/dev/null || true
sleep 2

# Best-effort control-plane check.
if [[ -x "${BIN_DIR}/roomcutctl" ]]; then
  if "${BIN_DIR}/roomcutctl" status --json 2>/dev/null | grep -q '"engineReachable":true'; then
    echo "Engine reachable. OK."
  else
    echo "note: engine not reachable yet — it may settle a moment after coreaudiod restarts."
  fi
fi

echo
echo "Done. Open Roomcut from /Applications (menu bar), then pick 'Roomcut Output'"
echo "in System Settings ▸ Sound, or let the app set it."
echo "To remove later:  sudo \"${INSTALL_ROOT}/uninstall.sh\""
