Roomcut — manual (script) install
=================================

This zip contains prebuilt Roomcut components and an installer. macOS 26
(Tahoe) or later is required.

INSTALL
  1. Unzip this folder anywhere (e.g. Downloads).
  2. Open Terminal in this folder and run:

         sudo ./install.sh

     It installs the app to /Applications, the audio driver into the system
     HAL folder, and a background engine, then restarts coreaudiod (system
     audio glitches for ~1 second).
  3. Open "Roomcut" (it lives in the menu bar — no Dock icon).
  4. In System Settings ▸ Sound, select "Roomcut Output" (or let the app set it).

UNINSTALL
         sudo ./uninstall.sh
  (also installed at: /Library/Application Support/Roomcut/uninstall.sh)

NOTE ON SIGNING
  These builds are ad-hoc signed (no Apple Developer ID). install.sh strips the
  download quarantine so the driver loads and the app opens. If macOS still
  blocks the app, right-click it ▸ Open, or allow it in
  System Settings ▸ Privacy & Security.

  Prefer a double-click installer? Use the .pkg from the same release instead.
