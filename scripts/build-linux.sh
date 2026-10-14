#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=$(dpkg --print-architecture)
[[ "$arch" == amd64 || "$arch" == arm64 ]] || { echo 'Supported Linux architectures: amd64, arm64'; exit 2; }
build="build/desktop/linux-$arch"
cmake -S desktop/linux -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build "$build" --parallel "$(getconf _NPROCESSORS_ONLN)"
ctest --test-dir "$build" --output-on-failure
NO_AT_BRIDGE=1 xvfb-run -a dbus-run-session -- "$build/linkora" --smoke-test
desktop-file-validate desktop/linux/io.github.sck03.linkora.desktop
stage=$(mktemp -d "$PWD/$build/package.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
cpack --config "$build/CPackConfig.cmake" -B "$stage"
packages=("$stage"/linkora_*_"$arch".deb)
[[ ${#packages[@]} -eq 1 && -s "${packages[0]}" ]] || { echo 'Expected one native DEB'; exit 2; }
python3 tests/linux_package_tests.py "${packages[0]}"
mkdir -p dist/linux
cp "${packages[0]}" dist/linux/
(cd dist/linux && sha256sum "${packages[0]##*/}" > SHA256SUMS.txt)
python3 scripts/package-linux-source.py
