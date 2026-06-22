#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ENGINE_BIN="${REPO_ROOT}/build/engine/RoomcutAudioEngine"
SIM_BIN="${REPO_ROOT}/build/engine/roomcut-driver-sim"
ENGINE_LOG="/tmp/roomcut-engine.err.log"
SECONDS_TO_STREAM="${1:-3}"

for b in "${ENGINE_BIN}" "${SIM_BIN}"; do
  if [[ ! -x "${b}" ]]; then
    echo "error: ${b} not built. Run: cmake -S \"${REPO_ROOT}\" -B \"${REPO_ROOT}/build\" -DROOMCUT_BUILD_TESTS=ON && cmake --build \"${REPO_ROOT}/build\"" >&2
    exit 1
  fi
done

if launchctl print "system/com.roomcut.engine" >/dev/null 2>&1 ||
   launchctl print "gui/$(id -u)/com.roomcut.engine" >/dev/null 2>&1; then
  echo "error: stop the installed Roomcut engine service before running this development check" >&2
  exit 1
fi

echo "== starting engine (development bootstrap_register path) =="
: > "${ENGINE_LOG}" 2>/dev/null || true
"${ENGINE_BIN}" 2>"${ENGINE_LOG}" &
ENGINE_PID=$!

cleanup() {
  kill "${ENGINE_PID}" 2>/dev/null || true
  wait "${ENGINE_PID}" 2>/dev/null || true
}
trap cleanup EXIT

sleep 1
echo "== streaming ${SECONDS_TO_STREAM}s from driver-sim (separate process) =="
"${SIM_BIN}" "${SECONDS_TO_STREAM}"

sleep 1
echo "== engine log =="
cat "${ENGINE_LOG}" 2>/dev/null || echo "(no engine log at ${ENGINE_LOG})"

echo "== assertions =="
fail=0

if ! grep -q "bootstrap_register OK" "${ENGINE_LOG}" 2>/dev/null; then
  echo "FAIL: engine did not register the development Mach service"
  fail=1
else
  echo "PASS: engine registered the development Mach service"
fi

if ! grep -q "handed off region" "${ENGINE_LOG}" 2>/dev/null; then
  echo "FAIL: engine never handed off the ring region"
  fail=1
else
  echo "PASS: HELLO handshake + memory-entry handoff completed"
fi

# Last ring line: peak should be ~0.25 (sim amplitude), overruns/underruns 0.
last="$(grep "ring:" "${ENGINE_LOG}" 2>/dev/null | tail -1 || true)"
if [[ -z "${last}" ]]; then
  echo "FAIL: engine reader produced no ring stats (no frames crossed the boundary)"
  fail=1
else
  echo "ring line: ${last}"
  if echo "${last}" | grep -q "peak=0.25000" && \
     echo "${last}" | grep -q "over=0 under=0" && \
     echo "${last}" | grep -q "NON-SILENCE"; then
    echo "PASS: exact signal received cross-process (peak=0.25, 0 over/underruns)"
  else
    echo "FAIL: ring stats did not match the expected clean transfer"
    fail=1
  fi
fi

if [[ "${fail}" -eq 0 ]]; then
  echo "== verify-transfer: ALL PASS =="
else
  echo "== verify-transfer: FAILURES (see above) =="
fi
exit "${fail}"
