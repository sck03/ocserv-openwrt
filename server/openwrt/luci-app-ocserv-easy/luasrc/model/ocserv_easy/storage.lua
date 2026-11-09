-- SPDX-License-Identifier: GPL-3.0-or-later
-- Bounded file IO shared by transactions, recovery and runtime inspection.
local nixio = require "nixio"
local fs = require "nixio.fs"
local logic = require "luci.model.ocserv_easy.logic"
local M = { limit = 262144 }

local function open_read(path)
    local f, _, code = io.open(path, "rb")
    if not f then
        -- Only ENOENT means absent; failed snapshots must never become empty
        -- backups or make an outstanding recovery journal disappear.
        logic.require(code == 2, "read_failed")
        return nil
    end
    return f
end

function M.read(path, limit)
    local f = open_read(path)
    if not f then return nil end
    local data, err = f:read((limit or M.limit) + 1)
    f:close()
    logic.require(err == nil, "read_failed")
    data = data or "" -- A successful read at EOF is an empty, existing file.
    logic.require(#data <= (limit or M.limit), "file_too_large")
    return data
end

-- Scan bounded lines without retaining the file. A false callback result stops
-- successfully; absent files return nil, and IO/budget/callback errors propagate.
function M.scan_lines(path, visit, limit)
    local f = open_read(path)
    if not f then return nil end
    local ok, result = pcall(function()
        local tail, size = "", 0
        limit = limit or M.limit
        while true do
            local chunk, err = f:read(math.min(4096, limit - size + 1))
            logic.require(err == nil, "read_failed")
            if not chunk or chunk == "" then
                if tail ~= "" then visit(tail) end
                return true
            end
            size = size + #chunk
            logic.require(size <= limit, "file_too_large")
            local data, first = tail .. chunk, 1
            while true do
                local last = data:find("\n", first, true)
                if not last then break end
                logic.require(last - first <= 4096, "file_too_large")
                if visit(data:sub(first, last - 1)) == false then return true end
                first = last + 1
            end
            tail = data:sub(first)
            logic.require(#tail <= 4096, "file_too_large")
        end
    end)
    f:close()
    if not ok then error(result, 0) end
    return result
end

function M.directory(path)
    if not fs.stat(path) then
        logic.require(fs.mkdir(path, "700"), "write_failed")
        M.sync_parent(path)
    end
    logic.require(fs.chmod(path, "700"), "write_failed")
end

function M.sync_parent(path)
    local parent = path:match("^(.*)/[^/]+$") or "."
    local file = nixio.open(parent, "r")
    logic.require(file, "write_failed")
    local ok = file:sync()
    file:close()
    logic.require(ok, "write_failed")
end

function M.remove(path, code)
    logic.require(fs.remove(path), code or "write_failed")
    M.sync_parent(path)
end

function M.random(count)
    local source = nixio.open("/dev/urandom", "r")
    logic.require(source, "random_failed")
    local data = source:read(count)
    source:close()
    logic.require(data and #data == count, "random_failed")
    return data
end

function M.atomic(path, data, limit)
    logic.require(type(data) == "string" and #data <= (limit or M.limit), "file_too_large")
    local temp = path .. ".easy-" .. nixio.bin.hexlify(M.random(16))
    local file = nixio.open(temp, nixio.open_flags("wronly", "creat", "excl"), "600")
    logic.require(file, "write_failed")
    local offset, ok = 0, true
    while offset < #data do
        -- nixio accepts a zero-based buffer offset; retry partial writes without
        -- allocating a copy of the remaining (up to 2 MiB) transaction state.
        local count = file:write(data, offset)
        if not count or count == 0 then ok = false; break end
        offset = offset + count
    end
    if ok then ok = file:sync() end
    file:close()
    if ok then ok = fs.rename(temp, path) end
    if not ok then fs.remove(temp); logic.fail("write_failed") end
    -- fsync(file) alone does not persist the renamed directory entry.
    M.sync_parent(path)
end

function M.keep(path, data)
    if M.read(path) ~= data then M.atomic(path, data) end
end

return M
