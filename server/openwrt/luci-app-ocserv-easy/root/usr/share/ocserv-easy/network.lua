-- SPDX-License-Identifier: GPL-3.0-or-later
-- Adapt the shared firewall definition to shell installation/removal.
local directory = (arg[0] or ""):match("^(.*)/[^/]+$") or "."
local bundled = loadfile(directory .. "/firewall.lua")
local firewall = bundled and bundled() or require "luci.model.ocserv_easy.firewall"
local function sorted(t)
    local keys = {}; for key in pairs(t) do keys[#keys + 1] = key end
    table.sort(keys); return keys
end
if arg[1] == "names" and #arg == 1 then
    for _, key in ipairs(sorted(firewall.names)) do print(firewall.names[key]) end
elseif arg[1] == "create" and #arg == 5 then
    local logic = require "ocserv.logic"
    local ip, mask, tcp, udp = arg[2], arg[3], arg[4], arg[5]
    assert(logic.ipv4(ip) and logic.prefix(mask), "Invalid VPN network")
    for _, port in ipairs({tcp, udp}) do
        assert(port:match("^%d+$") and tonumber(port) >= 1 and tonumber(port) <= 65535, "Invalid VPN port")
    end
    local sections = firewall.sections({ipaddr=ip, netmask=mask, port=tcp, udp_port=udp})
    for _, id in ipairs(sorted(sections)) do
        local section = sections[id]
        print("set firewall." .. id .. "=" .. section[".type"])
        for _, key in ipairs(sorted(section)) do
            if key ~= ".type" then
                local value = section[key]
                for _, item in ipairs(type(value) == "table" and value or {value}) do
                    assert(not tostring(item):find("['\r\n]"), "Invalid firewall value")
                    print((type(value) == "table" and "add_list " or "set ") .. "firewall." .. id .. "." .. key .. "='" .. item .. "'")
                end
            end
        end
    end
    print("commit firewall")
else error("Expected names or create IP MASK TCP UDP") end
