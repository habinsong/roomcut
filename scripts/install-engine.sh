#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

LABEL="com.roomcut.engine"
PLIST_TEMPLATE="${SCRIPT_DIR}/${LABEL}.plist"
INSTALLED_PLIST="/Library/LaunchDaemons/${LABEL}.plist"
INSTALL_ROOT="/Library/Application Support/Roomcut"
ENGINE_DIR="${INSTALL_ROOT}/bin"
ENGINE_BINARY="${ENGINE_DIR}/RoomcutAudioEngine"
STATE_FILE="${INSTALL_ROOT}/engine.state"
LOG_DIR="/Library/Logs/Roomcut"
OUT_LOG="${LOG_DIR}/engine.out.log"
ERR_LOG="${LOG_DIR}/engine.err.log"
SOURCE_BINARY="${1:-${REPO_ROOT}/build/engine/RoomcutAudioEngine}"
DOMAIN="system"
SERVICE_TARGET="${DOMAIN}/${LABEL}"
CTL="${REPO_ROOT}/build/engine/roomcutctl"
DEVICECTL="${REPO_ROOT}/build/engine/roomcut-devicectl"
# Lets the menu-bar app start/stop the engine on its own launch/quit without a
# password (the daemon lives in the system domain, so launchctl needs root).
SUDOERS_FILE="/etc/sudoers.d/roomcut-engine"
LAUNCHCTL="/bin/launchctl"
ROOMCUT_CAP_SPATIAL_PARAMS=1
ROOMCUT_CAP_ANALYZER=4

# User-context audio state captured before the old service is torn down, so a
# reinstall does not silently reset what the user was running (2026-06-13).
ORIG_DEFAULT_UID=""
ORIG_PRESET=""

if [[ "$(id -u)" -ne 0 ]]; then
  echo "error: run with sudo (installing a system LaunchDaemon)" >&2
  exit 1
fi

if [[ ! -x "${SOURCE_BINARY}" ]]; then
  echo "error: engine binary not found/executable at ${SOURCE_BINARY}" >&2
  echo "build it first: cmake -S \"${REPO_ROOT}\" -B \"${REPO_ROOT}/build\" && cmake --build \"${REPO_ROOT}/build\"" >&2
  exit 1
fi

if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != "root" ]]; then
  USER_UID="$(id -u "${SUDO_USER}")"
  USER_HOME="$(dscl . -read "/Users/${SUDO_USER}" NFSHomeDirectory | awk '{print $2}')"
  launchctl bootout "gui/${USER_UID}/${LABEL}" 2>/dev/null || true
  rm -f "${USER_HOME}/Library/LaunchAgents/${LABEL}.plist"
fi

mkdir -p "${ENGINE_DIR}" "${LOG_DIR}"
touch "${OUT_LOG}" "${ERR_LOG}"
chown root:wheel "${OUT_LOG}" "${ERR_LOG}"
chmod 0644 "${OUT_LOG}" "${ERR_LOG}"

TMP_PLIST="$(mktemp "/tmp/${LABEL}.XXXXXX")"
TMP_BIN="$(mktemp "/tmp/${LABEL}.bin.XXXXXX")"
BACKUP_PLIST=
BACKUP_BIN=
trap 'rm -f "${TMP_PLIST}" "${TMP_BIN}" "${BACKUP_PLIST:-}" "${BACKUP_BIN:-}"' EXIT
sed \
  -e "s|__ENGINE_BINARY__|${ENGINE_BINARY}|g" \
  -e "s|__STATE_FILE__|${STATE_FILE}|g" \
  -e "s|__OUT_LOG__|${OUT_LOG}|g" \
  -e "s|__ERR_LOG__|${ERR_LOG}|g" \
  "${PLIST_TEMPLATE}" > "${TMP_PLIST}"
plutil -lint "${TMP_PLIST}" >/dev/null
cp "${SOURCE_BINARY}" "${TMP_BIN}"
chown root:wheel "${TMP_BIN}"
chmod 0755 "${TMP_BIN}"

if [[ -f "${INSTALLED_PLIST}" ]]; then
  BACKUP_PLIST="$(mktemp "/tmp/${LABEL}.plist.backup.XXXXXX")"
  cp "${INSTALLED_PLIST}" "${BACKUP_PLIST}"
