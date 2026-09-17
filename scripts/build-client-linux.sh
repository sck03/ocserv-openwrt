#!/usr/bin/env bash
# Linux release entry point; dependency recipes are shared with the Windows build.
set -euo pipefail
arch=${1:?Usage: build-client-linux.sh x64|x86}
case "$arch" in
    x64) host=x86_64-w64-mingw32 ;;
    x86) host=i686-w64-mingw32 ;;
    *) printf '%s\n' 'Architecture must be x64 or x86.' >&2; exit 2 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
jobs=${BUILD_JOBS:-auto}
if [[ "$jobs" == auto ]]; then jobs=$(getconf _NPROCESSORS_ONLN); fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'BUILD_JOBS must be auto or a positive integer.' >&2; exit 2; }
export BUILD_JOBS=$jobs
bash "$root/scripts/build-dependencies.sh" "$arch"
build="$root/build/client/$arch"
cmake --fresh -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/.deps/$arch/toolchain.cmake" \
    -DBRIDGE_DEPS="$root/.deps/$arch" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$build" --parallel "$jobs"
python3 "$root/scripts/package-client.py" --arch "$arch" --build "$build" --strip "$host-strip"
