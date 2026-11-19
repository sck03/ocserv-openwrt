local S,guard
local root="/etc/ocserv/easy-guard/"
local admin="192.168.19.2"
local function clone(v) if type(v)~="table" then return v end; local out={}; for k,x in pairs(v) do out[k]=clone(x) end; return out end
local function equal(a,b)
    if type(a)~=type(b) then return false end
    if type(a)~="table" then return a==b end
    for k,v in pairs(a) do if not equal(v,b[k]) then return false end end
    for k in pairs(b) do if a[k]==nil then return false end end
    return true
end
local function check(v,message) assert(v,message or "check failed") end
local serial,objects=0,{}
local function encode(v) serial=serial+1; local key="JSON-"..serial; objects[key]=clone(v); return key end
local function decode(v) return clone(objects[v]) end
local function row(kind,values) values=values or {}; values[".type"]=kind; return values end
local function reset()
    S={files={},dirs={},commands={},now=1000,auto_confirm=true,nft=false,counter=0,running=true,proxy_running="openclash"}
    S.cfg={
        network={lan=row("interface",{proto="static",device="br-lan",ipaddr="192.168.19.253",netmask="255.255.255.0"})},
        firewall={defaults=row("defaults",{flow_offloading="1",flow_offloading_hw="1"}),lan=row("zone",{name="lan",device={"br-lan","vpns+"},input="ACCEPT",output="ACCEPT",forward="ACCEPT"})},
        dhcp={dns=row("dnsmasq",{localservice="1",nonwildcard="1",domain="lan",interface={"lan"}})},
        openclash={config=row("openclash",{enable="1",lan_ac_mode="0",lan_ac_white_ips={"192.168.19.9"},lan_ac_white_macs={"00:11:22:33:44:55"},enable_redirect_dns="2",subscription="synthetic-do-not-disclose"})},
        ocserv={config=row("ocserv",{port="4443",ipaddr="10.77.0.0",netmask="255.255.255.0",max_clients="16",max_same="1",dpd="60",udp="1",auth="plain",zone="lan"}),
            employee=row("ocservusers",{name="fixture-user",password="$6$fixture$"..string.rep("x",86),group="*"}),
            dns1=row("dns",{ip="1.1.1.1"}),route1=row("routes",{ip="192.168.40.0",netmask="255.255.255.0"})},
        uhttpd={main=row("uhttpd",{listen_http={"0.0.0.0:80"},listen_https={"[::]:8443"}})},dropbear={main=row("dropbear",{Port="2222"})}
    }
    for k,v in pairs(S.cfg) do S.files["/etc/config/"..k]=encode(v) end
    S.files["/etc/ocserv/ocserv.conf.local"]="# preserve me\nroute = 192.168.40.0/255.255.255.0\n"
    S.files["/etc/ocserv/ocserv.conf.template"]=TEST_TEMPLATE
    S.files["/usr/share/ocserv-easy/guard.nft.in"]=TEST_GUARD
    S.files["/var/run/ocserv.pid"]="101\n"
    S.files["/proc/net/tcp"]=" 0: 00000000:041D 00000000:0000 0A\n"
    S.files["/proc/net/udp"]=" 0: 00000000:041D 00000000:0000 07\n"
    for _,path in ipairs({"/sbin/fw4","/usr/sbin/nft","/etc/init.d/openclash"}) do S.files[path]="executable" end
