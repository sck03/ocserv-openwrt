-- SPDX-License-Identifier: GPL-3.0-or-later
local nixio = require "nixio"
local fs = require "nixio.fs"
local uci = require "luci.model.uci"
local json = require "luci.jsonc"
local logic = require "luci.model.ocserv_easy.logic"
local guard = require "luci.model.ocserv_easy.guard"
local firewall = require "luci.model.ocserv_easy.firewall"
local run = require("luci.model.ocserv_easy.process").run
local storage = require "luci.model.ocserv_easy.storage"
local configuration = require "luci.model.ocserv_easy.configuration"
local query_cache = require "luci.model.ocserv_easy.query_cache"
local proxy = require "luci.model.ocserv_easy.proxy"
local read, atomic, directory, random_bytes = storage.read, storage.atomic, storage.directory, storage.random
local M = {}
local config_path="/etc/config/ocserv"
local template_path="/etc/ocserv/ocserv.conf.template"
local local_path="/etc/ocserv/ocserv.conf.local"
local passwd_path="/var/etc/ocpasswd"
local runtime_path="/var/etc/ocserv.conf"
local firewall_path="/etc/config/firewall"
local work="/var/run/ocserv-easy"
local occtl="/usr/bin/occtl"

local function version(fresh)
    if not fresh then
        return query_cache.get("version",query_cache.identity("/usr/sbin/ocserv"),300,function()
            local value=version(true)
            return {version=value,cacheable=value~="unknown"}
        end).version
    end
    local code,output=run({"/usr/sbin/ocserv","--version"},10)
    return code==0 and output:match("OpenConnect VPN Server%s+([^%s]+)") or "unknown"
end
local function running()
    local pid=(read("/var/run/ocserv.pid",64) or ""):match("^%s*(%d+)%s*$")
    if not pid then return false end
    -- ocserv rewrites argv to "ocserv-main"; /proc/PID/exe still identifies it.
    local executable=fs.readlink("/proc/"..pid.."/exe")
    return executable=="/usr/sbin/ocserv" or executable=="/usr/sbin/ocserv (deleted)"
end
local function capabilities()
    return query_cache.get("capabilities",query_cache.identity(occtl),300,function()
        local code,help=run({occtl,"--help"},3)
        -- Some occtl releases print only switches for --help; command help is separate.
        if not help:find("terminate user",1,true) then
            local commands; code,commands=run({occtl,"help"},3); help=help..commands
        end
        local terminate=help:find("terminate user",1,true)~=nil
        -- A temporary process failure is not evidence of an unsupported server.
        return {terminate=terminate,cacheable=terminate or code==0}
    end)
