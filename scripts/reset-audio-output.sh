#!/usr/bin/env bash
# reset-audio-output.sh — emergency recovery: force macOS to a real output device.
# Use when Roomcut Output is selected but no engine is running (silent system).
set -euo pipefail

echo "Restarting coreaudiod to clear stuck audio routing..."
sudo killall -9 coreaudiod 2>/dev/null || true
sleep 2
echo "Now open System Settings > Sound > Output and pick a hardware device"
echo "(e.g. MacBook Speakers) if sound has not returned automatically."
