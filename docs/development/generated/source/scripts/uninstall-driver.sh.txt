#!/usr/bin/env bash
# uninstall-driver.sh — remove Roomcut.driver and restore default audio output.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

HAL_DIR="/Library/Audio/Plug-Ins/HAL"
DRIVER_NAME="Roomcut.driver"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "error: run with sudo" >&2
  exit 1
fi

"${SCRIPT_DIR}/uninstall-engine.sh"

if [[ -d "${HAL_DIR}/${DRIVER_NAME}" ]]; then
  echo "Removing ${HAL_DIR}/${DRIVER_NAME}"
  rm -rf "${HAL_DIR:?}/${DRIVER_NAME}"
else
  echo "Roomcut.driver not present; nothing to remove."
fi

echo "Restarting coreaudiod (macOS will fall back to a real output device)..."
"${SCRIPT_DIR}/restart-coreaudiod.sh"

echo "Done. If output is silent, set a device manually in System Settings > Sound."