fi
if [[ -f "${ENGINE_BINARY}" ]]; then
  BACKUP_BIN="$(mktemp "/tmp/${LABEL}.bin.backup.XXXXXX")"
  cp "${ENGINE_BINARY}" "${BACKUP_BIN}"
fi

service_pid() {
  launchctl print "${SERVICE_TARGET}" 2>/dev/null | awk '
    $1 == "pid" && $2 == "=" {
      print $3
      exit
    }
  '
}

wait_for_pid_exit() {
  local pid="$1"
  local checks="$2"
  local i

  if [[ -z "${pid}" ]]; then
    return 0
  fi

  for ((i = 0; i < checks; i++)); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done

  return 1
}

terminate_stale_pid() {
  local pid="$1"

  if [[ -z "${pid}" ]] || ! kill -0 "${pid}" 2>/dev/null; then
    return 0
  fi

  echo "install-engine: previous ${LABEL} pid ${pid} is still alive after bootout; terminating" >&2
  kill -TERM "${pid}" 2>/dev/null || true
  if wait_for_pid_exit "${pid}" 30; then
    return 0
  fi

  echo "install-engine: previous ${LABEL} pid ${pid} ignored SIGTERM; killing" >&2
  kill -KILL "${pid}" 2>/dev/null || true
  if wait_for_pid_exit "${pid}" 30; then
    return 0
  fi

  echo "error: previous ${LABEL} pid ${pid} is still alive after SIGKILL" >&2
  ps -p "${pid}" -o pid,ppid,user,stat,etime,command >&2 || true
  return 1
}

unload_existing_service() {
  local pid
  pid="$(service_pid || true)"
  launchctl bootout "${SERVICE_TARGET}" 2>/dev/null || true
  launchctl bootout "${DOMAIN}" "${INSTALLED_PLIST}" 2>/dev/null || true
  terminate_stale_pid "${pid}"
}

restore_previous() {
  echo "install-engine: restoring previous service after failure" >&2
  unload_existing_service || true
  if [[ -n "${BACKUP_BIN:-}" && -f "${BACKUP_BIN}" ]]; then
    cp "${BACKUP_BIN}" "${ENGINE_BINARY}"
    chown root:wheel "${ENGINE_BINARY}"
    chmod 0755 "${ENGINE_BINARY}"
  fi
  if [[ -n "${BACKUP_PLIST:-}" && -f "${BACKUP_PLIST}" ]]; then
    cp "${BACKUP_PLIST}" "${INSTALLED_PLIST}"
    chown root:wheel "${INSTALLED_PLIST}"
    chmod 0644 "${INSTALLED_PLIST}"
    launchctl bootstrap "${DOMAIN}" "${INSTALLED_PLIST}" 2>/dev/null || true
    launchctl enable "${SERVICE_TARGET}" 2>/dev/null || true
  fi
}

diagnose_bootstrap_failure() {
  local rc="$1"
  echo "install-engine: launchctl bootstrap failed (${rc})" >&2
  launchctl error "${rc}" >&2 || true
  echo "install-engine: plist ${INSTALLED_PLIST}" >&2
  plutil -lint "${INSTALLED_PLIST}" >&2 || true
  ls -lOe@ "${INSTALLED_PLIST}" "${ENGINE_BINARY}" >&2 || true
  codesign --verify --strict --verbose=2 "${ENGINE_BINARY}" >&2 || true
  echo "install-engine: recent launchd diagnostics" >&2
  log show --style compact --last 2m \
    --predicate 'process == "launchd" AND (eventMessage CONTAINS[c] "com.roomcut.engine" OR eventMessage CONTAINS[c] "RoomcutAudioEngine")' \
    2>/dev/null | tail -80 >&2 || true
}

diagnose_control_failure() {
  echo "install-engine: service diagnostics" >&2
  launchctl print "${SERVICE_TARGET}" >&2 || true
  echo "install-engine: recent engine stderr" >&2
  tail -80 "${ERR_LOG}" >&2 || true
}

