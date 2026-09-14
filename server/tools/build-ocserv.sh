#!/usr/bin/env bash
# Run on Linux, inside an SDK supplied for the intended firmware. Does not touch a router.
set -euo pipefail
sdk=${1:?Usage: build-ocserv.sh /absolute/path/to/matching-openwrt-sdk}
[[ $(uname -s) == Linux ]] || { printf '%s\n' 'An OpenWrt SDK requires a Linux build host.' >&2; exit 1; }
sdk=$(realpath "$sdk")
feed=$(cd "$(dirname "$0")/../openwrt" && pwd)
[[ -f "$sdk/include/toplevel.mk" && -x "$sdk/scripts/feeds" ]] || { printf '%s\n' 'Not an OpenWrt SDK directory.' >&2; exit 1; }
[[ "$feed" != *' '* && "$sdk" != *' '* ]] || { printf '%s\n' 'OpenWrt SDK paths cannot contain spaces.' >&2; exit 1; }
cd "$sdk"
[[ -f feeds.conf ]] || cp feeds.conf.default feeds.conf
if ! grep -q '^src-link bulijie ' feeds.conf; then
    printf '\nsrc-link bulijie %s\n' "$feed" >> feeds.conf
fi
./scripts/feeds update -a
./scripts/feeds install -a
./scripts/feeds install -f -p bulijie ocserv luci-app-ocserv-easy
touch .config
for symbol in ALL ALL_NONSHARED ALL_KMODS PACKAGE_ocserv PACKAGE_luci-app-ocserv PACKAGE_luci-app-ocserv-easy LUCI_LANG_zh_Hans OCSERV_PAM OCSERV_RADIUS OCSERV_LIBOATH OCSERV_PROTOBUF OCSERV_SECCOMP; do
    sed -i "/^CONFIG_${symbol}=/d; /^# CONFIG_${symbol} is not set$/d" .config
done
cat >> .config <<'CONFIG'
CONFIG_ALL=n
CONFIG_ALL_NONSHARED=n
CONFIG_ALL_KMODS=n
CONFIG_PACKAGE_ocserv=m
CONFIG_PACKAGE_luci-app-ocserv=m
CONFIG_PACKAGE_luci-app-ocserv-easy=m
CONFIG_LUCI_LANG_zh_Hans=y
# CONFIG_OCSERV_PAM is not set
# CONFIG_OCSERV_RADIUS is not set
# CONFIG_OCSERV_LIBOATH is not set
# CONFIG_OCSERV_SECCOMP is not set
CONFIG_OCSERV_PROTOBUF=y
CONFIG
make defconfig
if ! grep -q '^CONFIG_TARGET_armsr_armv8=y' .config; then
    printf '%s\n' 'This task targets armsr/armv8. Check that the SDK matches the N1 firmware before compiling.' >&2
    exit 1
fi
make package/feeds/bulijie/ocserv/download V=s
make -j"${BUILD_JOBS:-2}" package/feeds/bulijie/ocserv/compile V=s
make -j"${BUILD_JOBS:-2}" package/feeds/luci/luci-app-ocserv/compile V=s
make -j"${BUILD_JOBS:-2}" package/feeds/bulijie/luci-app-ocserv-easy/compile V=s
printf '%s\n' 'ocserv and LuCI output packages (match firmware ABI before installation):'
find bin -type f \( -name 'ocserv_*.ipk' -o -name 'ocserv-*.apk' -o -name 'luci-*ocserv*.ipk' -o -name 'luci-*ocserv*.apk' \) -print
