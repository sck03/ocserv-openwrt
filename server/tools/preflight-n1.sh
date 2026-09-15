#!/bin/sh
# Read-only compatibility report. Never prints credentials or private keys.
set -eu
printf '%s\n' 'BulijieVPN / ocserv upgrade preflight'
ubus call system board
printf 'Kernel: %s\n' "$(uname -r)"
release=$(ubus call system board | jsonfilter -e '@.release.version')
case "$release" in 25.12|25.12.*|25.12-SNAPSHOT) ;; *) printf '%s\n' 'ERROR: this bundle requires OpenWrt 25.12.' >&2; exit 1;; esac
if command -v apk >/dev/null 2>&1; then
    printf '%s\n' 'Package format: APK (OpenWrt 25.12)'
    apk --print-arch
    for package in ocserv musl libgnutls libev libncurses libreadline libprotobuf-c luci-app-ocserv luci-compat; do
        apk info -v "$package" 2>/dev/null || true
    done
else
    printf '%s\n' 'ERROR: APK is required for the OpenWrt 25.12 bundle.' >&2
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
