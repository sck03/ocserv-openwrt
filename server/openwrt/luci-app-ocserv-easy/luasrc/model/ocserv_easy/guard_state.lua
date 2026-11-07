-- SPDX-License-Identifier: GPL-3.0-or-later
-- Pure recovery schema. Validate the whole journal before any runtime action.
local logic=require "luci.model.ocserv_easy.logic"
local registry=require "luci.model.ocserv_easy.proxy_registry"
local M={version=3,root="/etc/ocserv/easy-guard",configs={"firewall","dhcp"}}
for _,name in ipairs(registry.names(true)) do M.configs[#M.configs+1]=name end
M.configs[#M.configs+1]="ocserv"
local phases={queued=true,applying=true,pending=true,enabled=true,restoring=true,recovery_failed=true}

local function identifier(value)
    return logic.text(value,128) and value~=""
end
local function option_value(value)
    if value==false or type(value)=="string" then return true end
    if not logic.array(value) then return false end
    for _,item in ipairs(value) do if type(item)~="string" then return false end end
    return true
end
local function section_value(value)
    if value==false then return true end
    if type(value)~="table" or not identifier(value[".type"]) then return false end
    for key,item in pairs(value) do
        if not identifier(key) or not option_value(item) then return false end
    end
    return true
end
function M.validate(state)
    local function valid(value) logic.require(value,"guard_state_invalid") end
    valid(type(state)=="table" and state.version==M.version and phases[state.phase] and
        logic.array(state.ops) and #state.ops>0 and logic.array(state.files,3) and type(state.before)=="table" and
        type(state.info)=="table" and type(state.was_running)=="boolean" and type(state.started)=="boolean" and
        type(state.deadline)=="number" and state.deadline>=0 and state.deadline<math.huge)
    local info=state.info
    local adapter=registry.get(info.proxy)
    valid(adapter and adapter.adapter and logic.ipv4(info.lan) and logic.ipv4(info.admin) and
        logic.ipv4(info.pool) and type(info.prefix)=="number" and info.prefix%1==0 and info.prefix>=8 and info.prefix<=30)
    if state.phase=="applying" or state.phase=="pending" or state.phase=="enabled" then valid(state.started) end
    if state.phase=="queued" then valid(not state.started) end
    if state.phase=="queued" or state.phase=="applying" or state.phase=="pending" then valid(identifier(state.token)) end
    local packages={}
    for _,name in ipairs(M.configs) do
        valid(state.before[name]==false or type(state.before[name])=="string")
        packages[name]=true
    end
    for name in pairs(state.before) do valid(packages[name]) end
    local seen={}
    for _,op in ipairs(state.ops) do
        valid(type(op)=="table" and packages[op.package] and identifier(op.id) and
            (op.key==nil or identifier(op.key)))
        local key=op.package.."\0"..op.id.."\0"..(op.key or "")
        valid(not seen[key]); seen[key]=true
        local value=op.key and option_value or section_value
        valid(value(op.before) and value(op.after))
    end
    local files={["/etc/ocserv/ocserv.conf.local"]=true,[M.root.."/guard.nft"]=true}
    if adapter.adapter=="ucode" then files["/etc/"..info.proxy.."/ucode/hijack.ut"]=true end
    for _,file in ipairs(state.files) do
        valid(type(file)=="table" and files[file.path] and
            (file.before==false or type(file.before)=="string") and type(file.after)=="string")
        files[file.path]=nil
    end
    valid(next(files)==nil)
end
return M
