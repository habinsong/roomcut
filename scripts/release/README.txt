Roomcut installer
=================

macOS 26 (Tahoe) or later and Apple Silicon are required.

1. Double-click Roomcut-<version>.pkg.
2. Follow the installer. It installs Roomcut.app, the virtual audio driver, and
   the background engine, then restarts coreaudiod. Audio may stop briefly.
3. Open Roomcut from Applications. It runs in the menu bar.
4. Select "Roomcut Output" in System Settings > Sound, or let Roomcut do it.

UNINSTALL
  sudo bash /Library/Application\ Support/Roomcut/uninstall.sh

SIGNING
  This build is ad-hoc signed and is not notarized. If macOS blocks it, open it
  once anyway, then go to System Settings > Privacy & Security > Open Anyway.
  That button appears only after the blocked attempt. Terminal works too:
  sudo installer -pkg Roomcut-<version>.pkg -target /

LICENSE
  Roomcut is licensed under the Apache License 2.0. The installed copy includes
  LICENSE and THIRD_PARTY_NOTICES.md in /Library/Application Support/Roomcut/.