end
local function cursor(directory,savedir)
    directory=directory or "/etc/config"
    local cache={}
    local function db(package)
        if not cache[package] then cache[package]=clone(directory=="/etc/config" and S.cfg[package] or decode(S.files[directory.."/"..package]) or {}) end
        return cache[package]
    end
    local c={}
    function c:get_confdir()return directory end
    function c:get_savedir()return savedir end
    function c:load(package) return S.files[directory.."/"..package]~=nil end
    function c:get(package,id,key)
        local r=db(package)[id];local value=r and r[key or ".type"]
        if value==nil then return false,"Entry not found" end
        return value
    end
    function c:get_all(package,id) local r=clone(db(package)[id]); if r then r[".name"]=id end; return r end
    function c:get_first(package,kind,key,default)
        for id,r in pairs(db(package)) do if r[".type"]==kind then if key then return r[key] or default end; return id end end
        return default
    end
    function c:foreach(package,kind,fn)
        local rows={}; for id,r in pairs(db(package)) do if r[".type"]==kind then local v=clone(r); v[".name"]=id; rows[#rows+1]=v end end
        table.sort(rows,function(a,b)
            local ai,bi=a[".index"] or 1,b[".index"] or 1
            return ai==bi and a[".name"]<b[".name"] or ai<bi
        end)
        for _,r in ipairs(rows) do fn(r) end
    end
    function c:set(package,id,key,value)
        if value==nil then db(package)[id]=row(key)
        else if not db(package)[id] then return false end; db(package)[id][key]=clone(value) end
        return true
    end
    function c:delete(package,id,key)
        if not db(package)[id] or key and db(package)[id][key]==nil then return false,"Entry not found" end
        if key then if db(package)[id] then db(package)[id][key]=nil end else db(package)[id]=nil end
        return true
    end
    function c:changes(package) return S.pending==package and {[package]={"external edit"}} or {} end
    function c:commit(package)
        if S.fail_commit==package then S.fail_commit=nil; return false end
        if directory=="/etc/config" then S.cfg[package]=clone(db(package)) end
        S.files[directory.."/"..package]=encode(db(package)); return true
    end
    function c:reorder(package,id) db(package)[id][".index"]=0; return true end
    function c:unload(package) cache[package]=nil end
    function c:revert(package) S.pending=nil; cache[package]=nil end
    return c
end
local fs={}
local function mode(value)
    assert(type(value)=="string" and value:match("^[0-7][0-7][0-7]$"),"nixio expects octal permission digits")
    return tonumber(value,8)
end
function fs.stat(path) if S.files[path]~=nil then return {} end; return S.dirs[path] end
function fs.access(path) return fs.stat(path)~=nil end
function fs.mkdir(path,value) S.dirs[path]={mode=mode(value)}; return true end
function fs.chmod(path,value) mode(value);return true end
function fs.remove(path) S.files[path]=nil; return true end
function fs.rename(from,to)
    if S.fail_write==to then S.fail_write=nil; return false end
    S.files[to]=S.files[from]; S.files[from]=nil
    if to:sub(1,5)=="/etc/" then S.flash_writes=(S.flash_writes or 0)+1 end
    if to:find("config-stage",1,true) then S.stage_writes=(S.stage_writes or 0)+1 end
    local package=to:match("^/etc/config/(%w+)$")
    if package then S.cfg[package]=decode(S.files[to]) end
    return true
end
function fs.readlink(path) if S.running and path=="/proc/101/exe" then return "/usr/sbin/ocserv" end end
io.open=function(path)
    if S.fail_open==path then return nil,"Permission denied",13 end
    if S.files[path]==nil then return nil,"No such file",2 end
    local position=1
    return {read=function(_,n)
        if S.fail_read==path then return nil,"Input/output error",5 end
        local data=S.files[path]:sub(position,position+n-1);position=position+#data
        return #data>0 and data or nil
    end,close=function()end}
end
os.time=function() return S.now end
local nixio={bin={},open_flags=function()return 1 end}
nixio.bin.hexlify=function(v)return (v:gsub(".",function(ch)return string.format("%02x",ch:byte())end))end
function nixio.open(path,flags,permissions)
    if path=="/dev/urandom" then return {read=function(_,n)S.counter=S.counter+1;return string.rep(string.char(S.counter%255),n)end,close=function()end} end
    if flags=="r" then return {sync=function()return true end,close=function()end} end
    mode(permissions)
    S.files[path]=""
    return {write=function(_,v,offset)local part=v:sub((offset or 0)+1);S.files[path]=S.files[path]..part;return #part end,sync=function()return true end,close=function()end,lock=function()return not S.locked end}
end
function nixio.poll(_,ms)
    S.now=S.now+ms/1000
    if S.ocserv_ready_at and S.now>=S.ocserv_ready_at then
        S.running=true; S.files["/var/run/ocserv.pid"]="101\n"; S.ocserv_ready_at=nil
    end
    local state=decode(S.files[root.."state.json"])
    if S.preview and state and state.phase=="pending" then coroutine.yield(); return end
    if S.auto_confirm and state and state.phase=="pending" then
        guard.begin("confirm",admin,state.token)
    end
end
local function run(args,timeout,consume)
    local command=table.concat(args," "); S.commands[#S.commands+1]=command
    if command==S.fail_command then S.fail_command=nil; return 1,"failure" end
    if command=="/usr/sbin/ocserv --version" then return 0,"OpenConnect VPN Server 1.6.0\n" end
    if command=="/usr/bin/occtl --help" then return 0,"terminate user" end
    if command=="/usr/bin/occtl -j show users" then
        if consume then consume("[]"); return 0,"" end
        return 0,"[]"
    end
    if command=="/etc/init.d/ocserv restart" then
        S.running=false; S.files["/var/run/ocserv.pid"]=nil; S.ocserv_ready_at=nil
        if not S.ocserv_never_ready then
            if S.ocserv_delay then S.ocserv_ready_at=S.now+S.ocserv_delay
            else S.running=true; S.files["/var/run/ocserv.pid"]="101\n" end
        end
    end
    if args[1]=="/bin/ubus" then
        if args[3]=="service" then
            local name=decode(args[5]).name
            return 0,encode({[name]={instances={[name]={running=S.proxy_running==name}}}})
        end
        if args[3]=="network.interface.lan" then return 0,encode({up=true,l3_device="br-lan",["ipv4-address"]={{address="192.168.19.253",mask=24}}}) end
        return 0,encode({up=S.wan or false})
    end
    if command=="/usr/sbin/nft -f "..root.."guard.nft" then S.nft=true end
    if command=="/usr/sbin/nft delete table inet lansyra_guard" then S.nft=false end
    if command=="/usr/sbin/nft list table inet lansyra_guard" then return S.nft and 0 or 1,"" end
    if args[2]=="list" and args[3]=="set" then
        if S.proxy_table_absent then return 1,"No such file or directory" end
        local template=S.files["/etc/"..args[5].."/ucode/hijack.ut"] or ""
        return 0,not S.proxy_rules_missing and template:find('"vpns*"',1,true) and 'elements = { "vpns*" }' or "elements = { }"
    end
    if args[2]=="-j" and args[3]=="list" and args[4]=="chain" then
        return 0,encode({nftables={{rule={expr=S.proxy_dns_missing and {} or {{redirect={port=1053}}}}}}})
    end
    if args[2]=="restart" and (args[1]=="/etc/init.d/nikki" or args[1]=="/etc/init.d/momo") then
        S.proxy_table_absent=false
    end
    return 0,""
end
package.loaded["nixio"]=nixio
package.loaded["nixio.fs"]=fs
package.loaded["luci.model.uci"]={cursor=function()return cursor()end}
package.loaded["uci"]={cursor=cursor}
package.loaded["luci.jsonc"]={parse=decode,stringify=encode}
package.loaded["ocserv.process"]={run=run}
guard=require "luci.model.ocserv_easy.guard"
local function enable() guard.begin("enable",admin); guard.work(false); check(guard.status(admin).phase=="enabled",decode(S.files[root.."result.json"]) and decode(S.files[root.."result.json"]).failure) end
local function disable() guard.begin("disable",admin); guard.work(false); check(guard.status(admin).phase=="disabled") end

local function use_proxy(name)
    S.cfg.openclash.config.enable="0"
    S.cfg[name]={config=row("config",{enabled="1",core_only="0",profile="subscription:fixture"}),
        proxy=row("proxy",{enabled="1",lan_proxy="1",ipv4_proxy="1",ipv4_dns_hijack="0",tcp_mode="redirect",udp_mode="tun",lan_inbound_interface={"lan"}}),
        first=row("lan_access_control",{enabled="1",ip={"10.77.0.0/24"},dns="0",proxy="0"}),
        last=row("lan_access_control",{enabled="1",dns="1",proxy="1"})}
    S.files["/etc/init.d/"..name]="executable"
    S.files["/etc/config/"..name]=encode(S.cfg[name])
    S.files["/etc/"..name.."/ucode/hijack.ut"]="#!/usr/bin/utpl\n{% const lan_inbound_device = []; %}\n"
    S.proxy_running=name
end
return {reset=reset,guard=guard,use_proxy=use_proxy,state=function()return S end,
    root=root,admin=admin,clone=clone,equal=equal,check=check,encode=encode,decode=decode,row=row,
    cursor=cursor,fs=fs,nixio=nixio,run=run,enable=enable,disable=disable}
