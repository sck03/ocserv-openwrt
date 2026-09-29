-- SPDX-License-Identifier: GPL-3.0-or-later
-- Structured diagnostics only: never persist command output, UCI or credentials.
local nixio=require "nixio"
local fs=require "nixio.fs"
local json=require "luci.jsonc"
local M={}
local directory="/var/run/ocserv-easy"
local path=directory.."/guard-log.json"
local ttl=86400
local function access(event)
    if not fs.stat(directory) then assert(fs.mkdir(directory,"700")) end
    assert(fs.chmod(directory,"700"))
    local lock=assert(nixio.open(directory.."/guard-log.lock","w","600"))
    local acquired=false
    for _=1,100 do
        if lock:lock("tlock") then acquired=true; break end
        nixio.poll({},10)
    end
    if not acquired then lock:close(); error("log busy") end
    local ok,result=pcall(function()
        local f=io.open(path,"rb")
        local raw=f and f:read(262145); if f then f:close() end
        local rows=raw and #raw<=262144 and json.parse(raw) or {}
        if type(rows)~="table" then rows={} end
        local kept,now={},os.time()
        for _,row in ipairs(type(rows)=="table" and rows or {}) do
            if type(row)=="table" and type(row.time)=="number" and row.time<=now and row.time>now-ttl then kept[#kept+1]=row end
        end
        if event then event.time=now; kept[#kept+1]=event end
        while #kept>256 do table.remove(kept,1) end
        if #kept==0 then
            if raw then assert(fs.remove(path)) end
        elseif event or #kept~=#rows then
            local temp=path..".new"
            local out=assert(nixio.open(temp,nixio.open_flags("wronly","creat","trunc"),"600"))
            local value=json.stringify(kept); local offset=1
            while offset<=#value do local n=out:write(value:sub(offset)); if not n or n==0 then out:close(); error("log write failed") end; offset=offset+n end
            out:close(); assert(fs.rename(temp,path))
        end
        return kept
    end)
    lock:lock("ulock"); lock:close()
    if not ok then error(result,0) end
    return result
end
function M.read() return {entries=access(),retention_hours=24} end
function M.add(step,code,detail)
    -- Logging failure must not stop a restoration.
    local ok=pcall(access,{step=step,code=code,detail=detail})
    if not ok then pcall(nixio.syslog,"err","ocserv-easy: guard diagnostic log write failed") end
end
function M.diagnostic(output)
    local value=(output or ""):lower()
    for _,hint in ipairs({"address already in use","permission denied","no such file or directory","syntax error","connection refused","out of memory","failed to bind"}) do
        if value:find(hint,1,true) then return hint end
    end
end
function M.clean_loop()
    while true do
        local ok,rows=pcall(access)
        local seconds=60
        if ok and rows[1] then seconds=math.max(1,math.min(seconds,rows[1].time+ttl-os.time())) end
        nixio.poll({},seconds*1000)
    end
end
return M
