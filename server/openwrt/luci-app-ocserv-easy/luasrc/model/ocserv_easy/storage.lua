-- SPDX-License-Identifier: GPL-3.0-or-later
-- Bounded file IO shared by configuration transactions and their recovery.
local nixio = require "nixio"
local fs = require "nixio.fs"
local logic = require "luci.model.ocserv_easy.logic"
local M = { limit = 262144 }

function M.read(path, limit)
    local f = io.open(path, "rb")
    if not f then return nil end
    local data = f:read((limit or M.limit) + 1) or ""
    f:close()
    logic.require(#data <= (limit or M.limit), "file_too_large")
    return data
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
    local offset, ok = 1, true
    while offset <= #data do
        local count = file:write(data:sub(offset))
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
