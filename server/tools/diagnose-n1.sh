#!/bin/sh
# Read-only diagnostics. Intentionally does not print accounts, passwords, private keys, or proxy subscriptions.
set -u
printf '%s\n' 'Device / firmware:'
ubus call system board
printf '\n%s\n' 'Package architecture:'
if command -v apk >/dev/null 2>&1; then
    apk --print-arch
    apk info -v ocserv musl libgnutls libev kmod-tun 2>/dev/null
fi
printf '\n%s\n' 'TUN device:'
ls -l /dev/net/tun 2>/dev/null || true
printf '\n%s\n' 'ocserv version:'
ocserv --version 2>/dev/null || true
printf '\n%s\n' 'Kernel:'
uname -r
