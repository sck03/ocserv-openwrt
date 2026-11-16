-- SPDX-License-Identifier: GPL-3.0-or-later
-- One renderer and preflight lifetime for startup, settings and isolation.
local fs = require "nixio.fs"
local logic = require "ocserv.logic"
local storage = require "ocserv.storage"
local run = require("ocserv.process").run
local M = {}

function M.check(config, passwd, prefix, execute)
    local config_path, passwd_path = prefix .. ".conf", prefix .. ".passwd"
    local ok, result = pcall(function()
        storage.atomic(passwd_path, passwd)
        storage.atomic(config_path, (config:gsub("/var/etc/ocpasswd", passwd_path)))
        return (execute or run)({"/usr/sbin/ocserv", "--test-config", "--config", config_path}, 10)
    end)
    fs.remove(config_path)
    fs.remove(passwd_path)
    if not ok then error(result, 0) end
    logic.require(result == 0, "config_check_failed")
end

function M.render(cursor)
    logic.require(cursor:load("ocserv"), "unknown_schema")
    local users, dns, routes = {}, {}, {}
    cursor:foreach("ocserv", "ocservusers", function(row) users[#users + 1] = row end)
    cursor:foreach("ocserv", "dns", function(row) dns[#dns + 1] = row end)
    cursor:foreach("ocserv", "routes", function(row) routes[#routes + 1] = row end)
    local config = cursor:get_all("ocserv", "config")
    logic.require(type(config) == "table", "unknown_schema")
    local values, normalized_dns, normalized_routes = logic.validate_settings(logic.settings(config, dns, routes))
    for key, value in pairs(values) do config[key] = value end
    return logic.render(storage.read("/etc/ocserv/ocserv.conf.template") or "",
        storage.read("/etc/ocserv/ocserv.conf.local") or "", config, normalized_dns, normalized_routes,
        {domain = cursor:get_first("dhcp", "dnsmasq", "domain", "")}), logic.passwd(users)
end

function M.start()
    local source = storage.read("/etc/config/ocserv")
    local config, passwd = M.render(require("uci").cursor())
    if not fs.stat("/var/etc") then logic.require(fs.mkdir("/var/etc", "755"), "write_failed") end
    storage.directory("/var/etc/ocserv-stage")
    M.check(config, passwd, "/var/etc/ocserv-stage/check")
    logic.require(source == storage.read("/etc/config/ocserv"), "stale_revision")
    local files = {{path = "/var/etc/ocpasswd", value = passwd}, {path = "/var/etc/ocserv.conf", value = config}}
    for _, file in ipairs(files) do file.before = storage.read(file.path) end
    local ok, failure = pcall(function()
        for _, file in ipairs(files) do storage.atomic(file.path, file.value) end
    end)
    if not ok then
        -- Startup has no active daemon. Preserve the previous complete runtime
        -- on ordinary IO failures; a reboot always regenerates it from UCI.
        for _, file in ipairs(files) do
            if storage.read(file.path) ~= file.before then
                if file.before then storage.atomic(file.path, file.before)
                else storage.remove(file.path) end
            end
        end
        error(failure, 0)
    end
end

return M
