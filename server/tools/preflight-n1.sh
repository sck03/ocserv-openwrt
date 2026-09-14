#!/bin/sh
# Read-only compatibility report. Never prints credentials or private keys.
set -eu
printf '%s\n' 'BulijieVPN / ocserv upgrade preflight'
ubus call system board
printf 'Kernel: %s\n' "$(uname -r)"
if command -v opkg >/dev/null 2>&1; then
    printf '%s\n' 'Package format: IPK (opkg)'
    opkg print-architecture
    # opkg status accepts one pattern on some firmware builds; query separately.
    for package in ocserv libc libgnutls libev libncurses6 libreadline8 libprotobuf-c libseccomp luci-app-ocserv luci-compat; do
        opkg status "$package" 2>/dev/null | sed -n '/^Package:/p; /^Version:/p; /^Architecture:/p; /^Status:/p; /^Depends:/p'
    done
elif command -v apk >/dev/null 2>&1; then
    printf '%s\n' 'Package format: APK (OpenWrt 25.12 or later)'
    apk --print-arch
    for package in ocserv musl libgnutls libev libncurses libreadline libprotobuf-c luci-app-ocserv luci-compat; do
        apk info -v "$package" 2>/dev/null || true
    done
else
    printf '%s\n' 'ERROR: unsupported package manager.' >&2
    exit 1
fi
if [ -c /dev/net/tun ]; then
    printf '%s\n' 'TUN: available; retain the current firmware kernel module.'
else
    printf '%s\n' 'ERROR: /dev/net/tun is missing. Install TUN support from this firmware author first.' >&2
    exit 1
fi
if [ -x /usr/sbin/ocserv ]; then /usr/sbin/ocserv --version | sed -n '1,3p'; fi
printf '%s\n' 'Storage:'
df -Pk /etc /tmp
for file in /etc/config/ocserv /etc/ocserv/ca.pem /etc/ocserv/server-cert.pem /etc/ocserv/server-key.pem; do
    [ ! -f "$file" ] || printf 'Present: %s\n' "$file"
done
printf '%s\n' 'Preflight finished. No router settings were changed.'