verify_control_plane() {
  local status
  local params

  if [[ ! -x "${CTL}" ]]; then
    echo "install-engine: skipping control-plane check; ${CTL} is not executable" >&2
    return 0
  fi

  if ! status="$("${CTL}" status --json 2>&1)"; then
    echo "error: ${LABEL} did not answer status after launch" >&2
    echo "${status}" >&2
    return 1
  fi
  if [[ "${status}" != *'"engineReachable":true'* ]]; then
    echo "error: ${LABEL} loaded but control plane is not reachable" >&2
    echo "${status}" >&2
    return 1
  fi
  local caps
  caps="$(sed -n 's/.*"capabilities":\([0-9][0-9]*\).*/\1/p' <<<"${status}")"
  if [[ -z "${caps}" || $((caps & ROOMCUT_CAP_SPATIAL_PARAMS)) -eq 0 ]]; then
    echo "error: ${LABEL} control plane does not expose Spatial support" >&2
    echo "${status}" >&2
    return 1
  fi
  if [[ $((caps & ROOMCUT_CAP_ANALYZER)) -eq 0 ]]; then
    echo "error: ${LABEL} control plane does not expose Analyzer support" >&2
    echo "${status}" >&2
    return 1
  fi

  if ! params="$("${CTL}" params get --json 2>&1)"; then
    echo "error: ${LABEL} status works but params get failed" >&2
    echo "${params}" >&2
    return 1
  fi
  if [[ "${params}" != *'"eqGainsDb":['* ]]; then
    echo "error: ${LABEL} params get returned an unexpected payload" >&2
    echo "${params}" >&2
    return 1
  fi
}

# Run a command as the invoking (GUI) user. The default output device is
# per-user coreaudiod state, so devicectl must not run as root.
run_as_user() {
  launchctl asuser "${USER_UID}" sudo -u "${SUDO_USER}" "$@"
}

