#!/usr/bin/env bash
# package-release.sh — assemble GitHub release artifacts from the prebuilt
# components in build/. Produces BOTH:
#
#   dist/Roomcut-<ver>.pkg   double-click installer (postinstall loads the engine)
#   dist/Roomcut-<ver>.zip   prebuilt files + install.sh / uninstall.sh (terminal)
#
# Both ship the SAME ad-hoc-signed binaries and install the same layout; only the
# entry point differs. There is no Developer ID signing / notarization here — see
# the release notes for the Gatekeeper caveats that implies.
#
# Usage:
#   bash scripts/package-release.sh [version] [--build]
#     version   override CFBundleShortVersionString (default: read from the app)
#     --build   build the native components + app first (CMake + build-app.sh)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RELEASE_DIR="${SCRIPT_DIR}/release"

PKG_ID="com.roomcut.pkg"
VERSION=""
DO_BUILD=0
for arg in "$@"; do
  case "${arg}" in
    --build) DO_BUILD=1 ;;
    *)       VERSION="${arg}" ;;
  esac
done

APP_SRC="${REPO_ROOT}/build/Roomcut.app"
DRIVER_SRC="${REPO_ROOT}/build/driver/RoomcutHAL/Roomcut.driver"
ENGINE_SRC="${REPO_ROOT}/build/engine/RoomcutAudioEngine"
CTL_SRC="${REPO_ROOT}/build/engine/roomcutctl"
DEVICECTL_SRC="${REPO_ROOT}/build/engine/roomcut-devicectl"
PLIST_TEMPLATE="${SCRIPT_DIR}/com.roomcut.engine.plist"

if [[ "${DO_BUILD}" -eq 1 ]]; then
  echo "==> Building native components (CMake)…"
  cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" >/dev/null
  cmake --build "${REPO_ROOT}/build"
  echo "==> Building app (build-app.sh)…"
  bash "${SCRIPT_DIR}/build-app.sh" release
fi

missing=0
for p in "${APP_SRC}" "${DRIVER_SRC}" "${ENGINE_SRC}" "${PLIST_TEMPLATE}"; do
  if [[ ! -e "${p}" ]]; then echo "missing: ${p}" >&2; missing=1; fi
done
if [[ "${missing}" -eq 1 ]]; then
  echo "error: build artifacts not found. Build first, or pass --build:" >&2
  echo "  cmake -S \"${REPO_ROOT}\" -B \"${REPO_ROOT}/build\" && cmake --build \"${REPO_ROOT}/build\"" >&2
  echo "  bash scripts/build-app.sh release" >&2
  exit 1
fi

if [[ -z "${VERSION}" ]]; then
  VERSION="$(plutil -extract CFBundleShortVersionString raw "${APP_SRC}/Contents/Info.plist" 2>/dev/null || echo "0.0.0")"
fi
echo "==> Roomcut release ${VERSION}"

DIST="${REPO_ROOT}/dist"
mkdir -p "${DIST}"
WORK="$(mktemp -d /tmp/roomcut-release.XXXXXX)"
trap 'rm -rf "${WORK}"' EXIT

# --- Concrete daemon plist (installed paths baked in) ---------------------------
ENGINE_INSTALLED="/Library/Application Support/Roomcut/bin/RoomcutAudioEngine"
STATE_FILE="/Library/Application Support/Roomcut/engine.state"
OUT_LOG="/Library/Logs/Roomcut/engine.out.log"
ERR_LOG="/Library/Logs/Roomcut/engine.err.log"
PLIST_RENDERED="${WORK}/com.roomcut.engine.plist"
sed \
  -e "s|__ENGINE_BINARY__|${ENGINE_INSTALLED}|g" \
  -e "s|__STATE_FILE__|${STATE_FILE}|g" \
  -e "s|__OUT_LOG__|${OUT_LOG}|g" \
  -e "s|__ERR_LOG__|${ERR_LOG}|g" \
  "${PLIST_TEMPLATE}" > "${PLIST_RENDERED}"
plutil -lint "${PLIST_RENDERED}" >/dev/null

# --- pkg payload tree (maps onto / at install) ----------------------------------
PAYLOAD="${WORK}/payload"
mkdir -p \
  "${PAYLOAD}/Applications" \
  "${PAYLOAD}/Library/Audio/Plug-Ins/HAL" \
  "${PAYLOAD}/Library/Application Support/Roomcut/bin" \
  "${PAYLOAD}/Library/LaunchDaemons"
