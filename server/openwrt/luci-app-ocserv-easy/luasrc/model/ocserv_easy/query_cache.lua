-- SPDX-License-Identifier: GPL-3.0-or-later
-- Bounded, disposable runtime snapshots shared by independent LuCI requests.
-- A corrupt/missing cache or a failed cache write never fails a live query.
local nixio = require "nixio"
local fs = require "nixio.fs"
local json = require "luci.jsonc"
local storage = require "luci.model.ocserv_easy.storage"
local M = {}
local root = "/var/run/ocserv-easy"

local function paths(name)
    assert(name:match("^[a-z-]+$"))
    local prefix = root .. "/query-" .. name
    return prefix .. ".json", prefix .. ".generation"
end

local function now()
    local info = nixio.sysinfo and nixio.sysinfo()
    return info and info.uptime or os.time()
end

function M.identity(path)
    local info = fs.stat(path) or {}
    return path .. ":" .. tostring(info.ino) .. ":" .. tostring(info.mtime) .. ":" .. tostring(info.size)
end

function M.get(name, identity, seconds, query, fresh)
    local path, marker = paths(name)
    local time = now()
    if not fresh then
        local ok, cached = pcall(function()
            local raw = storage.read(path)
            local value = raw and json.parse(raw)
            -- Read the marker after the snapshot: invalidation may have raced
            -- with either its publication or this read in another LuCI process.
            if type(value) == "table" and type(value.generation) == "string" and
                #value.generation == 32 and value.generation == storage.read(marker, 32) then return value end
        end)
        if ok and type(cached) == "table" and cached.identity == identity and
            type(cached.time) == "number" and cached.time <= time and time - cached.time < seconds and
            type(cached.value) == "table" then return cached.value end
    end
    -- Once bypassed, an old success must not reappear after a failed refresh.
    local generation = M.invalidate(name)
    local value = query()
    if generation and type(value) == "table" and value.cacheable ~= false then
        pcall(function()
            -- Avoid replacing newer work when possible. Readers also check the
            -- generation, so invalidation between this check and rename is safe.
            if storage.read(marker, 32) ~= generation then return end
            storage.atomic(path, json.stringify({identity = identity, time = now(),
                generation = generation, value = value}))
        end)
    end
    return value
end

function M.invalidate(name)
    local path, marker = paths(name)
    local ok, generation = pcall(function()
        storage.directory(root)
        local token = nixio.bin.hexlify(storage.random(16))
        storage.atomic(marker, token)
        return token
    end)
    -- If advancing the marker fails, also remove the old generation so pending
    -- writers cannot revive it. Cache IO never changes the live query outcome.
    if not ok then pcall(fs.remove, marker) end
    pcall(fs.remove, path)
    return ok and generation or nil
end

return M
