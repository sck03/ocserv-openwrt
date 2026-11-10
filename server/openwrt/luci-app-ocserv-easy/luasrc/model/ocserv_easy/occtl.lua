-- SPDX-License-Identifier: GPL-3.0-or-later
-- Retain display fields only; detailed routes/cipher data can dwarf the list.
local array = require "luci.model.ocserv_easy.json_array"
local run = require("luci.model.ocserv_easy.process").run
local M = {}

local function query(arguments, timeout, max_rows, visit)
    local command = {"/usr/bin/occtl", "-j", "show"}
    for _, argument in ipairs(arguments) do command[#command + 1] = argument end
    local parser = array.new(visit, max_rows)
    local code = run(command, timeout, parser.write, 64 * 1024 * 1024)
    return code == 0 and parser.finish()
end

local function text(value, limit)
    if type(value) ~= "string" then return "" end
    limit = limit or 128
    if #value <= limit then return value end
    -- Keep the byte budget without splitting a UTF-8 code point. Only inspect
    -- the cut boundary; do not decode/copy an entire potentially large field.
    while limit > 0 do
        local byte = value:byte(limit + 1)
        if byte < 128 or byte >= 192 then break end
        limit = limit - 1
    end
    return value:sub(1, limit)
end

function M.online()
    local rows = {}
    local ok = query({"users"}, 4, 4096, function(row)
        if type(row.Username) ~= "string" then return false end
        local id = tonumber(row.ID)
        if not id or id % 1 ~= 0 or id < 1 or id > 2147483647 then return false end
        local ipv4 = text(row.IPv4)
        rows[#rows + 1] = {id = tostring(id), name = text(row.Username, 64), group = text(row.Groupname),
            ip = text(row["Remote IP"]), vpn_ip = ipv4 ~= "" and ipv4 or text(row.IPv6),
            since = text(row["Last connected at"]), state = text(row.State)}
    end)
    return ok and {online = rows} or {online = {}, online_error = true}
end

function M.absent(username)
    -- Reconnect cookies can outnumber concurrently connected users.
    return query({"sessions", "all"}, 5, 65536, function(row)
        return type(row.Username) == "string" and row.Username ~= "" and row.Username ~= username
    end)
end

return M
