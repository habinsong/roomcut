#!/usr/bin/env bash
# uninstall.sh — remove Roomcut and restore normal audio output. Run with sudo:
#
#     sudo ./uninstall.sh
#
# Self-contained (works whether run from the release zip or from the installed
# copy at /Library/Application Support/Roomcut/uninstall.sh).
set -euo pipefail

LABEL="com.roomcut.engine"
PLIST="/Library/LaunchDaemons/${LABEL}.plist"
INSTALL_ROOT="/Library/Application Support/Roomcut"
HAL_DRIVER="/Library/Audio/Plug-Ins/HAL/Roomcut.driver"
APP="/Applications/Roomcut.app"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "error: run with sudo  →  sudo ./uninstall.sh" >&2
  exit 1
fi

echo "Stopping engine daemon…"
/bin/launchctl bootout "system/${LABEL}" 2>/dev/null || true
/bin/launchctl enable "system/${LABEL}" 2>/dev/null || true   # clear any disabled override

# Drop a dev-install sudoers rule if one is present (no-op for pkg/zip installs).
rm -f /etc/sudoers.d/roomcut-engine 2>/dev/null || true

echo "Removing files…"
rm -f "${PLIST}"
rm -rf "${HAL_DRIVER}"
rm -rf "${APP}"
# Leave logs; remove binaries + state. Keep this uninstaller until the very end.
rm -rf "${INSTALL_ROOT}/bin" "${INSTALL_ROOT}/engine.state"

echo "Restarting coreaudiod (macOS falls back to a real output device)…"
killall -9 coreaudiod 2>/dev/null || true
sleep 2

echo "Done. If sound stays silent, pick an output in System Settings ▸ Sound."
