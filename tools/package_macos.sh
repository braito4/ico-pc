#!/usr/bin/env bash
# Build and package the native Apple silicon macOS app.
# tools/package_macos.sh [<label>] [CMake arguments...]
# <label> (v0.7.0) gives the bundle its version, as package_win.sh does
# (CMakeLists.txt ICO_VERSION_LABEL; 0.0.0 without one). The other
# arguments go to CMake (e.g. -DICO_DXC=/path/to/host/dxc).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

tools/fetch_moltenvk.sh macos
label=""
if [[ $# -gt 0 && "$1" != -* ]]; then
    label="$1"
    shift
fi
cmake --preset macos-app "-DICO_VERSION_LABEL=$label" "$@"
cmake --build --preset macos-app --parallel "${ICO_BUILD_JOBS:-6}"

app="$root/build-host/macos-app/ICO.app"
exe="$app/Contents/MacOS/ICO"
[[ -x "$exe" ]] || { echo "Missing macOS executable: $exe" >&2; exit 1; }
plutil -lint "$app/Contents/Info.plist"

mkdir -p "$root/dist"
stage="$(mktemp -d "$root/build-host/macos-package.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
bundle="$stage/ICO.app"
ditto "$app" "$bundle"
mkdir -p "$bundle/Contents/Resources"
cp LICENSE "$bundle/Contents/Resources/LICENSE.txt"
python3 tools/gen_notices.py --platform macos --arch arm64 \
    --out "$bundle/Contents/Resources/NOTICES.txt"
rm -f "$bundle/Contents/MacOS/ico_pc.map"
# Cloud-backed workspaces may add Finder/provenance metadata that code signing
# rejects. The staged bundle does not need those extended attributes.
xattr -cr "$bundle"
codesign --force --deep --sign - "$bundle"
codesign --verify --deep --strict "$bundle"

ditto "$bundle" "$root/dist/ICO.app"
xattr -cr "$root/dist/ICO.app"
codesign --force --deep --sign - "$root/dist/ICO.app"
codesign --verify --deep --strict "$root/dist/ICO.app"
ditto -c -k --sequesterRsrc --keepParent "$bundle" "$root/dist/ico-pc-macos-arm64.zip"
hdiutil create -quiet -volname ICO -srcfolder "$stage" -ov -format UDZO \
    "$root/dist/ico-pc-macos-arm64.dmg"
cp "$app/Contents/MacOS/ico_pc.map" "$root/dist/ico-pc-macos-arm64.map"
echo "App: $root/dist/ICO.app"
echo "Zip: $root/dist/ico-pc-macos-arm64.zip"
echo "DMG: $root/dist/ico-pc-macos-arm64.dmg"
