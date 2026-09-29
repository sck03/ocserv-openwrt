#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
mode="${1:-unsigned}"
[[ "$mode" == unsigned || "$mode" == signed ]] || { echo 'Expected unsigned or signed'; exit 2; }
test ! -e dist/ios || { echo 'Move dist/ios before rebuilding'; exit 2; }
export BVPN_BUNDLE_ID="${BVPN_BUNDLE_ID:-com.bulijie.vpn}"
[[ "$BVPN_BUNDLE_ID" =~ ^[A-Za-z0-9]+([.-][A-Za-z0-9]+)+$ ]] || { echo 'Invalid bundle identifier'; exit 2; }
if [[ "$mode" == signed ]]; then
  : "${IOS_CERTIFICATE_BASE64:?Missing signing certificate}"
  : "${IOS_CERTIFICATE_PASSWORD:?Missing certificate password}"
  : "${IOS_APP_PROFILE_BASE64:?Missing app provisioning profile}"
  : "${IOS_TUNNEL_PROFILE_BASE64:?Missing tunnel provisioning profile}"
  : "${IOS_TEAM_ID:?Missing Apple team ID}"
fi
bash scripts/build-ios-core.sh
xcodegen generate --spec mobile/ios/project.yml
mkdir -p dist/ios
archive="$root/build/mobile/ios/BulijieVPN.xcarchive"
args=(-project mobile/ios/BulijieVPN.xcodeproj -scheme BulijieVPN -configuration Release
      -sdk iphoneos -destination 'generic/platform=iOS' -archivePath "$archive"
      "BVPN_BUNDLE_ID=$BVPN_BUNDLE_ID")
if [[ "$mode" == unsigned ]]; then
  xcodebuild "${args[@]}" CODE_SIGNING_ALLOWED=NO archive
  test -d "$archive/Products/Applications/BulijieVPN.app/PlugIns/PacketTunnel.appex"
  ditto -c -k --keepParent "$archive" dist/ios/BulijieVPN-ios-unsigned.xcarchive.zip
else
  signing="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/bvpn-signing.XXXXXX")"
  keychain="$signing/build.keychain-db"
  cleanup() {
    security delete-keychain "$keychain" >/dev/null 2>&1 || true
    python3 scripts/ios-signing.py clean "$signing"
    rm -rf "$signing"
  }
  trap cleanup EXIT
  python3 scripts/ios-signing.py prepare "$signing"
  keychain_password="$(openssl rand -hex 32)"
  security create-keychain -p "$keychain_password" "$keychain"
  security set-keychain-settings -lut 21600 "$keychain"
  security unlock-keychain -p "$keychain_password" "$keychain"
  security import "$signing/certificate.p12" -P "$IOS_CERTIFICATE_PASSWORD" -A -t cert -f pkcs12 -k "$keychain"
  security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$keychain_password" "$keychain" >/dev/null
  python3 scripts/ios-signing.py keychain "$signing"
  app_profile="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["app"])' "$signing/profiles.json")"
  tunnel_profile="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["tunnel"])' "$signing/profiles.json")"
  xcodebuild "${args[@]}" "DEVELOPMENT_TEAM=$IOS_TEAM_ID" \
    "BVPN_APP_PROFILE=$app_profile" "BVPN_TUNNEL_PROFILE=$tunnel_profile" \
    CODE_SIGN_IDENTITY='Apple Distribution' "OTHER_CODE_SIGN_FLAGS=--keychain $keychain" archive
  xcodebuild -exportArchive -archivePath "$archive" -exportPath "$root/dist/ios/export" \
    -exportOptionsPlist "$signing/ExportOptions.plist"
  cp dist/ios/export/*.ipa dist/ios/BulijieVPN-ios.ipa
fi
cp build/mobile/ios/BUILDINFO.json dist/ios/BUILDINFO.json
python3 scripts/package-mobile.py ios
