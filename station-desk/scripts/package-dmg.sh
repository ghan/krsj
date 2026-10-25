#!/bin/bash
set -euo pipefail
overlay="$(cd "$(dirname "$0")/.." && pwd)"
app="${1:?Usage: package-dmg.sh /path/to/Station Desk.app /output/folder}"
output="${2:?Output folder required}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
codesign --verify --deep --strict "$app"
lipo -verify_arch arm64 x86_64 "$app/Contents/MacOS/Station Desk"
ditto "$app" "$stage/Station Desk.app"
ln -s /Applications "$stage/Applications"
cp "$overlay/LICENSE" "$stage/LICENSE.txt"
cat > "$stage/Install.txt" <<'INSTALL'
Station Desk 0.2.0 — Radio Saint Johns

Drag Station Desk.app to Applications, then open it.
Choose Music folder in FOLDER ROTATION; the next hour fills automatically.
Choose your audio device and output pairs, then press Start rotation.

The app supports Apple silicon and Intel Macs running macOS 11 or newer.
This community development build is ad hoc signed, not Apple notarized.
macOS may require Privacy & Security > Open Anyway after the first launch.

Source, release notes, and configuration:
https://github.com/ghan/krsj
INSTALL
dmg="$output/Station-Desk-0.2.0-universal.dmg"
hdiutil create -volname "Station Desk" -srcfolder "$stage" -ov -format UDZO "$dmg"
hdiutil verify "$dmg"
(cd "$output" && shasum -a 256 "$(basename "$dmg")" > "$(basename "$dmg").sha256")
printf '\nInstaller: %s\n' "$dmg"
