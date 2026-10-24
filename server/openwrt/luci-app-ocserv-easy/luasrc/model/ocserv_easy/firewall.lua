-- SPDX-License-Identifier: GPL-3.0-or-later
-- Managed baseline firewall sections shared by installation and LuCI updates.
-- Keep this module free of UCI side effects so it can be tested with Lua 5.1.
local M = {}
local logic = require "luci.model.ocserv_easy.logic"

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
            src_ip = settings.ipaddr .. "/" .. tostring(logic.prefix(settings.netmask)),
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

function M.matches(cursor, settings)
    for id, expected in pairs(M.sections(settings)) do
        local actual = cursor:get_all("firewall", id)
        if not actual or not compatible(actual, expected) then return false end
        for key, value in pairs(expected) do
            if key ~= ".type" and not logic.equal(actual[key], value) then return false end
        end
        -- These named sections belong to the baseline. An extra selector or
        -- enabled=0 can silently disable an otherwise matching rule.
        for key in pairs(actual) do
            if key:sub(1, 1) ~= "." and expected[key] == nil then return false end
        end
    end
    return true
end

function M.apply(cursor, settings)
    local sections = M.sections(settings)
    -- Detect all ownership conflicts before changing the cursor.
    for id, expected in pairs(sections) do
        local actual = cursor:get_all("firewall", id)
        if actual and not compatible(actual, expected) then
            return nil, "firewall_section_conflict"
        end
    end
    for id, expected in pairs(sections) do
        local actual = cursor:get_all("firewall", id)
        if not actual then
            if not cursor:set("firewall", id, expected[".type"]) then
                return nil, "write_failed"
            end
        end
        for key in pairs(actual or {}) do
            if key:sub(1, 1) ~= "." and expected[key] == nil and not cursor:delete("firewall", id, key) then
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
