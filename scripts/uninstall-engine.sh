#!/usr/bin/env bash
set -euo pipefail

LABEL="com.roomcut.engine"
INSTALLED_PLIST="/Library/LaunchDaemons/${LABEL}.plist"
ENGINE_BINARY="/Library/Application Support/Roomcut/bin/RoomcutAudioEngine"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "error: run with sudo" >&2
  exit 1
fi

launchctl bootout "system/${LABEL}" 2>/dev/null || true
# Clear any persistent "disabled" override left by the app-controlled lifecycle,
# and drop the passwordless-launchctl rule the installer added.
launchctl enable "system/${LABEL}" 2>/dev/null || true
rm -f "/etc/sudoers.d/roomcut-engine"
rm -f "${INSTALLED_PLIST}" "${ENGINE_BINARY}"

if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
  USER_UID="$(id -u "${SUDO_USER}")"
  USER_HOME="$(dscl . -read "/Users/${SUDO_USER}" NFSHomeDirectory | awk '{print $2}')"
  launchctl bootout "gui/${USER_UID}/${LABEL}" 2>/dev/null || true
  rm -f "${USER_HOME}/Library/LaunchAgents/${LABEL}.plist"
fi

echo "Engine LaunchDaemon ${LABEL} unloaded."
