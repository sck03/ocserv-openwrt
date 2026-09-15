-- SPDX-License-Identifier: GPL-3.0-or-later
-- Reversible VPN-only access for the N1 single-interface side-router topology.
local nixio=require "nixio"
local fs=require "nixio.fs"
local uci=require "luci.model.uci"
local json=require "luci.jsonc"
local logic=require "luci.model.ocserv_easy.logic"
local run=require("luci.model.ocserv_easy.process").run
local M={}
local root="/etc/ocserv/easy-guard"
local statefile=root.."/state.json"
local rulefile=root.."/guard.nft"
local work="/var/run/ocserv-easy"
local configs={"firewall","dhcp","openclash","ocserv"}
local phases={queued=true,applying=true,pending=true,enabled=true,restoring=true,recovery_failed=true}

local function read(path)
    local f=io.open(path,"rb"); if not f then return nil end
    local value=f:read(2097153); f:close()
    logic.require(not value or #value<=2097152,"guard_state_invalid")
    return value
end
local function directory(path)
    if not fs.stat(path) then logic.require(fs.mkdir(path,448),"write_failed") end
    logic.require(fs.chmod(path,448),"write_failed")
end
local function random()
    local f=nixio.open("/dev/urandom","r"); logic.require(f,"random_failed")
    local value=f:read(16); f:close(); logic.require(value and #value==16,"random_failed")
    return nixio.bin.hexlify(value)
end
local function atomic(path,value)
    local temp=path..".new-"..random()
    local f=nixio.open(temp,nixio.open_flags("wronly","creat","excl"),384)
    logic.require(f,"write_failed")
    local offset,ok=1,true
    while offset<=#value do
        local count=f:write(value:sub(offset))
        if not count or count==0 then ok=false; break end
        offset=offset+count
    end
    if ok then ok=f:sync() end
    f:close()
    if ok then ok=fs.rename(temp,path) end
    if not ok then fs.remove(temp); logic.fail("write_failed") end
end
local function saved()
    local raw=read(statefile); if not raw then return nil end
    local state=json.parse(raw)
    logic.require(type(state)=="table" and state.version==1 and phases[state.phase] and type(state.ops)=="table","guard_state_invalid")
    return state
end
local function save(state) atomic(statefile,json.stringify(state)) end
local function equal(a,b)
    if type(a)~=type(b) then return false end
    if type(a)~="table" then return a==b end
    for k,v in pairs(a) do if not equal(v,b[k]) then return false end end
    for k in pairs(b) do if a[k]==nil then return false end end
    return true
end
local function section(c,package,id)
    local value=c:get_all(package,id); if not value then return false end
    local clean={[".type"]=value[".type"]}
    for key,item in pairs(value) do if key:sub(1,1)~="." then clean[key]=item end end
    return clean
end
local function current(c,op)
    if op.key then local value=c:get(op.package,op.id,op.key); if value==nil then return false end; return value end
    return section(c,op.package,op.id)
end
local function put(c,op,value)
    if op.key then
        if value==false then if c:get(op.package,op.id,op.key)~=nil then logic.require(c:delete(op.package,op.id,op.key),"write_failed") end
        else logic.require(c:set(op.package,op.id,op.key,value),"write_failed") end
    else
        if c:get(op.package,op.id)~=nil then logic.require(c:delete(op.package,op.id),"write_failed") end
        if value~=false then
            logic.require(c:set(op.package,op.id,value[".type"]),"write_failed")
            for key,item in pairs(value) do if key~=".type" then logic.require(c:set(op.package,op.id,key,item),"write_failed") end end
        end
    end
end
local function pending(c)
    for _,package in ipairs(configs) do
        local changes=c:changes(package)
        logic.require(not changes or not next(changes[package] or changes),"pending_changes")
    end
end
local function address(n)
    local parts={}; for i=3,0,-1 do parts[#parts+1]=tostring(math.floor(n/256^i)%256) end
    return table.concat(parts,".")
end
local function list(value) return type(value)=="table" and value or value and {value} or {} end
local function detect(c,admin)
    logic.require(fs.access("/sbin/fw4") and fs.access("/usr/sbin/nft"),"guard_requires_fw4")
    logic.require(c:get("openclash","config","enable")=="1" and fs.access("/etc/init.d/openclash") and run({"/bin/pidof","clash"},3)==0,"guard_requires_openclash")
    logic.require((c:get("ocserv","config","auth") or "plain")=="plain" and c:get("ocserv","config","proxy_arp")~="1","guard_requires_routed_vpn")
    local code,out=run({"/bin/ubus","call","network.interface.lan","status"},4)
    local lan=code==0 and json.parse(out) or nil
    logic.require(type(lan)=="table" and lan.up and type(lan["ipv4-address"])=="table","guard_requires_lan")
    local device=lan.l3_device or lan.device
    logic.require(type(device)=="string" and #device<=15 and device:match("^[A-Za-z0-9_.-]+$"),"guard_requires_lan")
    local value=lan["ipv4-address"][1] or {}
    local ip,prefix=logic.ipv4(value.address),tonumber(value.mask)
    local administrator=logic.ipv4(admin)
    logic.require(ip and prefix and prefix>=8 and prefix<=30,"guard_requires_lan")
    local size=2^(32-prefix); local network=math.floor(ip/size)*size
    logic.require(administrator and administrator~=ip and administrator>network and administrator<network+size-1,"guard_admin_not_lan")
    local rc,wan=run({"/bin/ubus","call","network.interface.wan","status"},3)
    wan=rc==0 and json.parse(wan) or nil
    logic.require(not (type(wan)=="table" and wan.up),"guard_side_router_only")
    for _,name in ipairs({"shortcut_fe","shortcut_fe_ipv6","fast_classifier","sfe"}) do
        logic.require(not fs.stat("/sys/module/"..name),"guard_extra_offload")
    end
    local pool=c:get("ocserv","config","ipaddr") or "10.77.0.0"
    if pool=="" then pool="10.77.0.0" end
    local bits=logic.prefix(c:get("ocserv","config","netmask") or "255.255.255.0")
    local poolnum=logic.ipv4(pool)
    logic.require(poolnum and bits and bits>=8 and bits<=30 and poolnum%2^(32-bits)==0,"invalid_pool")
    logic.require(poolnum+2^(32-bits)-1<network or poolnum>network+size-1,"guard_pool_overlap")
    local port=tonumber(c:get("ocserv","config","port") or "4443")
    local udp=tonumber(c:get("ocserv","config","udp_port")) or port
    logic.require(port and port>=1 and port<=65535 and port%1==0 and udp>=1 and udp<=65535 and udp%1==0,"invalid_port")
    local ports={[22]=true,[80]=true,[443]=true}
    c:foreach("uhttpd","uhttpd",function(s)
        for _,key in ipairs({"listen_http","listen_https"}) do for _,v in ipairs(list(s[key])) do
            local p=tonumber(v:match(":(%d+)$")); if p and p>0 and p<=65535 then ports[p]=true end
        end end
    end)
    c:foreach("dropbear","dropbear",function(s) local p=tonumber(s.Port); if p and p>0 and p<=65535 then ports[p]=true end end)
    local management={}; for p in pairs(ports) do management[#management+1]=p end; table.sort(management)
    logic.require(not ports[port],"guard_port_conflict")
    return {lan=value.address,device=device,admin=admin,lan_network=address(network),lan_mask=logic.mask(prefix),
        pool=pool,prefix=bits,dns=address(poolnum+1),port=port,udp_port=udp,management=management}
end
local function running()
    local pid=(read("/var/run/ocserv.pid") or ""):match("^%s*(%d+)%s*$")
    local executable=pid and fs.readlink("/proc/"..pid.."/exe")
    return executable=="/usr/sbin/ocserv" or executable=="/usr/sbin/ocserv (deleted)"
end
local function plan(c,info)
    local state={version=1,phase="queued",ops={},before={},files={},info=info,token=random(),deadline=os.time()+240,was_running=running()}
    logic.require(state.was_running,"guard_requires_ocserv")
    pending(c)
    for _,package in ipairs(configs) do state.before[package]=read("/etc/config/"..package) or false end
    local function option(package,id,key,value)
        local op={package=package,id=id,key=key,after=value}; op.before=current(c,op)
        if not equal(op.before,op.after) then state.ops[#state.ops+1]=op end
    end
    local function whole(package,id,value)
        local op={package=package,id=id,after=value}; op.before=current(c,op)
        if not equal(op.before,op.after) then state.ops[#state.ops+1]=op end
    end
    local defaults=c:get_first("firewall","defaults")
    logic.require(defaults,"guard_requires_fw4")
    option("firewall",defaults,"flow_offloading","0")
    option("firewall",defaults,"flow_offloading_hw","0")
    local zone,has_lan
    local vpn_networks={}
    c:foreach("network","interface",function(s)
        if s.device=="vpns+" or s.ifname=="vpns+" then vpn_networks[s[".name"]]=true end
    end)
    c:foreach("firewall","zone",function(s)
        if s.name=="lan" then has_lan=true end
        if s.name=="ocvpn" then logic.require(not zone,"guard_section_conflict"); zone=s[".name"] end
        for _,key in ipairs({"device","network"}) do
            local kept={}; local changed=false
            for _,v in ipairs(list(s[key])) do
                if key=="device" and (v=="vpns+" or v=="vpns*") or key=="network" and vpn_networks[v] then changed=true
                else kept[#kept+1]=v end
            end
            if changed and s.name~="ocvpn" then option("firewall",s[".name"],key,#kept>0 and kept or false) end
        end
    end)
    logic.require(has_lan,"guard_requires_lan")
    -- An existing ocvpn zone is updated option-by-option and restored on disable.
    if not zone then
        zone="bulijie_vpn"
        logic.require(not c:get("firewall",zone),"guard_section_conflict")
        whole("firewall",zone,{[".type"]="zone",name="ocvpn",device={"vpns+"},input="ACCEPT",output="ACCEPT",forward="REJECT",mtu_fix="1"})
    else
        for key,value in pairs({device={"vpns+"},input="ACCEPT",output="ACCEPT",forward="REJECT",mtu_fix="1"}) do option("firewall",zone,key,value) end
    end
    for name,values in pairs({
        bulijie_vpn_to_lan={[".type"]="forwarding",src="ocvpn",dest="lan"},
        bulijie_ocserv_entry={[".type"]="rule",name="Allow-ocserv-from-LAN",src="lan",dest_ip=info.lan,proto="tcp udp",dest_port=tostring(info.port).." "..tostring(info.udp_port),family="ipv4",target="ACCEPT"},
        bulijie_guard={[".type"]="include",type="nftables",path=rulefile,position="ruleset-prepend",enabled="1"}
    }) do
        logic.require(not c:get("firewall",name),"guard_section_conflict")
        whole("firewall",name,values)
    end
    option("ocserv","config","zone","ocvpn")
    option("ocserv","config","ipaddr",info.pool)
    option("ocserv","config","netmask",logic.mask(info.prefix))
    option("ocserv","config","ip6addr",false)
    option("ocserv","config","split_dns","0")
    c:foreach("ocserv","dns",function(s) whole("ocserv",s[".name"],false) end)
    c:foreach("ocserv","routes",function(s) whole("ocserv",s[".name"],false) end)
    logic.require(not c:get("ocserv","bulijie_guard_dns"),"guard_section_conflict")
    whole("ocserv","bulijie_guard_dns",{[".type"]="dns",ip=info.dns})
    local dns=c:get_first("dhcp","dnsmasq"); logic.require(dns,"guard_requires_dnsmasq")
    option("dhcp",dns,"localservice","0")
    option("dhcp",dns,"nonwildcard","0")
    option("dhcp",dns,"interface",false)
    option("dhcp",dns,"notinterface",false)
    option("openclash","config","lan_ac_mode","1")
    option("openclash","config","lan_ac_white_ips",{info.pool.."/"..info.prefix})
    option("openclash","config","lan_ac_white_macs",false)
    option("openclash","config","intranet_allowed","0")
    option("openclash","config","enable_redirect_dns","1")
    local extra=read("/etc/ocserv/ocserv.conf.local")
    local lines={}
    local managed={["listen-host"]=true,dns=true,route=true,["tunnel-all-dns"]=true,device=true,["ipv4-network"]=true,["ipv4-netmask"]=true,["ipv6-network"]=true}
    for line in ((extra or "").."\n"):gmatch("([^\n]*)\n") do
        local key=line:match("^%s*([a-z0-9-]+)%s*=")
        if not managed[key] then lines[#lines+1]=line end
    end
    lines[#lines+1]="# Managed by the VPN-only switch; restored when disabled."
    lines[#lines+1]="listen-host = "..info.lan
    lines[#lines+1]="no-route = "..info.lan_network.."/"..info.lan_mask
    lines[#lines+1]="tunnel-all-dns = true"
    state.files[1]={path="/etc/ocserv/ocserv.conf.local",before=extra or false,after=table.concat(lines,"\n").."\n"}
    local template=read("/usr/share/ocserv-easy/guard.nft.in")
    logic.require(template and not fs.stat(rulefile),"guard_section_conflict")
    local substitutions={LAN_DEVICE=info.device,LAN_ADDRESS=info.lan,ADMIN_ADDRESS=info.admin,VPN_POOL=info.pool.."/"..info.prefix,VPN_DNS=info.dns,
        VPN_TCP_PORT=tostring(info.port),VPN_UDP_PORT=tostring(info.udp_port),ADMIN_PORTS=table.concat(info.management,", ")}
    local rules=template:gsub("@([A-Z_]+)@",function(key) return logic.require(substitutions[key],"guard_state_invalid") end)
    state.files[2]={path=rulefile,before=false,after=rules}
    return state
end
function M.status(admin)
    local ok,state=pcall(saved)
    if not ok then return {phase="recovery_failed",enabled=true,reason="guard_state_invalid"} end
    if state then
        return {phase=state.phase,enabled=true,lan=state.info.lan,admin=state.info.admin,pool=state.info.pool.."/"..state.info.prefix,
            deadline=state.deadline,token=state.phase=="pending" and state.info.admin==admin and state.token or nil,reason=state.failure}
    end
    local success,info=pcall(function() return detect(uci.cursor(),admin) end)
    local result={phase="disabled",enabled=false,available=success and running()}
    if success then result.lan=info.lan; result.admin=info.admin; result.pool=info.pool.."/"..info.prefix; if not result.available then result.reason="guard_requires_ocserv" end
    else result.reason=type(info)=="table" and info.code or "guard_unavailable" end
    local last=read(root.."/result.json"); last=last and json.parse(last)
    if type(last)=="table" then result.last=last end
    return result
end
function M.active() return saved()~=nil end
function M.begin(command,admin,token)
    local state=saved()
    if command=="confirm" then
        logic.require(state and state.phase=="pending" and state.token==token and state.info.admin==admin and os.time()<state.deadline,"guard_confirmation_expired")
        state.phase="enabled"; state.token=nil; save(state)
        return {effect="guard_enabled"}
    end
    logic.require(command=="enable" or command=="disable","bad_action")
    directory(root)
    if command=="enable" then
        logic.require(not state,"busy")
        local c=uci.cursor(); state=plan(c,detect(c,admin))
        save(state)
        logic.require(run({"/etc/init.d/ocserv-easy-guard","enable"},3)==0,"guard_worker_failed")
    else
        if not state then return {effect="guard_disabled"} end
        logic.require(state.phase=="enabled" or state.phase=="pending" or state.phase=="queued" or state.phase=="recovery_failed","busy")
        state.phase="restoring"; state.deadline=os.time()+240; save(state)
    end
    if run({"/etc/init.d/ocserv-easy-guard","restart"},5)~=0 then
        state.failure="guard_worker_failed"; save(state); logic.fail("guard_worker_failed")
    end
    return {effect=command=="enable" and "guard_queued" or "guard_restoring"}
end
local function commit(c)
    for _,package in ipairs(configs) do logic.require(c:commit(package),"write_failed") end
end
local function reload(state,boot)
    if boot then return end
    logic.require(run({"/etc/init.d/firewall","reload"},20)==0,"guard_firewall_failed")
    logic.require(run({"/etc/init.d/dnsmasq","restart"},20)==0,"guard_dns_failed")
    logic.require(run({"/etc/init.d/openclash","restart"},90)==0,"guard_openclash_failed")
    local ready=false
    for _=1,30 do
        if run({"/bin/pidof","clash"},3)==0 then ready=true; break end
        nixio.poll({},1000)
    end
    logic.require(ready,"guard_openclash_failed")
    if state.was_running then logic.require(run({"/etc/init.d/ocserv","restart"},20)==0 and running(),"restart_failed") end
end
local function restore(state,boot)
    if not state.started then
        atomic(root.."/result.json",json.stringify({restored=true,preserved=0,failure=state.failure,time=os.time()}))
        logic.require(fs.remove(statefile),"write_failed"); return
    end
    local c=uci.cursor(); local preserved=0
    pending(c)
    for i=#state.ops,1,-1 do
        local op=state.ops[i]; local value=current(c,op)
        if op.id=="bulijie_guard" and not op.key and op.before==false and c:get(op.package,op.id,"path")==rulefile then put(c,op,false)
        elseif equal(value,op.after) then put(c,op,op.before)
        elseif not equal(value,op.before) then preserved=preserved+1 end
    end
    for _,file in ipairs(state.files) do
        local value=read(file.path) or false
        if value==file.after then if file.before==false then fs.remove(file.path) else atomic(file.path,file.before) end
        elseif value~=file.before then preserved=preserved+1 end
    end
    commit(c)
    run({"/usr/sbin/nft","delete","table","inet","bulijie_guard"},5)
    if not boot then logic.require(run({"/sbin/fw4","check"},10)==0,"guard_firewall_failed") end
    reload(state,boot)
    atomic(root.."/last-backup.json",json.stringify(state))
    atomic(root.."/result.json",json.stringify({restored=true,preserved=preserved,failure=state.failure,time=os.time()}))
    logic.require(fs.remove(statefile),"write_failed")
end
local function apply(state)
    local c=uci.cursor(); pending(c)
    for _,package in ipairs(configs) do logic.require((read("/etc/config/"..package) or false)==state.before[package],"stale_revision") end
    state.phase="applying"; state.started=true; save(state)
    for _,op in ipairs(state.ops) do put(c,op,op.after) end
    for _,file in ipairs(state.files) do atomic(file.path,file.after) end
    local users,dns,routes={},{},{}
    c:foreach("ocserv","ocservusers",function(s) users[#users+1]={name=s.name,password=s.password,group=s.group} end)
    c:foreach("ocserv","dns",function(s) dns[#dns+1]={ip=s.ip} end)
    c:foreach("ocserv","routes",function(s) routes[#routes+1]={ip=s.ip,netmask=s.netmask} end)
    local passwd,config=work.."/guard-check.passwd",work.."/guard-check.conf"
    atomic(passwd,logic.passwd(users))
    local rendered=logic.render(read("/etc/ocserv/ocserv.conf.template") or "",read("/etc/ocserv/ocserv.conf.local") or "",c:get_all("ocserv","config"),dns,routes,{domain=c:get_first("dhcp","dnsmasq","domain","")})
    atomic(config,(rendered:gsub("/var/etc/ocpasswd",passwd)))
    local valid=run({"/usr/sbin/ocserv","--test-config","--config",config},10)==0
    fs.remove(passwd); fs.remove(config)
    logic.require(valid,"config_check_failed")
    logic.require(run({"/usr/sbin/nft","-c","-f",rulefile},10)==0,"guard_firewall_failed")
    commit(c)
    logic.require(run({"/sbin/fw4","check"},10)==0,"guard_firewall_failed")
    logic.require(run({"/usr/sbin/nft","-f",rulefile},10)==0,"guard_firewall_failed")
    reload(state,false)
    logic.require(run({"/usr/sbin/nft","list","table","inet","bulijie_guard"},5)==0,"guard_firewall_failed")
    state.phase="pending"; state.deadline=os.time()+120; save(state)
end
local function locked(operation)
    directory(work)
    local lock=nixio.open(work.."/lock","w",384); logic.require(lock,"write_failed")
    local acquired=false
    for _=1,100 do if lock:lock("tlock") then acquired=true; break end; nixio.poll({},100) end
    if not acquired then lock:close(); logic.fail("busy") end
    local ok,result=pcall(operation)
    lock:lock("ulock"); lock:close()
    if not ok then error(result,0) end
    return result
end
function M.work(boot)
    locked(function()
        local state=saved(); if not state or state.phase=="enabled" then return end
        local ok,err=pcall(function()
            if not boot and state.phase=="queued" then apply(state)
            else state.phase="restoring"; save(state); restore(state,boot) end
        end)
        if not ok then
            state.phase="restoring"; state.failure=type(err)=="table" and err.code or "internal_error"; save(state)
            local restored=pcall(restore,state,boot)
            if not restored then state.phase="recovery_failed"; save(state) end
        end
    end)
    if boot then return end
    while true do
        local state=saved()
        if not state or state.phase~="pending" then return end
        if os.time()>=state.deadline then
            locked(function()
                state=saved()
                if state and state.phase=="pending" and os.time()>=state.deadline then
                    state.phase="restoring"; state.failure="guard_confirmation_expired"; save(state)
                    local ok=pcall(restore,state,false)
                    if not ok then state.phase="recovery_failed"; save(state) end
                end
            end)
            return
        end
        nixio.poll({},1000)
    end
end
return M