end
local function snapshot()
    local c=uci.cursor()
    logic.require(c:load("ocserv") and c:get("ocserv","config")=="ocserv","unknown_schema")
    local firewall_loaded=c:load("firewall") == true
    local users,dns,routes={},{},{}
    c:foreach("ocserv","ocservusers",function(s) users[#users+1]={id=s[".name"],name=s.name,group=s.group,password=s.password} end)
    c:foreach("ocserv","dns",function(s) dns[#dns+1]={id=s[".name"],ip=s.ip} end)
    c:foreach("ocserv","routes",function(s) routes[#routes+1]={id=s[".name"],ip=s.ip,netmask=s.netmask} end)
    local raw=read(config_path) or ""
    local firewall_raw=read(firewall_path)
    local template=read(template_path) or ""
    local extra=read(local_path) or ""
    return {cursor=c,config=c:get_all("ocserv","config"),users=users,dns=dns,routes=routes,raw=raw,
        firewall_loaded=firewall_loaded,firewall_raw=firewall_raw,template=template,extra=extra,
        revision=logic.revision(raw.."\0"..(firewall_raw or "").."\0"..template.."\0"..extra)}
end
local function safe(value,limit) return type(value)=="string" and value:sub(1,limit or 128) or "" end
function M.status(fresh)
    local result={running=running(),online={},recovery_pending=configuration.pending()}
    if not result.running then return result end
    local identity=read("/var/run/ocserv.pid",64) or ""
    local online=query_cache.get("online",identity,5,function()
    local code,output=run({occtl,"-j","show","users"},4)
    local parsed=code==0 and json.parse(output) or nil
    if type(parsed)~="table" then return {online={},online_error=true} end
    local rows={}
    for _,row in ipairs(parsed) do
        if type(row)=="table" then
            local id=tonumber(row.ID)
            if id and id>0 then
                rows[#rows+1]={id=tostring(id),name=safe(row.Username,64),group=safe(row.Groupname),
                    ip=safe(row["Remote IP"]),vpn_ip=safe(row["VPN IP"] or row["IPv4"] or row["VPN IPv4"] or row["IPv4 Address"]),
                    since=safe(row["Connected at"] or row["Connected"]),state=safe(row.State)}
            end
        end
    end
    return {online=rows}
    end,fresh)
    result.online=online.online
    result.online_error=online.online_error
    return result
end
function M.data(admin)
    local s=snapshot(); local state=M.status(true)
    state.version=version()
    state.ui_version="0.6.4"
    state.supported=logic.supported_version(state.version)
    state.capabilities=capabilities()
    state.revision=s.revision
    state.firewall_sync=s.firewall_loaded and s.firewall_raw~=nil
    state.users=logic.public_users(s.users)
    state.settings=logic.settings(s.config,s.dns,s.routes)
    state.auth=s.config.auth or "plain"
    state.settings_supported=state.auth=="plain" and s.config.proxy_arp~="1"
    state.autostart=run({"/etc/init.d/ocserv","enabled"},3)==0
    local ca=read("/etc/ocserv/ca.pem",65536) or ""
    local certificate=read(s.config.server_cert or "/etc/ocserv/server-cert.pem",65536) or ""
    local key=fs.stat(s.config.server_key or "/etc/ocserv/server-key.pem")
    state.ca_available=ca:find("-----BEGIN CERTIFICATE-----",1,true)~=nil and not ca:find("PRIVATE KEY",1,true)
    state.certificates_ready=certificate:find("-----BEGIN CERTIFICATE-----",1,true)~=nil and key~=nil and (key.size or 0)>0
    state.installation_pending=fs.access("/etc/ocserv/easy-install-pending")==true
    state.client_profile={cert_mode=s.config.easy_cert_mode or "pin"}
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
    logic.require(s.cursor:commit("ocserv"),"write_failed")
    local candidate=read(s.stage.."/ocserv")
    logic.require(candidate,"write_failed")
    configuration.replace(s.journal,"ocserv",s.raw,candidate)
end
local function commit_firewall(s)
    logic.require(s.firewall_loaded and s.firewall_raw~=nil,"firewall_unavailable")
    logic.require(s.cursor:commit("firewall"),"write_failed")
    local candidate=read(s.stage.."/firewall")
    logic.require(candidate,"write_failed")
    configuration.replace(s.journal,"firewall",s.firewall_raw,candidate)
end
local function backup(s,old_passwd,include_firewall)
    local path="/etc/ocserv/easy-backup"
    directory(path)
    storage.keep(path.."/ocserv.uci",s.raw)
    storage.keep(path.."/ocserv.conf.template",s.template)
    storage.keep(path.."/ocserv.conf.local",s.extra)
    if old_passwd then storage.keep(path.."/ocpasswd",old_passwd) end
    if include_firewall and s.firewall_raw then storage.keep(path.."/firewall.uci",s.firewall_raw) end
end
local function reload_firewall(mark)
    mark()
    logic.require(run({"/sbin/fw4","check"},10)==0,"firewall_check_failed")
    logic.require(run({"/etc/init.d/firewall","reload"},20)==0,"firewall_reload_failed")
end
local wait_running
local function wait_proxy(name,managed,code)
    for _=1,30 do if proxy.ready(name,managed) then return end nixio.poll({},1000) end
    logic.fail(code)
end
local function recover_locked(boot)
    local state=configuration.restore()
    if not state then return end
    if not boot and state.firewall then
        logic.require(run({"/sbin/fw4","check"},10)==0,"rollback_failed")
        logic.require(run({"/etc/init.d/firewall","reload"},20)==0,"rollback_failed")
    end
    if not boot and state.service then
        logic.require(run({"/etc/init.d/ocserv",state.was_running and "restart" or "stop"},15)==0,"rollback_failed")
        wait_running(state.was_running,"rollback_failed")
    end
    if not boot and state.proxy then
        logic.require(run({"/etc/init.d/"..state.proxy,"restart"},90)==0,"rollback_failed")
        wait_proxy(state.proxy,false,"rollback_failed")
    end
    configuration.finish()
end
local function stage_cursor(s,include_firewall,proxy_name)
    s.stage=work.."/config-stage"
    directory(s.stage); directory(s.stage.."/delta")
    for _,package in ipairs({"ocserv","firewall","nikki","momo"}) do
        -- Fixed paths under the shared lease; clear only our private UCI deltas.
        fs.remove(s.stage.."/delta/"..package)
    end
    atomic(s.stage.."/ocserv",s.raw)
    if include_firewall then atomic(s.stage.."/firewall",s.firewall_raw) end
    if proxy_name then atomic(s.stage.."/"..proxy_name,s.proxy_raw) end
    -- The LuCI rpcd bridge ignores cursor directory arguments and commits to
    -- /etc/config. Only the native binding can build a private candidate.
    s.cursor=require("uci").cursor(s.stage,s.stage.."/delta")
    logic.require(s.cursor:get_confdir()==s.stage and s.cursor:get_savedir()==s.stage.."/delta","write_failed")
    logic.require(s.cursor:load("ocserv"),"write_failed")
    if include_firewall then logic.require(s.cursor:load("firewall"),"write_failed") end
    if proxy_name then logic.require(s.cursor:load(proxy_name),"write_failed") end
end
local function clear_stage(s)
    for _,package in ipairs({"ocserv","firewall","nikki","momo"}) do
        fs.remove(s.stage.."/"..package); fs.remove(s.stage.."/delta/"..package)
    end
end
local function transaction(s,was_running,operation,include_firewall,proxy_name,prepared)
    local old_passwd=read(passwd_path)
    s.old_passwd=old_passwd
    backup(s,old_passwd,include_firewall)
    if not prepared then stage_cursor(s,include_firewall,proxy_name) end
    s.journal=configuration.begin(was_running)
    local function mark_service() configuration.mark(s.journal,"service") end
    local function mark_firewall() configuration.mark(s.journal,"firewall") end
    local ok,result=pcall(operation,mark_service,mark_firewall)
    if ok then
        configuration.finish()
        clear_stage(s)
        return result
    end
    local restored=pcall(function()
        s.cursor:revert("ocserv"); s.cursor:unload("ocserv")
        if include_firewall then s.cursor:revert("firewall"); s.cursor:unload("firewall") end
        if proxy_name then s.cursor:revert(proxy_name); s.cursor:unload(proxy_name) end
        recover_locked(false)
    end)
    clear_stage(s)
    if not restored then logic.fail("rollback_failed") end
    error(result,0)
end
wait_running=function(expected,code)
    -- procd may return before the daemon writes its pidfile.
    for _=1,30 do if running()==expected then return end nixio.poll({},100) end
    logic.fail(code)
end
local function initcerts()
    -- First-time RSA generation on routers can exceed a normal service timeout.
    logic.require(run({"/etc/init.d/ocserv","initcerts"},90)==0,"certificate_init_failed")
end
local function restart(mark)
    mark()
    logic.require(run({"/etc/init.d/ocserv","restart"},15)==0,"restart_failed")
    wait_running(true,"restart_failed")
end
local function change_user(s,request)
    logic.require((s.config.auth or "plain")=="plain","plain_auth_required")
    local was_running=running()
    if was_running then logic.require(logic.standard_auth(read(runtime_path) or ""),"custom_auth_file") end
    local users,revoke=logic.change_user(s.users,request,hash_password)
    if logic.equal(users,s.users) then return {effect="unchanged"} end
    if was_running and revoke then logic.require(capabilities().terminate,"upgrade_required") end
    return transaction(s,was_running,function(mark)
        write_users(s,users); commit(s)
        if not fs.stat("/var/etc") then logic.require(fs.mkdir("/var/etc","755"),"write_failed") end
        configuration.replace(s.journal,"passwd",s.old_passwd,logic.passwd(users))
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
local managed={}
for _,key in ipairs({"tcp-port","udp-port","max-clients","max-same-clients","dpd","ipv4-network","ipv4-netmask","ipv6-network","dns","route","auth","compression","predictable-ips","cisco-client-compat","default-domain"}) do managed[key]=true end
local function apply_settings(s,request)
    local values,dns,routes=logic.validate_settings(request.settings)
    for line in (s.extra.."\n"):gmatch("([^\n]*)\n") do
        logic.require(not managed[line:match("^%s*([%w-]+)%s*=")],"custom_override")
    end
    local previous=logic.settings(s.config,s.dns,s.routes)
    local proposed=logic.settings(values,dns,routes)
    proposed.easy_public_url=previous.easy_public_url
    local firewall_aligned=s.firewall_loaded and s.firewall_raw~=nil and firewall.matches(s.cursor,previous)
    -- The published address is export metadata, independent of the live listener.
    if logic.equal(previous,proposed) and firewall_aligned then
        if values.easy_public_url==previous.easy_public_url then return {effect="unchanged"} end
        return transaction(s,running(),function()
            logic.require(s.cursor:set("ocserv","config","easy_public_url",values.easy_public_url),"write_failed")
            commit(s)
            return {effect="address_saved"}
        end)
    end
    if logic.equal(previous,proposed) then
        logic.require(s.firewall_loaded and s.firewall_raw~=nil,"firewall_unavailable")
        return transaction(s,running(),function(_,mark_firewall)
            if values.easy_public_url~=previous.easy_public_url then
                logic.require(s.cursor:set("ocserv","config","easy_public_url",values.easy_public_url),"write_failed")
                commit(s)
            end
            local ok,code=firewall.apply(s.cursor,previous); logic.require(ok,code)
            commit_firewall(s)
            reload_firewall(mark_firewall)
            return {effect="firewall_repaired"}
        end,true)
    end
    local config={}; for key,value in pairs(s.config) do config[key]=value end
    for key,value in pairs(values) do config[key]=value end
    local domain=s.cursor:get_first("dhcp","dnsmasq","domain","")
    local candidate=logic.render(s.template,s.extra,config,dns,routes,{domain=domain})
    logic.require(logic.standard_auth(candidate),"custom_auth_file")
    local was_running=running()
    if was_running then logic.require(request.allow_restart==true,"restart_confirmation_required") end
    local test_config=work.."/check.conf"; local test_passwd=work.."/check.passwd"
    initcerts()
    atomic(test_passwd,logic.passwd(s.users))
    atomic(test_config,(candidate:gsub("/var/etc/ocpasswd",test_passwd)))
    local code=run({"/usr/sbin/ocserv","--test-config","--config",test_config},8)
    fs.remove(test_config); fs.remove(test_passwd)
    logic.require(code==0,"config_check_failed")
    logic.require(s.firewall_loaded and s.firewall_raw~=nil,"firewall_unavailable")
    return transaction(s,was_running,function(mark,mark_firewall)
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
        local ok,code=firewall.apply(s.cursor,values); logic.require(ok,code)
        commit_firewall(s)
        reload_firewall(mark_firewall)
        if was_running then restart(mark) end
        return {effect=was_running and "restarted" or "saved"}
    end,true)
end
local function connect_proxy(s,name)
    logic.require(not guard.active(),"guard_settings_locked")
    logic.require(running() and (s.config.auth or "plain")=="plain" and s.config.proxy_arp~="1","guard_requires_routed_vpn")
    local changes=s.cursor:changes(name)
    logic.require(not changes or not next(changes[name] or changes),"pending_changes")
    local settings=logic.settings(s.config,s.dns,s.routes)
    local values=logic.validate_settings(settings)
    for line in (s.extra.."\n"):gmatch("([^\n]*)\n") do
        logic.require(not line:match("^%s*device%s*="),"custom_override")
    end
    local path="/etc/"..name.."/ucode/hijack.ut"
    local before=read(path); local after=proxy.ingress(before)
    local pool=values.ipaddr.."/"..logic.prefix(values.netmask)
    local changed=proxy.connect(s.cursor,name,pool,values.ip6addr,true)
    if not changed and before==after then
        -- A startup event can arrive before service_started finishes generating
        -- rules. Wait without another restart when the configuration is current.
        return proxy.ready(name,true) and {effect="unchanged",proxy=name} or false
    end
    s.proxy_raw=read("/etc/config/"..name)
    logic.require(s.proxy_raw,"write_failed")
    stage_cursor(s,false,name)
    proxy.connect(s.cursor,name,pool,values.ip6addr)
    require("luci.model.ocserv_easy.guard_log").add("proxy_connect","start",name)
    return transaction(s,true,function()
        storage.keep("/etc/ocserv/easy-backup/"..name..".uci",s.proxy_raw)
        storage.keep("/etc/ocserv/easy-backup/"..name.."-hijack.ut",before)
        if changed then
            logic.require(s.cursor:commit(name),"write_failed")
            configuration.replace(s.journal,name,s.proxy_raw,read(s.stage.."/"..name))
        end
        configuration.replace(s.journal,name.."_template",before,after)
        configuration.mark_proxy(s.journal,name)
        logic.require(run({"/etc/init.d/"..name,"restart"},90)==0,"guard_proxy_failed")
        wait_proxy(name,true,"guard_proxy_failed")
        return {effect="proxy_connected",proxy=name}
    end,false,name,true)
end
function M.sync_proxy()
    -- Root-only lifecycle hook; do not change another editor's staged settings.
    local lease=require "luci.model.ocserv_easy.lease"
    local lock,code=lease.acquire()
    if not lock and code=="busy" then return false end
    logic.require(lock,code)
    local ok,result=pcall(function()
        if not running() or guard.active() then return {effect="unchanged"} end
        recover_locked(false)
        local s=snapshot()
        if (s.config.auth or "plain")~="plain" or s.config.proxy_arp=="1" then return {effect="unchanged"} end
        local selected=proxy.select(s.cursor)
        if not selected or selected=="openclash" then return {effect="unchanged"} end
        logic.require(proxy.supports(selected),"guard_proxy_unsupported")
        local detected,name=pcall(proxy.detect,s.cursor)
        if not detected then
            -- Enabled but not running yet is transient, not successful sync.
            if type(name)=="table" and name.code=="guard_requires_proxy" then return false end
            error(name,0)
        end
        -- Do not require existing hijack rules before enabling proxy/DNS ingress:
        -- a running core with proxy.enabled=0 has no nft table to wait for.
        logic.require(logic.supported_version(version(true)),"upgrade_required")
        local changes=s.cursor:changes("ocserv")
        logic.require(not changes or not next(changes.ocserv or changes),"pending_changes")
        return connect_proxy(s,name)
    end)
    lease.release(lock)
    if not ok then error(result,0) end
    return result
end
function M.proxy_status()
    local c=uci.cursor()
    local name=proxy.select(c)
    logic.require(name,"guard_requires_proxy")
    local result=proxy.diagnostics(c,name)
    result.proxy=name
    return result
end
function M.action(request,admin)
    logic.require(type(request)=="table" and type(request.action)=="string","bad_request")
    local lease=require "luci.model.ocserv_easy.lease"
    local lock,code=lease.acquire()
    logic.require(lock,code)
    local ok,result=pcall(function()
        recover_locked(false)
        local s=snapshot()
        logic.require(request.revision==s.revision,"stale_revision")
        local current_version=version(true)
        logic.require(current_version~="unknown","version_unavailable")
        logic.require(logic.supported_version(current_version),"upgrade_required")
        -- Pending UCI deltas from another editor must be reviewed there first.
        for _,package in ipairs(request.action=="settings" and {"ocserv","firewall"} or {"ocserv"}) do
            local changes=s.cursor:changes(package)
            logic.require(not changes or not next(changes[package] or changes),"pending_changes")
        end
        if request.action=="save_user" or request.action=="delete_user" then return change_user(s,request) end
        if request.action=="guard" then return guard.begin(request.command,admin,request.token) end
        if request.action=="initcerts" then initcerts(); return {effect="certificates_initialized"} end
        if request.action=="client_profile" then
            local server,cert_mode=logic.validate_client_profile(request.server,request.cert_mode)
            if server==(s.config.easy_public_url or "") and cert_mode==(s.config.easy_cert_mode or "pin") then return {effect="unchanged"} end
            return transaction(s,running(),function()
                logic.require(s.cursor:set("ocserv","config","easy_public_url",server),"write_failed")
                logic.require(s.cursor:set("ocserv","config","easy_cert_mode",cert_mode),"write_failed")
                commit(s)
                return {effect="profile_settings_saved"}
            end)
        end
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
            if command=="start" or command=="restart" then initcerts() end
            logic.require(run({"/etc/init.d/ocserv",command},15)==0,"service_failed")
            if command=="start" or command=="restart" then wait_running(true,"service_failed")
            elseif command=="stop" then wait_running(false,"service_failed") end
            return {effect="service_changed"}
        end
        logic.fail("bad_action")
    end)
    query_cache.invalidate("online")
    lease.release(lock)
    if not ok then error(result,0) end
    return result
end
function M.recover(boot)
    if not configuration.pending() then return true end
    local lease=require "luci.model.ocserv_easy.lease"
    local lock,code=lease.acquire()
    if not lock and code=="busy" then return false end
    logic.require(lock,code)
    local ok,result=pcall(recover_locked,boot)
    lease.release(lock)
    if not ok then error(result,0) end
    return true
end
local function server_pin()
    local s=snapshot()
    local rendered=logic.render(s.template,s.extra,s.config,s.dns,s.routes,
        {domain=s.cursor:get_first("dhcp","dnsmasq","domain","")})
    local certificate
    for line in (rendered.."\n"):gmatch("([^\n]*)\n") do
        -- Multiple keys or virtual hosts need an explicitly selected pin.
        logic.require(not line:match("^%s*%[vhost:"),"pin_unavailable")
        local path=line:match("^%s*server%-cert%s*=%s*(.-)%s*$")
        if path then
            logic.require(not certificate,"pin_unavailable")
            certificate=path:match('^"(.*)"$') or path
        end
    end
    logic.require(certificate and certificate:sub(1,1)=="/","pin_unavailable")
    local code,output=run({"/usr/bin/certtool","--pubkey-info","--load-certificate",certificate},8)
    local pin=code==0 and ("\n"..output.."\n"):match("\n%s*(pin%-sha256:[A-Za-z0-9+/]+=)%s*\n")
    logic.require(pin and #pin==55,"pin_unavailable")
    return pin
end
function M.export(kind,url)
    logic.require(kind=="ca" or kind=="profile" or kind=="address" or kind=="pin","bad_request")
    local normalized
    if kind~="ca" then
        if type(url)=="string" and not url:find("://",1,true) then url="https://"..url end
        normalized=logic.url(url)
        logic.require(normalized,"invalid_url")
        if kind=="address" then return "[VPN]\nServer="..normalized.."\n","Lansyra.vpn" end
        if kind=="pin" then return "[VPN]\nServer="..normalized.."\nServerPin="..server_pin().."\n","Lansyra.vpn" end
    end
    local ca=read("/etc/ocserv/ca.pem",65536)
    logic.require(ca and ca:find("-----BEGIN CERTIFICATE-----",1,true) and ca:find("-----END CERTIFICATE-----",1,true) and not ca:find("PRIVATE KEY",1,true),"ca_unavailable")
    if kind=="ca" then return ca,"ca.pem" end
    return "[VPN]\nServer="..normalized.."\nCABase64="..nixio.bin.b64encode(ca).."\n","Lansyra.vpn"
end
return M
