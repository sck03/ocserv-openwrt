-- SPDX-License-Identifier: GPL-3.0-or-later
-- The command output is bounded by process.run; reject incomplete session lists.
local json = require "luci.jsonc"
local logic = require "luci.model.ocserv_easy.logic"
local run = require("luci.model.ocserv_easy.process").run
local M = {}

local function query(arguments, timeout)
    local command = {"/usr/bin/occtl", "-j", "show"}
    for _, argument in ipairs(arguments) do command[#command + 1] = argument end
    local code, output = run(command, timeout)
    -- jsonc maps both [] and {} to empty Lua tables. Check the wire shape too.
    if code ~= 0 or not output:match("^%s*%[") or logic.json_has_null(output) then return nil end
    local rows = json.parse(output)
    return logic.array(rows) and rows or nil
end

local function text(value, limit)
    return type(value) == "string" and value:sub(1, limit or 128) or ""
end

function M.online()
    local parsed, rows = query({"users"}, 4), {}
    local failure = {online = {}, online_error = true}
    if not parsed then return failure end
    for _, row in ipairs(parsed) do
        if type(row) ~= "table" or type(row.Username) ~= "string" then return failure end
        local id = tonumber(row.ID)
        if not id or id % 1 ~= 0 or id < 1 or id > 2147483647 then return failure end
        rows[#rows + 1] = {id = tostring(id), name = text(row.Username, 64), group = text(row.Groupname),
            ip = text(row["Remote IP"]), vpn_ip = text(row.IPv4 or row.IPv6),
            since = text(row["Last connected at"]), state = text(row.State)}
    end
    return {online = rows}
end

function M.absent(username)
    local rows = query({"sessions", "all"}, 5)
    if not rows then return false end
    for _, row in ipairs(rows) do
        if type(row) ~= "table" or type(row.Username) ~= "string" or
            row.Username == "" or row.Username == username then return false end
    end
    return true
end

return M
