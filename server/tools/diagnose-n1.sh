#!/bin/sh
# Read-only diagnostics. Intentionally does not print accounts, passwords, private keys, or proxy subscriptions.
set -u
printf '%s\n' 'Device / firmware:'
ubus call system board
printf '\n%s\n' 'Package architecture:'
if command -v apk >/dev/null 2>&1; then
    apk --print-arch
    apk info -v ocserv luci-app-ocserv-easy luci-compat luci-lib-nixio musl libgnutls libev kmod-tun 2>/dev/null
fi
printf '\n%s\n' 'TUN device:'
ls -l /dev/net/tun 2>/dev/null || true
printf '\n%s\n' 'ocserv version:'
ocserv --version 2>/dev/null || true
printf '\n%s\n' 'Kernel:'
uname -r
printf '\n%s\n' 'Management runtime / account health (counts only):'
if command -v lua >/dev/null 2>&1; then
    lua - <<'LUA'
local ok,result=pcall(function() return require("luci.model.ocserv_easy.backend").data() end)
if not ok then
    print("Management API: "..(type(result)=="table" and result.code or "internal_error"))
else
    local invalid=0
    for _,user in ipairs(result.users or {})do if user.needs_password then invalid=invalid+1 end end
    print("Management UI: "..tostring(result.ui_version or "older than 0.4.1"))
    print("Detected ocserv: "..tostring(result.version).."; supported: "..tostring(result.supported))
    print("Running: "..tostring(result.running).."; accounts: "..tostring(#(result.users or {})).."; need password reset: "..invalid)
    print("Authentication mode: "..tostring(result.auth))
end
LUA
else
    printf '%s\n' 'Lua runtime is missing.'
fi
printf '\n%s\n' 'Password file permissions (no file contents):'
ls -l /etc/config/ocserv /var/etc/ocpasswd 2>/dev/null || true
printf '\n%s\n' 'Management code failure locations (no request data):'
logread 2>/dev/null | grep 'ocserv-easy:.*backend failure' | tail -n 10 || true
