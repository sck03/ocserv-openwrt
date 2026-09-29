#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mode="${1:-debug}"
[[ "$mode" == debug || "$mode" == release ]] || { echo 'Expected debug or release'; exit 2; }
if [[ "$mode" == release ]]; then
  : "${KEYSTORE_FILE:?Release keystore required}"
  : "${KEYSTORE_PASSWORD:?Keystore password required}"
  : "${KEY_ALIAS:?Key alias required}"
  test -s "$KEYSTORE_FILE"
fi
test ! -e dist/android || { echo 'Move dist/android before rebuilding'; exit 2; }
python3 scripts/mobile-sources.py android
root="$PWD"
cd build/mobile/android/app
bash native/build-openconnect.sh --abis arm64-v8a,armeabi-v7a,x86_64 \
  --version 9.21 --openssl 3.5.8 --libxml2 2.15.3 --lz4 1.10.0 --api 26
for abi in arm64-v8a armeabi-v7a x86_64; do
  test -s "app/src/main/jniLibs/$abi/libopenconnect.so"
done
python3 "$root/scripts/audit-android.py" app/src/main/jniLibs
if [[ "$mode" == release ]]; then
  : "${KEYSTORE_FILE:?Release keystore required}"
  : "${KEYSTORE_PASSWORD:?Keystore password required}"
  : "${KEY_ALIAS:?Key alias required}"
  bash gradlew :app:assembleRelease :app:testReleaseUnitTest --no-daemon --stacktrace
else
  bash gradlew :app:assembleDebug :app:testDebugUnitTest --no-daemon --stacktrace
fi
mkdir -p "$root/dist/android"
cp app/build/outputs/apk/"$mode"/*.apk "$root/dist/android/BulijieVPN-android-$mode.apk"
python3 "$root/scripts/audit-android.py" "$root/dist/android/BulijieVPN-android-$mode.apk"
cp ../BUILDINFO.json "$root/dist/android/BUILDINFO.json"
cd "$root"
python3 scripts/package-mobile.py android