# Snapshot what the user was running BEFORE the old service is torn down:
# the default output device and the live preset/params. A fresh engine always
# boots flat, and its 3 s startup-restore window (docs/05) flips the default
# off Roomcut when the driver's HELLO arrives late — without this, every
# reinstall silently dropped the EQ path (2026-06-13).
capture_audio_state() {
  if [[ -z "${SUDO_USER:-}" || "${SUDO_USER}" == "root" || ! -x "${DEVICECTL}" ]]; then
    return 0
  fi
  ORIG_DEFAULT_UID="$(run_as_user "${DEVICECTL}" get 2>/dev/null || true)"

  if [[ ! -x "${CTL}" ]]; then
    return 0
  fi
  local status
  status="$("${CTL}" status --json 2>/dev/null || true)"
  if [[ "${status}" != *'"engineReachable":true'* ]]; then
    return 0
  fi
  ORIG_PRESET="$(sed -n 's/.*"preset":"\([^"]*\)".*/\1/p' <<<"${status}")"
}

restore_audio_state() {
  if [[ -n "${ORIG_PRESET}" && "${ORIG_PRESET}" != "flat" && -x "${CTL}" ]]; then
    if [[ "${ORIG_PRESET}" == "custom" ]]; then
      # A custom set resumes from the engine's own state file (kept across the
      # reinstall) — it carries the FULL set (EQ, parametric, spatial mode,
      # dynamics), which a CLI reapply cannot: pushing a partial params set here
      # would overwrite the resumed state and silently drop the rest.
      echo "Params: custom set resumes from ${STATE_FILE}."
    elif "${CTL}" preset "${ORIG_PRESET}" >/dev/null 2>&1; then
      echo "Preset: '${ORIG_PRESET}' reapplied."
    else
      echo "install-engine: warning: could not reapply preset '${ORIG_PRESET}'" >&2
    fi
  fi

  if [[ "${ORIG_DEFAULT_UID}" != RoomcutOutput:* || ! -x "${DEVICECTL}" ||
        -z "${SUDO_USER:-}" || "${SUDO_USER}" == "root" ]]; then
    return 0
  fi
  sleep 3   # let the engine's startup-restore window pass so it can't undo us
  local now=""
  local attempt
  for attempt in 1 2; do
    run_as_user "${DEVICECTL}" set "${ORIG_DEFAULT_UID}" 2>/dev/null || true
    sleep 1
    now="$(run_as_user "${DEVICECTL}" get 2>/dev/null || true)"
    if [[ "${now}" == "${ORIG_DEFAULT_UID}" ]]; then
      echo "Default output: restored to Roomcut (${ORIG_DEFAULT_UID})."
      return 0
    fi
  done
  echo "install-engine: warning: default output is '${now:-unknown}', expected '${ORIG_DEFAULT_UID}' — select Roomcut Output manually" >&2
}

# Drop a tightly-scoped sudoers rule so the app can run exactly the four
# launchctl verbs it needs on this one service — nothing else. Validated with
# `visudo -c` before it is installed so a bad rule never lands.
install_sudoers() {
  if [[ -z "${SUDO_USER:-}" || "${SUDO_USER}" == "root" ]]; then
    echo "install-engine: no SUDO_USER; skipping app-controlled engine setup" >&2
    return 0
  fi
  local tmp
  tmp="$(mktemp /tmp/roomcut-sudoers.XXXXXX)"
  cat > "${tmp}" <<EOF
# Roomcut: allow the menu-bar app to start/stop the engine LaunchDaemon without a
# password. Installed by scripts/install-engine.sh, removed by uninstall-engine.sh.
${SUDO_USER} ALL=(root) NOPASSWD: ${LAUNCHCTL} enable ${SERVICE_TARGET}, ${LAUNCHCTL} bootstrap ${DOMAIN} ${INSTALLED_PLIST}, ${LAUNCHCTL} bootout ${SERVICE_TARGET}, ${LAUNCHCTL} disable ${SERVICE_TARGET}
EOF
  chown root:wheel "${tmp}"
  chmod 0440 "${tmp}"
  if visudo -cf "${tmp}" >/dev/null 2>&1; then
    mv "${tmp}" "${SUDOERS_FILE}"
    echo "Sudoers: app may start/stop ${LABEL} without a password (${SUDOERS_FILE})."
  else
    rm -f "${tmp}"
    echo "install-engine: warning: generated sudoers failed visudo check; app-controlled engine NOT enabled" >&2
  fi
}

capture_audio_state
unload_existing_service
cp "${TMP_BIN}" "${ENGINE_BINARY}"
chown root:wheel "${ENGINE_BINARY}"
chmod 0755 "${ENGINE_BINARY}"
xattr -d com.apple.quarantine "${ENGINE_BINARY}" 2>/dev/null || true
cp "${TMP_PLIST}" "${INSTALLED_PLIST}"
chown root:wheel "${INSTALLED_PLIST}"
chmod 0644 "${INSTALLED_PLIST}"
# A prior install leaves the service DISABLED (app-controlled lifecycle keeps it
# from auto-starting at boot). `bootstrap` fails with "Input/output error (5)" on
# a disabled label, so clear that override BEFORE bootstrapping, not after.
launchctl enable "${SERVICE_TARGET}" 2>/dev/null || true
rc=1
for attempt in 1 2 3 4; do
  # `launchctl bootstrap` can print "Bootstrap failed: 5: Input/output error" yet
  # still exit 0, so don't trust its status — verify the label actually loaded.
  # The common cause on a REINSTALL is coreaudiod still holding the engine's Mach
  # name from the previous run; it releases it shortly after the bootout, so we
  # back off and retry rather than failing outright.
  launchctl bootstrap "${DOMAIN}" "${INSTALLED_PLIST}" 2>&1 | sed 's/^/  /' || true
  sleep 1
  if launchctl print "${SERVICE_TARGET}" >/dev/null 2>&1; then
    rc=0
    break
  fi
  echo "install-engine: bootstrap attempt ${attempt} did not load the service; retrying" >&2
  unload_existing_service || true
  sleep $(( attempt * 2 ))
done
if [[ "${rc}" -ne 0 ]]; then
  diagnose_bootstrap_failure 5
  restore_previous
  exit 1
fi
launchctl enable "${SERVICE_TARGET}" 2>/dev/null || true

sleep 1
if launchctl print "${SERVICE_TARGET}" >/dev/null 2>&1; then
  if ! verify_control_plane; then
    diagnose_control_failure
    restore_previous
    exit 1
  fi
  restore_audio_state
  # Hand the engine's lifecycle to the app: it stays running for THIS session,
  # but `disable` stops launchd from auto-starting it at the next boot, and the
  # sudoers rule lets the app start it on launch / stop it on quit. Quitting the
  # app boots it out (SIGTERM → the engine restores the real default output).
  install_sudoers
  if [[ -f "${SUDOERS_FILE}" ]]; then
    launchctl disable "${SERVICE_TARGET}" 2>/dev/null || true
    echo "Lifecycle: engine now follows the app (boots out on quit, starts on launch)."
  fi
  echo "LaunchDaemon ${LABEL} loaded."
  echo "Control: status and params get OK."
  echo "Engine: ${ENGINE_BINARY}"
  echo "Logs: ${OUT_LOG} ${ERR_LOG}"
else
  echo "error: ${LABEL} did not appear in the system launchd domain" >&2
  exit 1
fi