cp -R "${APP_SRC}"    "${PAYLOAD}/Applications/Roomcut.app"
cp -R "${DRIVER_SRC}" "${PAYLOAD}/Library/Audio/Plug-Ins/HAL/Roomcut.driver"
cp    "${ENGINE_SRC}" "${PAYLOAD}/Library/Application Support/Roomcut/bin/RoomcutAudioEngine"
[[ -e "${CTL_SRC}" ]]       && cp "${CTL_SRC}"       "${PAYLOAD}/Library/Application Support/Roomcut/bin/roomcutctl"
[[ -e "${DEVICECTL_SRC}" ]] && cp "${DEVICECTL_SRC}" "${PAYLOAD}/Library/Application Support/Roomcut/bin/roomcut-devicectl"
cp "${PLIST_RENDERED}" "${PAYLOAD}/Library/LaunchDaemons/com.roomcut.engine.plist"
# Stash the uninstaller where pkg users can find it (pkgs can't self-uninstall).
cp "${RELEASE_DIR}/uninstall.sh" "${PAYLOAD}/Library/Application Support/Roomcut/uninstall.sh"
chmod +x "${PAYLOAD}/Library/Application Support/Roomcut/uninstall.sh"

SCRIPTS="${WORK}/scripts"
mkdir -p "${SCRIPTS}"
cp "${RELEASE_DIR}/postinstall" "${SCRIPTS}/postinstall"
chmod +x "${SCRIPTS}/postinstall"

# Disable bundle relocation so the app/driver always install to the fixed paths,
# even if a stale copy exists elsewhere on the user's Mac (pkgbuild defaults apps
# to relocatable, which silently redirects the install).
COMPONENT_PLIST="${WORK}/component.plist"
pkgbuild --analyze --root "${PAYLOAD}" "${COMPONENT_PLIST}" >/dev/null
# Iterate every bundle entry (some, e.g. the HAL driver, omit BundleIsRelocatable
# entirely — so drive the loop off the dict existing, and Add the key if absent).
i=0
while /usr/libexec/PlistBuddy -c "Print :${i}" "${COMPONENT_PLIST}" >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Set :${i}:BundleIsRelocatable false" "${COMPONENT_PLIST}" 2>/dev/null \
    || /usr/libexec/PlistBuddy -c "Add :${i}:BundleIsRelocatable bool false" "${COMPONENT_PLIST}"
  i=$((i + 1))
done

PKG_OUT="${DIST}/Roomcut-${VERSION}.pkg"
echo "==> Building ${PKG_OUT}"
pkgbuild \
  --root "${PAYLOAD}" \
  --component-plist "${COMPONENT_PLIST}" \
  --scripts "${SCRIPTS}" \
  --identifier "${PKG_ID}" \
  --version "${VERSION}" \
  --ownership recommended \
  --install-location "/" \
  "${PKG_OUT}"

# --- zip (script install) -------------------------------------------------------
STAGE="${WORK}/Roomcut-${VERSION}"
mkdir -p "${STAGE}"
cp -R "${APP_SRC}"    "${STAGE}/Roomcut.app"
cp -R "${DRIVER_SRC}" "${STAGE}/Roomcut.driver"
cp    "${ENGINE_SRC}" "${STAGE}/RoomcutAudioEngine"
[[ -e "${CTL_SRC}" ]]       && cp "${CTL_SRC}"       "${STAGE}/roomcutctl"
[[ -e "${DEVICECTL_SRC}" ]] && cp "${DEVICECTL_SRC}" "${STAGE}/roomcut-devicectl"
cp "${PLIST_RENDERED}"           "${STAGE}/com.roomcut.engine.plist"
cp "${RELEASE_DIR}/install.sh"   "${STAGE}/install.sh"
cp "${RELEASE_DIR}/uninstall.sh" "${STAGE}/uninstall.sh"
cp "${RELEASE_DIR}/README.txt"   "${STAGE}/README.txt"
chmod +x "${STAGE}/install.sh" "${STAGE}/uninstall.sh"

ZIP_OUT="${DIST}/Roomcut-${VERSION}.zip"
rm -f "${ZIP_OUT}"
echo "==> Building ${ZIP_OUT}"
( cd "${WORK}" && /usr/bin/zip -qry "${ZIP_OUT}" "Roomcut-${VERSION}" )

echo
echo "Done:"
echo "  ${PKG_OUT}"
echo "  ${ZIP_OUT}"
echo
echo "Both are ad-hoc signed (no Developer ID). Note in the GitHub release that"
echo "the .pkg may need right-click ▸ Open the first time, and the .zip path runs"
echo "install.sh which strips the download quarantine."
