#!/usr/bin/env bash
# restart-coreaudiod.sh — restart the CoreAudio daemon so HAL plug-ins reload.
# This briefly interrupts ALL system audio. Required after install/uninstall.
set -euo pipefail

if [[ "$(id -u)" -ne 0 ]]; then
  echo "error: run with sudo" >&2
  exit 1
fi

echo "Restarting coreaudiod (system audio will glitch for ~1s)..."
killall -9 coreaudiod 2>/dev/null || true
# launchd relaunches coreaudiod automatically; give it a moment.
sleep 2
echo "coreaudiod restarted."
