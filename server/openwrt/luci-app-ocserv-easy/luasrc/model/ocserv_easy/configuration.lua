-- SPDX-License-Identifier: GPL-3.0-or-later
-- One durable, bounded journal. Callers hold the shared backend/guard lease.
-- Candidates are rendered by UCI in a private directory before live replacement.
local fs = require "nixio.fs"
local json = require "luci.jsonc"
local logic = require "luci.model.ocserv_easy.logic"
local storage = require "luci.model.ocserv_easy.storage"
local M = {}
local root = "/etc/ocserv/easy-transaction"
local statefile = root .. "/state.json"
local paths = { ocserv = "/etc/config/ocserv", firewall = "/etc/config/firewall", passwd = "/var/etc/ocpasswd" }

local function save(state) storage.atomic(statefile, json.stringify(state)) end
local function file(name, suffix) return root .. "/" .. name .. "." .. suffix end

function M.pending() return fs.access(statefile) == true end

function M.begin(was_running)
    logic.require(not M.pending(), "recovery_pending")
    storage.directory(root)
    local state = { version = 1, files = {}, was_running = was_running, service = false, firewall = false }
    save(state)
    return state
end

function M.mark(state, key)
    logic.require(key == "service" or key == "firewall", "rollback_failed")
    if not state[key] then state[key] = true; save(state) end
end

function M.replace(state, name, before, after)
    local path = paths[name]
    logic.require(path and type(after) == "string" and #after <= storage.limit, "file_too_large")
    logic.require((storage.read(path) or false) == (before or false), "stale_revision")
    for _, item in ipairs(state.files) do logic.require(item.name ~= name, "write_failed") end
    if before == after then return end
    if before then storage.keep(file(name, "before"), before) end
    storage.atomic(file(name, "after"), after)
    state.files[#state.files + 1] = { name = name, existed = before ~= nil and before ~= false }
    save(state) -- Recovery is durable before any live file is changed.
    storage.atomic(path, after)
end

function M.finish()
    logic.require(fs.remove(statefile), "write_failed")
end

function M.restore()
    local raw = storage.read(statefile)
    if not raw then return nil end
    local state = json.parse(raw)
    logic.require(type(state) == "table" and state.version == 1 and type(state.files) == "table" and
        #state.files <= 3 and type(state.was_running) == "boolean" and
        type(state.service) == "boolean" and type(state.firewall) == "boolean", "rollback_failed")
    local changes, seen = {}, {}
    -- Check every file before restoring any. Never overwrite a later editor.
    for _, item in ipairs(state.files) do
        logic.require(type(item) == "table" and paths[item.name] and not seen[item.name] and
            type(item.existed) == "boolean", "rollback_failed")
        seen[item.name] = true
        local before = item.existed and storage.read(file(item.name, "before")) or false
        local after = storage.read(file(item.name, "after"))
        logic.require((not item.existed or before) and after, "rollback_failed")
        local current = storage.read(paths[item.name]) or false
        logic.require(current == before or current == after, "rollback_failed")
        changes[#changes + 1] = { path = paths[item.name], before = before, current = current }
    end
    for _, item in ipairs(changes) do
        if item.current ~= item.before then
            if item.before == false then logic.require(fs.remove(item.path), "rollback_failed")
            else storage.atomic(item.path, item.before) end
        end
    end
    return state -- Keep the journal until runtime services have also recovered.
end

return M
