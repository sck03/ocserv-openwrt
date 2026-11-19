-- SPDX-License-Identifier: GPL-3.0-or-later
-- Bounded, disposable runtime snapshots shared by independent LuCI requests.
-- A corrupt/missing cache or a failed cache write never fails a live query.
local nixio = require "nixio"
local fs = require "nixio.fs"
local json = require "luci.jsonc"
local storage = require "ocserv.storage"
local equal = require("ocserv.logic").equal
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
    return path .. ":" .. tostring(info.ino) .. ":" .. tostring(info.mtime) .. ":" .. tostring(info.ctime) .. ":" .. tostring(info.size)
end

local function cached(path, marker, identity, seconds, limit, valid)
    local time = now()
    local ok, value = pcall(function()
        local raw = storage.read(path, limit)
        local entry = raw and json.parse(raw)
        -- Check the marker after reading the snapshot, including across processes.
        if type(entry) == "table" and entry.format == 3 and type(entry.version) == "string" and #entry.version == 32 and
            type(entry.generation) == "string" and
            #entry.generation == 32 and entry.generation == storage.read(marker, 32) and
            entry.identity == identity and type(entry.time) == "number" and entry.time <= time and
            time - entry.time < seconds and type(entry.value) == "table" and
            (not valid or valid(entry.value)) then return entry end
    end)
    return ok and value or nil
end

function M.peek(name,identity,seconds,limit,valid)
    local path,marker=paths(name)
    local entry=cached(path,marker,identity,seconds,limit,valid)
    return entry and entry.value or nil
end

local function marker_value(marker)
    local ok, value = pcall(storage.read, marker, 32)
    return ok and value or nil
end

function M.get(name, identity, seconds, query, fresh, limit, project, valid)
    local path, marker = paths(name)
    local previous = marker_value(marker)
    local value = not fresh and cached(path, marker, identity, seconds, limit, valid)
    if value then return value.value, value.version, value.time, value.generation end
    -- Query locks are independent of configuration transactions. Bounded waits
    -- coalesce simultaneous cache misses without blocking invalidation.
    local lock, file
    pcall(function()
        storage.directory(root)
        file = nixio.open(path .. ".lock", nixio.open_flags("wronly", "creat"), "600")
        if not file then return end
        for attempt = 1, 101 do
            if file:lock("tlock") then lock = file; return end
            if attempt < 101 then nixio.poll({}, 50) end
        end
    end)
    if file and not lock then pcall(function() file:close() end) end
    if lock then
        value = cached(path, marker, identity, seconds, limit, valid)
        if value and (not fresh or value.generation ~= previous) then
            lock:lock("ulock"); lock:close()
            return value.value, value.version, value.time, value.generation
        end
    end
    -- Once bypassed, an old success must not reappear after a failed refresh.
    local previous_value = cached(path, marker, identity, math.huge, limit, valid)
    local generation = M.invalidate(name)
    local ok, value = pcall(query)
    local completed = now()
    -- Content versions survive TTL refreshes; generations fence late writers.
    local version = generation
    if ok and previous_value and equal(previous_value.value, value) then version = previous_value.version end
    if ok and generation and type(value) == "table" and value.cacheable ~= false then
        pcall(function()
            -- Avoid replacing newer work when possible. Readers also check the
            -- generation, so invalidation between this check and rename is safe.
            if storage.read(marker, 32) ~= generation then return end
            storage.atomic(path, json.stringify({format = 3, identity = identity, time = completed,
                generation = generation, version = version, value = value}), limit)
            if project then
                storage.atomic(path .. ".summary", json.stringify({format = 3, identity = identity, time = completed,
                    generation = generation, version = version, value = project(value)}))
            end
        end)
    end
    if lock then lock:lock("ulock"); lock:close() end
    if not ok then error(value, 0) end
    return value, version, completed, generation
end

function M.projected(name, identity, seconds, query, project, limit, valid_full, valid_summary)
    local path, marker = paths(name)
    local value = cached(path .. ".summary", marker, identity, seconds, nil, valid_summary)
    if value then return value.value, value.version end
    local full, version, time, generation = M.get(name, identity, seconds, query, false, limit, project, valid_full)
    local summary = project(full)
    local published = cached(path .. ".summary", marker, identity, seconds, nil, valid_summary)
    if published and published.generation == generation then return summary, version end
    -- A full-data reader may have populated the cache before this projection
    -- was requested. Preserve its original expiry when adding the small view.
    if generation then pcall(function()
        if storage.read(marker, 32) == generation then
            storage.atomic(path .. ".summary", json.stringify({format = 3, identity = identity, time = time,
                generation = generation, version = version, value = summary}))
        end
    end) end
    return summary, version
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
    pcall(fs.remove, path .. ".summary")
    return ok and generation or nil
end

return M
