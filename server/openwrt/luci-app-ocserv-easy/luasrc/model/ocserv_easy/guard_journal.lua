-- SPDX-License-Identifier: GPL-3.0-or-later
-- Recovery always reads the journal; polling uses a disposable, identity-bound projection.
local json=require "luci.jsonc"
local logic=require "ocserv.logic"
local storage=require "ocserv.storage"
local schema=require "luci.model.ocserv_easy.guard_state"
local cache=require "luci.model.ocserv_easy.query_cache"
local M={}
local path=schema.root.."/state.json"

function M.read()
    local raw=storage.read(path,2097152)
    if not raw then return nil end
    logic.require(not logic.json_has_null(raw),"guard_state_invalid")
    local state=json.parse(raw)
    schema.validate(state)
    return state
end

function M.write(state)
    cache.invalidate("guard-state")
    storage.atomic(path,json.stringify(state),2097152)
end

function M.status()
    if not storage.readable(path) then return nil end
    local identity=cache.identity(path)
    local function valid(value)
        return type(value.phase)=="string" and value.enabled==true and type(value.admin)=="string" and
            type(value.pool)=="string" and type(value.lan)=="string" and type(value.proxy)=="string" and
            type(value.deadline)=="number" and (value.token==nil or type(value.token)=="string") and
            (value.reason==nil or type(value.reason)=="string")
    end
    local value=cache.peek("guard-state",identity,60,4096,valid)
    if value then return value end
    -- Validate before any cache write, preserving a damaged journal's entire scene.
    local state=M.read()
    logic.require(state and cache.identity(path)==identity,"guard_state_invalid")
    return cache.get("guard-state",identity,60,function()
        logic.require(cache.identity(path)==identity,"guard_state_invalid")
        return {phase=state.phase,enabled=true,proxy=state.info.proxy,lan=state.info.lan,
            admin=state.info.admin,pool=state.info.pool.."/"..state.info.prefix,
            deadline=state.deadline,token=state.phase=="pending" and state.token or nil,reason=state.failure}
    end,false,4096,nil,valid)
end
return M
