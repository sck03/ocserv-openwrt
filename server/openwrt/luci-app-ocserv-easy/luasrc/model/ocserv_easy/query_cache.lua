-- SPDX-License-Identifier: GPL-3.0-or-later
-- Bounded, disposable runtime snapshots shared by independent LuCI requests.
-- A corrupt/missing cache or a failed cache write never fails a live query.
local nixio = require "nixio"
local fs = require "nixio.fs"
local json = require "luci.jsonc"
local storage = require "luci.model.ocserv_easy.storage"
local M = {}
local root = "/var/run/ocserv-easy"

local function now()
    local info = nixio.sysinfo and nixio.sysinfo()
    return info and info.uptime or os.time()
end

function M.identity(path)
    local info = fs.stat(path) or {}
    return path .. ":" .. tostring(info.ino) .. ":" .. tostring(info.mtime) .. ":" .. tostring(info.size)
end

function M.get(name, identity, seconds, query, fresh)
    assert(name:match("^[a-z-]+$"))
    local path = root .. "/query-" .. name .. ".json"
    local time = now()
    if not fresh then
        local ok, cached = pcall(function()
            local raw = storage.read(path)
            return raw and json.parse(raw)
        end)
        if ok and type(cached) == "table" and cached.identity == identity and
            type(cached.time) == "number" and cached.time <= time and time - cached.time < seconds and
            type(cached.value) == "table" then return cached.value end
    end
    local value = query()
    if type(value) == "table" and value.cacheable ~= false then
        pcall(function()
            storage.directory(root)
            storage.atomic(path, json.stringify({identity = identity, time = time, value = value}))
        end)
    end
    return value
end

function M.invalidate(name)
    assert(name:match("^[a-z-]+$"))
    pcall(fs.remove, root .. "/query-" .. name .. ".json")
end

return M
