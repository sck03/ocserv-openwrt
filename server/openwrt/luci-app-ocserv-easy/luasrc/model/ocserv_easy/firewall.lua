-- SPDX-License-Identifier: GPL-3.0-or-later
-- Managed baseline firewall sections shared by installation and LuCI updates.
-- Keep this module free of UCI side effects so it can be tested with Lua 5.1.
local M = {}

M.names = {
    zone = "ocserv_vpn",
    forwarding = "ocserv_vpn_to_lan",
    nat = "ocserv_vpn_nat",
    entry = "ocserv_vpn_entry",
}

local function unique_ports(settings)
    local ports, seen = {}, {}
    for _, value in ipairs({settings.port, settings.udp_port ~= "" and settings.udp_port or settings.port}) do
        if not seen[value] then
            seen[value] = true
            ports[#ports + 1] = value
        end
    end
    return table.concat(ports, " ")
end

local function prefix(mask)
    if mask:match("^%d+$") then return tonumber(mask) end
    local a, b, c, d = mask:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
    if not a then return nil end
    local value = (((tonumber(a) * 256 + tonumber(b)) * 256 + tonumber(c)) * 256 + tonumber(d))
    for bits = 0, 32 do
        local network = bits == 0 and 0 or 4294967296 - 2 ^ (32 - bits)
        if value == network then return bits end
    end
    return nil
end

function M.sections(settings)
    return {
        [M.names.zone] = {
            [".type"] = "zone",
            name = "ocvpn",
            device = {"vpns+"},
            input = "ACCEPT",
            output = "ACCEPT",
            forward = "REJECT",
            mtu_fix = "1",
        },
        [M.names.forwarding] = {
            [".type"] = "forwarding",
            src = "ocvpn",
            dest = "lan",
        },
        [M.names.nat] = {
            [".type"] = "nat",
            name = "ocserv VPN source NAT",
            src = "lan",
            src_ip = settings.ipaddr .. "/" .. tostring(prefix(settings.netmask)),
            proto = "all",
            family = "ipv4",
            target = "MASQUERADE",
        },
        [M.names.entry] = {
            [".type"] = "rule",
            name = "Allow ocserv from LAN",
            src = "lan",
            proto = "tcp udp",
            dest_port = unique_ports(settings),
            target = "ACCEPT",
        },
    }
end

local function compatible(actual, expected)
    if not actual then return true end
    if actual[".type"] ~= expected[".type"] then return false end
    if actual.name and expected.name and actual.name ~= expected.name then return false end
    return true
end

local function equal(a, b)
    if type(a) ~= type(b) then return false end
    if type(a) ~= "table" then return a == b end
    for key, value in pairs(a) do
        if not equal(value, b[key]) then return false end
    end
    for key in pairs(b) do
        if a[key] == nil then return false end
    end
    return true
end

function M.matches(cursor, settings)
    for id, expected in pairs(M.sections(settings)) do
        local actual = cursor:get_all("firewall", id)
        if not actual or not compatible(actual, expected) then return false end
        for key, value in pairs(expected) do
            if key ~= ".type" and not equal(actual[key], value) then return false end
        end
    end
    return true
end

function M.apply(cursor, settings)
    local sections = M.sections(settings)
    for id, expected in pairs(sections) do
        local actual = cursor:get_all("firewall", id)
        if actual and not compatible(actual, expected) then
            return nil, "firewall_section_conflict"
        end
        if not actual then
            if not cursor:set("firewall", id, expected[".type"]) then
                return nil, "write_failed"
            end
        end
        for key, value in pairs(expected) do
            if key ~= ".type" and not cursor:set("firewall", id, key, value) then
                return nil, "write_failed"
            end
        end
    end
    return true
end

return M
