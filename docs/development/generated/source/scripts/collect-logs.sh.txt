#!/usr/bin/env bash
# collect-logs.sh — gather Roomcut diagnostics into a single archive for bug reports.
set -euo pipefail

OUT="roomcut-logs-$(date +%Y%m%d-%H%M%S)"
mkdir -p "${OUT}"

echo "Collecting audio device list..."
system_profiler SPAudioDataType > "${OUT}/audio-devices.txt" 2>&1 || true

echo "Collecting HAL plug-in listing..."
ls -la /Library/Audio/Plug-Ins/HAL > "${OUT}/hal-plugins.txt" 2>&1 || true

echo "Collecting coreaudiod / Roomcut log excerpts (last 10 min)..."
log show --last 10m --predicate 'process == "coreaudiod" OR senderImagePath CONTAINS "Roomcut"' \
  > "${OUT}/system-log.txt" 2>&1 || true

echo "Collecting engine logs..."
cp -f "/Library/Logs/Roomcut/"*.log "${OUT}/" 2>/dev/null || true

tar -czf "${OUT}.tar.gz" "${OUT}"
rm -rf "${OUT}"
echo "Wrote ${OUT}.tar.gz"
