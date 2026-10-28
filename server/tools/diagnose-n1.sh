#!/bin/sh
# Read-only diagnostics. Intentionally does not print accounts, passwords, private keys, or proxy subscriptions.
set -u
printf '%s\n' 'Device / firmware:'
ubus call system board
printf '\n%s\n' 'Package architecture:'
if command -v apk >/dev/null 2>&1; then
    [ ! -s /etc/apk/arch ] || { printf 'Firmware /etc/apk/arch: '; cat /etc/apk/arch; printf '\n'; }
    printf 'APK build architecture: '
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
    print("Management UI: "..tostring(result.ui_version or "unavailable"))
    print("Detected ocserv: "..tostring(result.version).."; supported: "..tostring(result.supported))
    print("Running: "..tostring(result.running).."; accounts: "..tostring(#(result.users or {})).."; need password reset: "..invalid)
    print("Authentication mode: "..tostring(result.auth))
    local native,binding=pcall(require,"uci")
    print("Native UCI staging binding: "..tostring(native and binding.cursor~=nil))
    print("VPN DNS: "..table.concat(result.settings.dns or {},", "))
    print("VPN routes: "..(#(result.settings.routes or {})==0 and "all traffic" or table.concat(result.settings.routes,", ")))
end
LUA
else
    printf '%s\n' 'Lua runtime is missing.'
fi
printf '\n%s\n' 'Proxy forwarding diagnostics (generated targets; no subscriptions or credentials):'
if command -v lua >/dev/null 2>&1; then
    lua - <<'LUA'
local ok,err=pcall(function()
    local c=require("luci.model.uci").cursor()
    local proxy=require "luci.model.ocserv_easy.proxy"
    local detected,name=pcall(proxy.detect,c)
    print("Proxy selection: "..(detected and name or type(name)=="table" and name.code or "unavailable"))
    for _,provider in ipairs({"openclash","nikki","momo"}) do
        if c:get(provider,"config",provider=="openclash" and "enable" or "enabled")=="1" then
            local state=proxy.diagnostics(c,provider)
            print(provider..": running="..tostring(state.running).."; VPN ingress="..tostring(state.ingress_ready))
            if state.tcp_mode then print("  TCP="..state.tcp_mode.."; UDP="..tostring(state.udp_mode)) end
            for _,chain in ipairs({"lan_dns_hijack","lan_redirect","lan_tproxy"}) do
                local target=state.targets[chain]
                if target then
                    print("  "..chain..": rules="..tostring(target.available).."; targets="..#target.ports)
                    for _,port in ipairs(target.ports) do
                        print("    port="..port.port.."; TCP socket="..tostring(port.tcp).."; UDP socket="..tostring(port.udp))
                    end
                end
            end
            if state.tun then
                print("  IPv4 TUN: table="..tostring(state.tun.table).."; device="..tostring(state.tun.device).."; marked rule="..tostring(state.tun.rule))
            end
            local acl=c:get_all(provider,"lansyra_vpn")
            if acl then print("  Normal VPN ACL: enabled="..tostring(acl.enabled).."; proxy="..tostring(acl.proxy).."; DNS="..tostring(acl.dns)) end
        end
    end
end)
if not ok then print("Proxy diagnostics: "..(type(err)=="table" and err.code or "unavailable")) end
LUA
fi
printf '\n%s\n' 'Password file permissions (no file contents):'
ls -l /etc/config/ocserv /var/etc/ocpasswd 2>/dev/null || true
printf '\n%s\n' 'Management code failure locations (no request data):'
logread 2>/dev/null | grep 'ocserv-easy:.*backend failure' | tail -n 10 || true
