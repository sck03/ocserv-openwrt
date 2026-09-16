-- SPDX-License-Identifier: GPL-3.0-or-later
local nixio = require "nixio"
local fs = require "nixio.fs"
local uci = require "luci.model.uci"
local json = require "luci.jsonc"
local logic = require "luci.model.ocserv_easy.logic"
local guard = require "luci.model.ocserv_easy.guard"
local run = require("luci.model.ocserv_easy.process").run
local M = {}
local config_path="/etc/config/ocserv"
local template_path="/etc/ocserv/ocserv.conf.template"
local local_path="/etc/ocserv/ocserv.conf.local"
local passwd_path="/var/etc/ocpasswd"
local runtime_path="/var/etc/ocserv.conf"
local work="/var/run/ocserv-easy"
local occtl="/usr/bin/occtl"

local function read(path, limit)
    local f=io.open(path,"rb")
    if not f then return nil end
    local data=f:read((limit or 262144)+1) or ""; f:close()
    logic.require(#data<=(limit or 262144),"file_too_large")
    return data
end
local function directory(path)
    if not fs.stat(path) then logic.require(fs.mkdir(path,"700"),"write_failed") end
    logic.require(fs.chmod(path,"700"),"write_failed")
end
local function random_bytes(count)
    local source=nixio.open("/dev/urandom","r")
    logic.require(source,"random_failed")
    local value=source:read(count); source:close()
    logic.require(value and #value==count,"random_failed")
    return value
end
local function atomic(path,data)
    local tmp=path..".easy-"..nixio.getpid().."-"..nixio.bin.hexlify(random_bytes(8))
    local out=nixio.open(tmp,nixio.open_flags("wronly","creat","excl"),"600")
    logic.require(out,"write_failed")
    local offset=1; local ok=true
    while offset<=#data do
        local written=out:write(data:sub(offset))
        if not written or written==0 then ok=false; break end
        offset=offset+written
    end
    if ok then ok=out:sync() end
    out:close()
    if ok then ok=fs.rename(tmp,path) end
    if not ok then fs.remove(tmp); logic.fail("write_failed") end
end

local function version()
    local code,output=run({"/usr/sbin/ocserv","--version"},3)
    return code==0 and (output:match("OpenConnect VPN Server%s+([^%s]+)") or output:match("ocserv%s+([^%s]+)")) or "unknown"
end
local function running()
    local pid=(read("/var/run/ocserv.pid",64) or ""):match("^%s*(%d+)%s*$")
    if not pid then return false end
    -- ocserv rewrites argv to "ocserv-main"; /proc/PID/exe still identifies it.
    local executable=fs.readlink("/proc/"..pid.."/exe")
    return executable=="/usr/sbin/ocserv" or executable=="/usr/sbin/ocserv (deleted)"
end
local function capabilities()
    local _,help=run({occtl,"--help"},3)
    -- Some occtl releases print only switches for --help; command help is separate.
    if not help:find("terminate user",1,true) then local _,commands=run({occtl,"help"},3); help=help..commands end
    return {terminate=help:find("terminate user",1,true)~=nil}
end
local function snapshot()
    local c=uci.cursor()
    logic.require(c:load("ocserv") and c:get("ocserv","config")=="ocserv","unknown_schema")
    local users,dns,routes={},{},{}
    c:foreach("ocserv","ocservusers",function(s) users[#users+1]={id=s[".name"],name=s.name,group=s.group,password=s.password} end)
    c:foreach("ocserv","dns",function(s) dns[#dns+1]={id=s[".name"],ip=s.ip} end)
    c:foreach("ocserv","routes",function(s) routes[#routes+1]={id=s[".name"],ip=s.ip,netmask=s.netmask} end)
    local raw=read(config_path) or ""
    local template=read(template_path) or ""
    local extra=read(local_path) or ""
    return {cursor=c,config=c:get_all("ocserv","config"),users=users,dns=dns,routes=routes,raw=raw,template=template,extra=extra,
        revision=logic.revision(raw.."\0"..template.."\0"..extra)}
end
local function safe(value,limit) return type(value)=="string" and value:sub(1,limit or 128) or "" end
function M.status()
    local result={running=running(),online={}}
    if not result.running then return result end
    local code,output=run({occtl,"-j","show","users"},4)
    local parsed=code==0 and json.parse(output) or nil
    if type(parsed)~="table" then result.online_error=true; return result end
    for _,row in ipairs(parsed) do
        if type(row)=="table" then
            local id=tonumber(row.ID)
            if id and id>0 then
                result.online[#result.online+1]={id=tostring(id),name=safe(row.Username,64),group=safe(row.Groupname),
                    ip=safe(row["Remote IP"]),vpn_ip=safe(row["VPN IP"] or row["IPv4"] or row["VPN IPv4"] or row["IPv4 Address"]),
                    since=safe(row["Connected at"] or row["Connected"]),state=safe(row.State)}
            end
        end
    end
    return result
end
function M.data(admin)
    local s=snapshot(); local state=M.status()
    state.version=version()
    state.ui_version="0.4.1"
    state.supported=logic.supported_version(state.version)
    state.capabilities=capabilities()
    state.revision=s.revision
    state.users=logic.public_users(s.users)
    state.settings=logic.settings(s.config,s.dns,s.routes)
    state.auth=s.config.auth or "plain"
    state.settings_supported=state.auth=="plain" and s.config.proxy_arp~="1"
    state.autostart=run({"/etc/init.d/ocserv","enabled"},3)==0
    state.ca_available=(read("/etc/ocserv/ca.pem",65536) or ""):find("BEGIN CERTIFICATE",1,true)~=nil
    state.guard=guard.status(admin)
    return state
end
local function hash_password(password)
    local alphabet="./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    local bytes=random_bytes(16); local salt={}
    for i=1,16 do local p=bytes:byte(i)%64+1; salt[i]=alphabet:sub(p,p) end
    local hashed=nixio.crypt(password,"$6$"..table.concat(salt).."$")
    logic.require(type(hashed)=="string" and hashed:match("^%$6%$[^$]+%$[./A-Za-z0-9]+$"),"hash_failed")
    return hashed
end
local function write_users(s,users)
    local keep={}
    for _,user in ipairs(users) do
        local id=user.id
        if not id then id=s.cursor:add("ocserv","ocservusers"); logic.require(id,"write_failed") end
        keep[id]=true
        if user.name then
            logic.require(s.cursor:set("ocserv",id,"name",user.name),"write_failed")
            logic.require(s.cursor:set("ocserv",id,"group",user.group or "*"),"write_failed")
            logic.require(s.cursor:set("ocserv",id,"password",user.password),"write_failed")
        end
    end
    for _,user in ipairs(s.users) do if not keep[user.id] then logic.require(s.cursor:delete("ocserv",user.id),"write_failed") end end
end
local function commit(s)
    -- Do not overwrite changes made in the original LuCI page since this request began.
    logic.require((read(config_path) or "")==s.raw,"stale_revision")
    logic.require(s.cursor:commit("ocserv"),"write_failed")
    s.committed_raw=read(config_path)
    logic.require(fs.chmod(config_path,"600"),"write_failed")
end
local function backup(s,old_passwd)
    local path="/etc/ocserv/easy-backup"
    directory(path)
    atomic(path.."/ocserv.uci",s.raw)
    atomic(path.."/ocserv.conf.template",s.template)
    atomic(path.."/ocserv.conf.local",s.extra)
    if old_passwd then atomic(path.."/ocpasswd",old_passwd) end
end
local function transaction(s,was_running,operation)
    local old_passwd=read(passwd_path)
    backup(s,old_passwd)
    local touched=false
    local function mark_service() touched=true end
    local ok,result=pcall(operation,mark_service)
    if ok then return result end
    local restored=pcall(function()
        -- A stale request must never roll back somebody else's later commit.
        if s.committed_raw then
            logic.require(read(config_path)==s.committed_raw,"rollback_failed")
            atomic(config_path,s.raw)
        end
        s.cursor:revert("ocserv"); s.cursor:unload("ocserv")
        if s.committed_raw then
            if old_passwd then atomic(passwd_path,old_passwd) else fs.remove(passwd_path) end
        end
        if touched then
            logic.require(run({"/etc/init.d/ocserv",was_running and "restart" or "stop"},15)==0,"rollback_failed")
            logic.require(not was_running or running(),"rollback_failed")
        end
    end)
    if not restored then logic.fail("rollback_failed") end
    error(result,0)
end
local function restart(mark)
    mark()
    logic.require(run({"/etc/init.d/ocserv","restart"},15)==0,"restart_failed")
    -- procd may return before the daemon writes its pidfile.
    for _=1,20 do if running() then return end nixio.poll({},100) end
    logic.fail("restart_failed")
end
local function change_user(s,request)
    logic.require((s.config.auth or "plain")=="plain","plain_auth_required")
    local was_running=running()
    if was_running then logic.require(logic.standard_auth(read(runtime_path) or ""),"custom_auth_file") end
    local users,revoke=logic.change_user(s.users,request,hash_password)
    if logic.same_users(users,s.users) then return {effect="unchanged"} end
    if was_running and revoke then logic.require(capabilities().terminate,"upgrade_required") end
    return transaction(s,was_running,function(mark)
        write_users(s,users); commit(s)
        if not fs.stat("/var/etc") then logic.require(fs.mkdir("/var/etc","755"),"write_failed") end
        atomic(passwd_path,logic.passwd(users))
        local effect=was_running and "updated_live" or "saved"
        if was_running and revoke then
            local code=run({occtl,"terminate","user",revoke},5)
            if code~=0 then
                -- occtl also returns 1 when an offline user has no remaining cookies.
                -- Verify the complete session list before treating that case as success.
                local rc,output=run({occtl,"-j","show","sessions","all"},5)
                local sessions=rc==0 and json.parse(output) or nil
                logic.require(type(sessions)=="table","terminate_failed")
                for key,row in pairs(sessions) do
                    logic.require(type(key)=="number" and type(row)=="table" and row.Username~=revoke,"terminate_failed")
                end
            end
            effect="terminated"
        end
        return {effect=effect}
    end)
end
local function repair_users(s)
    logic.require((s.config.auth or "plain")=="plain","plain_auth_required")
    local was_running=running()
    if was_running then logic.require(logic.standard_auth(read(runtime_path) or ""),"custom_auth_file") end
    local users,converted,invalid=logic.repair_users(s.users,hash_password)
    return transaction(s,was_running,function()
        if not logic.same_users(users,s.users) then write_users(s,users); commit(s) end
        if not fs.stat("/var/etc") then logic.require(fs.mkdir("/var/etc","755"),"write_failed") end
        atomic(passwd_path,logic.passwd(users))
        return {effect="accounts_repaired",converted=converted,needs_password=invalid}
    end)
end
local managed={"tcp-port","udp-port","max-clients","max-same-clients","dpd","ipv4-network","ipv4-netmask","ipv6-network","dns","route","auth","compression","predictable-ips","cisco-client-compat","default-domain"}
local function apply_settings(s,request)
    local values,dns,routes=logic.validate_settings(request.settings)
    for _,key in ipairs(managed) do
        local pattern="^%s*"..key:gsub("%-","%%-").."%s*="
        for line in (s.extra.."\n"):gmatch("([^\n]*)\n") do logic.require(not line:match(pattern),"custom_override") end
    end
    local config={}; for key,value in pairs(s.config) do config[key]=value end
    for key,value in pairs(values) do config[key]=value end
    local domain=s.cursor:get_first("dhcp","dnsmasq","domain","")
    local dyndns=false
    s.cursor:foreach("ddns","service",function(row) if row.domain and row.domain~="" then dyndns=true end end)
    local candidate=logic.render(s.template,s.extra,config,dns,routes,{domain=domain,dyndns=dyndns})
    logic.require(logic.standard_auth(candidate),"custom_auth_file")
    local was_running=running()
    if was_running then logic.require(request.allow_restart==true,"restart_confirmation_required") end
    local test_config=work.."/check.conf"; local test_passwd=work.."/check.passwd"
    atomic(test_passwd,logic.passwd(s.users))
    atomic(test_config,(candidate:gsub("/var/etc/ocpasswd",test_passwd)))
    local code=run({"/usr/sbin/ocserv","--test-config","--config",test_config},8)
    fs.remove(test_config); fs.remove(test_passwd)
    logic.require(code==0,"config_check_failed")
    return transaction(s,was_running,function(mark)
        for key,value in pairs(values) do logic.require(s.cursor:set("ocserv","config",key,value),"write_failed") end
        for _,row in ipairs(s.dns) do logic.require(s.cursor:delete("ocserv",row.id),"write_failed") end
        for _,row in ipairs(s.routes) do logic.require(s.cursor:delete("ocserv",row.id),"write_failed") end
        for _,row in ipairs(dns) do
            local id=s.cursor:add("ocserv","dns"); logic.require(id and s.cursor:set("ocserv",id,"ip",row.ip),"write_failed")
        end
        for _,row in ipairs(routes) do
            local id=s.cursor:add("ocserv","routes")
            logic.require(id and s.cursor:set("ocserv",id,"ip",row.ip) and s.cursor:set("ocserv",id,"netmask",row.netmask),"write_failed")
        end
        commit(s)
        if was_running then restart(mark) end
        return {effect=was_running and "restarted" or "saved"}
    end)
end
function M.action(request,admin)
    logic.require(type(request)=="table" and type(request.action)=="string","bad_request")
    directory(work)
    local lock=nixio.open(work.."/lock","w","600")
    logic.require(lock,"write_failed")
    if not lock:lock("tlock") then lock:close(); logic.fail("busy") end
    local ok,result=pcall(function()
        local s=snapshot()
        logic.require(request.revision==s.revision,"stale_revision")
        logic.require(logic.supported_version(version()),"upgrade_required")
        -- Pending UCI deltas from another editor must be reviewed there first.
        local changes=s.cursor:changes("ocserv")
        logic.require(not changes or not next(changes.ocserv or changes),"pending_changes")
        if request.action=="save_user" or request.action=="delete_user" then return change_user(s,request) end
        if request.action=="repair_users" then return repair_users(s) end
        if request.action=="guard" then return guard.begin(request.command,admin,request.token) end
        if request.action=="settings" then
            logic.require(not guard.active(),"guard_settings_locked")
            return apply_settings(s,request)
        end
        if request.action=="disconnect" then
            logic.require(type(request.id)=="string" and request.id:match("^%d+$") and tonumber(request.id)>0 and tonumber(request.id)<=2147483647,"bad_request")
            local code,output=run({occtl,"disconnect","id",request.id},5)
            logic.require(code==0 and not output:lower():find("could not",1,true),"disconnect_failed")
            return {effect="disconnected"}
        end
        if request.action=="service" then
            local command=request.command
            logic.require(command=="start" or command=="stop" or command=="restart" or command=="enable" or command=="disable","bad_action")
            if running() and (command=="stop" or command=="restart") then logic.require(request.allow_restart==true,"restart_confirmation_required") end
            logic.require(run({"/etc/init.d/ocserv",command},15)==0,"service_failed")
            return {effect="service_changed"}
        end
        logic.fail("bad_action")
    end)
    lock:lock("ulock"); lock:close()
    if not ok then error(result,0) end
    return result
end
function M.repair_users()
    local s=snapshot()
    if (s.config.auth or "plain")~="plain" then return {effect="unchanged",converted=0,needs_password=0} end
    return M.action({action="repair_users",revision=s.revision})
end
function M.export(kind,url)
    logic.require(kind=="ca" or kind=="profile" or kind=="address","bad_request")
    local normalized
    if kind~="ca" then
        if type(url)=="string" and not url:find("://",1,true) then url="https://"..url end
        normalized=logic.url(url)
        logic.require(normalized,"invalid_url")
        if kind=="address" then return "[VPN]\nServer="..normalized.."\n","BulijieVPN.bvpn" end
    end
    local ca=read("/etc/ocserv/ca.pem",65536)
    logic.require(ca and ca:find("-----BEGIN CERTIFICATE-----",1,true) and ca:find("-----END CERTIFICATE-----",1,true) and not ca:find("PRIVATE KEY",1,true),"ca_unavailable")
    if kind=="ca" then return ca,"ca.pem" end
    return "[VPN]\nServer="..normalized.."\nCABase64="..nixio.bin.b64encode(ca).."\n","BulijieVPN.bvpn"
end
return M
