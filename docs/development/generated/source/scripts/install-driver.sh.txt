#!/usr/bin/env bash
# install-driver.sh — install Roomcut.driver into the system HAL plug-in dir.
# Must run as root (uses sudo internally where needed). The driver bundle must
# already be built and code-signed; an unsigned driver will be refused by
# coreaudiod on modern macOS.
set -euo pipefail

# Resolve the repo root from this script's own location so the default bundle
# path works no matter what directory sudo was invoked from.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

HAL_DIR="/Library/Audio/Plug-Ins/HAL"
DRIVER_NAME="Roomcut.driver"
BUILD_DRIVER="${1:-${REPO_ROOT}/build/driver/RoomcutHAL/${DRIVER_NAME}}"
BUILD_ENGINE="${2:-${REPO_ROOT}/build/engine/RoomcutAudioEngine}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "error: run with sudo (writing to ${HAL_DIR})" >&2
  exit 1
fi

if [[ ! -d "${BUILD_DRIVER}" ]]; then
  echo "error: driver bundle not found at ${BUILD_DRIVER}" >&2
  echo "build it first: cmake -S \"${REPO_ROOT}\" -B \"${REPO_ROOT}/build\" && cmake --build \"${REPO_ROOT}/build\"" >&2
  exit 1
fi
if [[ ! -x "${BUILD_ENGINE}" ]]; then
  echo "error: engine binary not found at ${BUILD_ENGINE}" >&2
  exit 1
fi

# Save the user's current default output device so uninstall can restore it.
STATE_DIR="/Library/Application Support/Roomcut"
mkdir -p "${STATE_DIR}"

echo "Installing ${DRIVER_NAME} -> ${HAL_DIR}"
rm -rf "${HAL_DIR:?}/${DRIVER_NAME}"
cp -R "${BUILD_DRIVER}" "${HAL_DIR}/${DRIVER_NAME}"
chown -R root:wheel "${HAL_DIR}/${DRIVER_NAME}"

"${SCRIPT_DIR}/install-engine.sh" "${BUILD_ENGINE}"

echo "Restarting coreaudiod..."
"${SCRIPT_DIR}/restart-coreaudiod.sh"

echo "Done. Driver and system engine service are installed."
echo "Check: System Settings > Sound > Output for 'Roomcut Output'."
echo "Or run: system_profiler SPAudioDataType | grep -A2 Roomcut"
