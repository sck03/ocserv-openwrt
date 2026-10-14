#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mode=${1:-unsigned}
[[ "$mode" == unsigned || "$mode" == signed ]] || { echo 'Expected unsigned or signed'; exit 2; }
test ! -e dist/macos || { echo 'Move dist/macos before rebuilding'; exit 2; }
export VPN_BUNDLE_ID="${VPN_BUNDLE_ID:-io.github.sck03.lansyra.macos}"
[[ "$VPN_BUNDLE_ID" =~ ^[A-Za-z0-9]+([.-][A-Za-z0-9]+)+$ ]] || { echo 'Invalid bundle identifier'; exit 2; }
root="$PWD"
bash scripts/build-apple-core.sh macos
python3 - <<'PY'
from PIL import Image
image = Image.open('resources/app.ico').convert('RGBA')
image.resize((1024, 1024), Image.Resampling.LANCZOS).save('desktop/macos/App/AppIcon.icns')
PY
xcodegen generate --spec desktop/macos/project.yml
archive="$root/build/desktop/macos/Lansyra.xcarchive"
args=(-project desktop/macos/Lansyra.xcodeproj -scheme Lansyra -configuration Release
      -destination 'generic/platform=macOS' -archivePath "$archive" "VPN_BUNDLE_ID=$VPN_BUNDLE_ID")
if [[ "$mode" == unsigned ]]; then
  xcodebuild "${args[@]}" CODE_SIGNING_ALLOWED=NO archive
else
  : "${MACOS_TEAM_ID:?Set the Apple developer team ID}"
  : "${MACOS_APP_PROFILE:?Set the app provisioning profile name}"
  : "${MACOS_TUNNEL_PROFILE:?Set the packet tunnel provisioning profile name}"
  : "${MACOS_SIGN_IDENTITY:?Set the installed Apple signing identity}"
  xcodebuild "${args[@]}" "DEVELOPMENT_TEAM=$MACOS_TEAM_ID" "CODE_SIGN_IDENTITY=$MACOS_SIGN_IDENTITY" \
    "VPN_APP_PROFILE=$MACOS_APP_PROFILE" "VPN_TUNNEL_PROFILE=$MACOS_TUNNEL_PROFILE" archive
fi
app="$archive/Products/Applications/Lansyra.app"
test "$(/usr/libexec/PlistBuddy -c 'Print:CFBundleDisplayName' "$app/Contents/Info.plist")" = '岚序 Lansyra'
test "$(/usr/libexec/PlistBuddy -c 'Print:CFBundleDisplayName' "$app/Contents/Library/SystemExtensions/$VPN_BUNDLE_ID.tunnel.systemextension/Contents/Info.plist")" = '岚序 Lansyra 隧道'
version="$(/usr/libexec/PlistBuddy -c 'Print:CFBundleShortVersionString' "$app/Contents/Info.plist")"
xcrun lipo "$app/Contents/MacOS/Lansyra" -verify_arch arm64 x86_64
xcrun lipo "$app/Contents/Library/SystemExtensions/$VPN_BUNDLE_ID.tunnel.systemextension/Contents/MacOS/PacketTunnel" -verify_arch arm64 x86_64
mkdir -p dist/macos
ditto -c -k --keepParent "$app" "dist/macos/Lansyra-$version-macos-universal-$mode.zip"
cp build/desktop/macos/BUILDINFO.json dist/macos/BUILDINFO.json
python3 scripts/package-mobile.py macos
