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
[[ ! -e package/bulijie ]] || { printf '%s\n' 'Use a fresh SDK: package/bulijie already exists.' >&2; exit 1; }
[[ -f feeds.conf ]] || cp feeds.conf.default feeds.conf
./scripts/feeds update base packages luci
# Keep project recipes inside the SDK package tree. A src-link into a different
# Git checkout can produce an empty package index with SDK metadata scanning.
mkdir -p package/bulijie
cp -a "$feed/ocserv" "$feed/luci-app-ocserv-easy" package/bulijie/
chmod 0755 package/bulijie/luci-app-ocserv-easy/root/etc/init.d/ocserv-easy-guard
chmod 0755 package/bulijie/luci-app-ocserv-easy/root/usr/libexec/ocserv-easy-guard
chmod 0755 package/bulijie/luci-app-ocserv-easy/root/usr/libexec/ocserv-easy-repair-users
# Install only needed build recipes and their dependencies. Installing every
# feed package introduces unrelated Kconfig/provider conflicts into an SDK.
./scripts/feeds install libgnutls certtool libev libncurses libreadline libprotobuf-c \
    luci-compat luci-lib-nixio luci-lib-jsonc
touch .config
for symbol in ALL ALL_NONSHARED ALL_KMODS PACKAGE_ocserv PACKAGE_luci-app-ocserv PACKAGE_luci-app-ocserv-easy LUCI_LANG_zh_Hans OCSERV_PAM OCSERV_RADIUS OCSERV_LIBOATH OCSERV_PROTOBUF OCSERV_SECCOMP; do
    sed -i "/^CONFIG_${symbol}=/d; /^# CONFIG_${symbol} is not set$/d" .config
done
cat >> .config <<'CONFIG'
CONFIG_ALL=n
CONFIG_ALL_NONSHARED=n
CONFIG_ALL_KMODS=n
CONFIG_PACKAGE_ocserv=m
# CONFIG_PACKAGE_luci-app-ocserv is not set
CONFIG_PACKAGE_luci-app-ocserv-easy=m
CONFIG_LUCI_LANG_zh_Hans=y
# CONFIG_OCSERV_PAM is not set
# CONFIG_OCSERV_RADIUS is not set
# CONFIG_OCSERV_LIBOATH is not set
# CONFIG_OCSERV_SECCOMP is not set
CONFIG_OCSERV_PROTOBUF=y
CONFIG
make defconfig
grep -q '^CONFIG_USE_APK=y$' .config || { printf '%s\n' 'Use an OpenWrt 25.12 APK SDK.' >&2; exit 1; }
grep -Eq '^CONFIG_PACKAGE_ocserv=(y|m)$' .config
grep -Eq '^CONFIG_PACKAGE_luci-app-ocserv-easy=(y|m)$' .config
if ! grep -q '^CONFIG_TARGET_armsr_armv8=y' .config; then
    printf '%s\n' 'This task targets armsr/armv8. Check that the SDK matches the N1 firmware before compiling.' >&2
    exit 1
fi
make package/bulijie/ocserv/download V=s
make -j"${BUILD_JOBS:-2}" package/bulijie/ocserv/compile V=s
make -j"${BUILD_JOBS:-2}" package/bulijie/luci-app-ocserv-easy/compile V=s
printf '%s\n' 'ocserv and LuCI output packages (match firmware ABI before installation):'
find bin -type f \( -name 'ocserv-*.apk' -o -name 'luci-*ocserv*.apk' \) -print
