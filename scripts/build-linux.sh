#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
arch=$(dpkg --print-architecture)
[[ "$arch" == amd64 || "$arch" == arm64 ]] || { echo 'Supported Linux architectures: amd64, arm64'; exit 2; }
build="build/desktop/linux-$arch"
cmake -S desktop/linux -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
cmake --build "$build" --parallel "$(getconf _NPROCESSORS_ONLN)"
ctest --test-dir "$build" --output-on-failure
xvfb-run -a dbus-run-session -- "$build/linkora-vpn" --smoke-test
desktop-file-validate desktop/linux/io.github.sck03.linkoravpn.desktop
stage="$build/package"
mkdir -p "$stage/DEBIAN" dist/linux
DESTDIR="$PWD/$stage" cmake --install "$build" --strip
cat > "$stage/DEBIAN/control" <<EOF
Package: linkora-vpn
Version: 0.1.0
Architecture: $arch
Maintainer: Linkora VPN <noreply@github.com>
Section: net
Priority: optional
Depends: libc6 (>= 2.36), libstdc++6 (>= 12), libgcc-s1, libgtk-3-0 (>= 3.24) | libgtk-3-0t64 (>= 3.24), libglib2.0-0 (>= 2.74) | libglib2.0-0t64 (>= 2.74), libnm0 (>= 1.42), libsecret-1-0, network-manager (>= 1.42), network-manager-openconnect-gnome
Description: Linkora VPN desktop client for OpenConnect and ocserv
 Native GTK desktop with system VPN permissions, secure authentication,
 optional login connection, bounded retries and private diagnostics.
EOF
dpkg-deb --root-owner-group --build "$stage" "dist/linux/linkora-vpn_0.1.0_$arch.deb"
python3 scripts/package-linux-source.py
