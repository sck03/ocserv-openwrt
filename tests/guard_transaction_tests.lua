local results,S,guard={}
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
local function test(name,fn)
    local ok,err=pcall(fn)
    assert(ok,name..": "..(type(err)=="table" and tostring(err.code) or tostring(err)))
    results[#results+1]=name
end
local function expect(code,fn) local ok,err=pcall(fn); check(not ok and type(err)=="table" and err.code==code,"expected "..code) end
local serial,objects=0,{}
local function encode(v) serial=serial+1; local key="JSON-"..serial; objects[key]=clone(v); return key end
local function decode(v) return clone(objects[v]) end
local function row(kind,values) values=values or {}; values[".type"]=kind; return values end
local function reset()
    S={files={},dirs={},commands={},now=1000,auto_confirm=true,nft=false,counter=0,running=true,openclash_process="clash"}
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
    local package=to:match("^/etc/config/(%w+)$")
    if package then S.cfg[package]=decode(S.files[to]) end
    return true
end
function fs.readlink(path) if S.running and path=="/proc/101/exe" then return "/usr/sbin/ocserv" end end
io.open=function(path)
    if S.files[path]==nil then return nil end
    return {read=function(_,n)return S.files[path]:sub(1,n)end,close=function()end}
end
os.time=function() return S.now end
local nixio={bin={},open_flags=function()return 1 end}
nixio.bin.hexlify=function(v)return (v:gsub(".",function(ch)return string.format("%02x",ch:byte())end))end
function nixio.open(path,flags,permissions)
    if path=="/dev/urandom" then return {read=function(_,n)S.counter=S.counter+1;return string.rep(string.char(S.counter%255),n)end,close=function()end} end
    if flags=="r" then return {sync=function()return true end,close=function()end} end
    mode(permissions)
    S.files[path]=""
    return {write=function(_,v)S.files[path]=S.files[path]..v;return #v end,sync=function()return true end,close=function()end,lock=function()return not S.locked end}
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
local function run(args)
    local command=table.concat(args," "); S.commands[#S.commands+1]=command
    if command==S.fail_command then S.fail_command=nil; return 1,"failure" end
    if command=="/usr/sbin/ocserv --version" then return 0,"OpenConnect VPN Server 1.6.0\n" end
    if command=="/usr/bin/occtl --help" then return 0,"terminate user" end
    if command=="/usr/bin/occtl -j show users" then return 0,encode({}) end
    if command=="/etc/init.d/ocserv restart" then
        S.running=false; S.files["/var/run/ocserv.pid"]=nil; S.ocserv_ready_at=nil
        if not S.ocserv_never_ready then
            if S.ocserv_delay then S.ocserv_ready_at=S.now+S.ocserv_delay
            else S.running=true; S.files["/var/run/ocserv.pid"]="101\n" end
        end
    end
    if args[1]=="/bin/pidof" then
        return S.openclash_process==args[2] and 0 or 1,""
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
        local template=S.files["/etc/"..args[5].."/ucode/hijack.ut"] or ""
        return 0,not S.proxy_rules_missing and template:find('"vpns*"',1,true) and 'elements = { "vpns*" }' or "elements = { }"
    end
    return 0,""
end
package.loaded["nixio"]=nixio
package.loaded["nixio.fs"]=fs
package.loaded["luci.model.uci"]={cursor=function()return cursor()end}
package.loaded["uci"]={cursor=cursor}
package.loaded["luci.jsonc"]={parse=decode,stringify=encode}
package.loaded["luci.model.ocserv_easy.process"]={run=run}
guard=require "luci.model.ocserv_easy.guard"
local function enable() guard.begin("enable",admin); guard.work(false); check(guard.status(admin).phase=="enabled",decode(S.files[root.."result.json"]) and decode(S.files[root.."result.json"]).failure) end
local function disable() guard.begin("disable",admin); guard.work(false); check(guard.status(admin).phase=="disabled") end

test("frequent disabled status spawns no detection commands",function()
    reset()
    local count=#S.commands
    for _=1,10000 do
        local status=guard.status(admin,false)
        check(status.phase=="disabled" and not status.enabled and status.available==nil)
    end
    check(#S.commands==count)
    check(guard.status(admin).available)
    check(#S.commands>count)
end)
test("default is off and detection writes no router configuration",function()
    reset(); local before=clone(S.cfg); check(guard.status(admin).phase=="disabled" and guard.status(admin).available); check(equal(before,S.cfg) and not S.nft)
end)
test("Meta/Mihomo OpenClash core is accepted",function()
    reset(); S.openclash_process="mihomo"; local before=clone(S.cfg)
    check(guard.status(admin).phase=="disabled" and guard.status(admin).available)
    check(equal(before,S.cfg) and not S.nft); enable(); check(S.nft); disable()
end)
test("a host address cannot be used as the VPN pool network",function()
    reset(); S.cfg.ocserv.config.ipaddr="192.168.100.1"; local before=clone(S.cfg)
    expect("pool_not_network",function()guard.begin("enable",admin)end)
    check(equal(before,S.cfg) and not guard.active())
end)
test("enable queues work without blocking the LuCI request",function()
    reset(); local before=clone(S.cfg); guard.begin("enable",admin); check(guard.status(admin).phase=="queued" and equal(before,S.cfg) and not S.nft)
end)
test("the guard worker honors the common maintenance lease",function()
    reset();local before=clone(S.cfg);guard.begin("enable",admin);S.locked=true
    expect("busy",function()guard.work(false)end)
    check(equal(before,S.cfg) and guard.status(admin).phase=="queued")
    S.locked=false;guard.work(false);check(guard.status(admin).phase=="enabled")
end)
test("enabling configures real interface guard and VPN-only proxy whitelist",function()
    reset(); S.cfg.openclash.config.enable_redirect_dns="1"; local before=clone(S.cfg)
    enable(); check(S.nft and S.cfg.openclash.config.lan_ac_mode=="1")
    check(equal(S.cfg.dhcp,before.dhcp))
    check(equal(S.cfg.openclash.config.lan_ac_white_ips,{"10.77.0.0/24"}) and not S.cfg.openclash.config.lan_ac_white_macs)
    check(S.cfg.openclash.config.enable_redirect_dns=="2")
    check(S.cfg.firewall.defaults.flow_offloading=="0" and S.cfg.firewall.defaults.flow_offloading_hw=="0")
    check(equal(S.cfg.firewall.lan.device,{"br-lan"}) and S.cfg.ocserv.lansyra_guard_dns.ip=="10.77.0.1" and not S.cfg.ocserv.route1)
    check(S.files[root.."guard.nft"]:find("192.168.19.253",1,true) and S.files[root.."guard.nft"]:find("2222, 8443",1,true))
    disable(); check(equal(before,S.cfg))
end)
test("disabled OpenClash DNS hijack is restored after VPN-only use",function()
    reset(); S.cfg.openclash.config.enable_redirect_dns="0"; local before=clone(S.cfg)
    enable(); check(S.cfg.openclash.config.enable_redirect_dns=="2")
    disable(); check(equal(before,S.cfg))
end)
test("disable restores original settings, lists and local file",function()
    reset(); local before=clone(S.cfg); local extra=S.files["/etc/ocserv/ocserv.conf.local"]
    enable(); disable(); check(equal(before,S.cfg)); check(extra==S.files["/etc/ocserv/ocserv.conf.local"] and not S.nft)
    check(S.files[root.."last-backup.json"]~=nil and S.files[root.."guard.nft"]==nil)
end)
test("IPv6 VPN entry retains isolation and restores a prior IPv4 listener",function()
    reset()
    local extra="listen-host = 192.168.19.253\nno-route = 0.0.0.0/0\n# administrator setting\n"
    S.files["/etc/ocserv/ocserv.conf.local"]=extra
    S.cfg.ocserv.config.ip6addr="fd77::/64"
    local before=clone(S.cfg)
    enable()
    check(not S.cfg.ocserv.config.ip6addr)
    local entry=S.cfg.firewall.lansyra_ocserv_entry6
    check(entry.src=="lan" and entry.family=="ipv6" and not entry.dest and entry.target=="ACCEPT")
    check(entry.dest_port=="4443 4443")
    check(not S.files["/etc/ocserv/ocserv.conf.local"]:find("listen-host",1,true))
    check(not S.files["/etc/ocserv/ocserv.conf.local"]:find("0.0.0.0/0",1,true))
    check(S.files[root.."guard.nft"]:find("VPN IPv6 disabled",1,true))
    disable()
    check(equal(before,S.cfg) and S.files["/etc/ocserv/ocserv.conf.local"]==extra)
end)
test("repeat toggles back up the latest normal settings",function()
    reset(); enable(); disable(); S.cfg.dhcp.dns.domain="new-normal"; local before=clone(S.cfg)
    enable(); disable(); check(equal(before,S.cfg))
end)
test("proxy subscriptions and accounts are never rewritten by the switch",function()
    reset(); local user=clone(S.cfg.ocserv.employee); local subscription=S.cfg.openclash.config.subscription
    enable(); check(equal(user,S.cfg.ocserv.employee) and subscription==S.cfg.openclash.config.subscription)
end)
test("loss of browser confirmation automatically restores",function()
    reset(); local before=clone(S.cfg); S.auto_confirm=false; guard.begin("enable",admin); guard.work(false)
    check(guard.status(admin).phase=="disabled" and not S.nft and equal(before,S.cfg))
    check(guard.status(admin).last.failure=="guard_confirmation_expired")
end)
test("OpenClash restart failure rolls back configuration and guard",function()
    reset(); local before=clone(S.cfg); S.fail_command="/etc/init.d/openclash restart"
    guard.begin("enable",admin); guard.work(false); check(equal(before,S.cfg) and not S.nft and guard.status(admin).last.failure=="guard_proxy_failed")
end)

test("asynchronous ocserv startup succeeds on enable and restore",function()
    reset(); local before=clone(S.cfg); S.ocserv_delay=3
    enable(); check(S.running and S.nft)
    disable(); check(S.running and equal(before,S.cfg) and not guard.active())
end)

test("ocserv restart command failure still rolls back",function()
    reset(); local before=clone(S.cfg); S.fail_command="/etc/init.d/ocserv restart"; S.ocserv_delay=1
    guard.begin("enable",admin); guard.work(false)
    check(equal(before,S.cfg) and not S.nft and not guard.active())
    check(guard.status(admin).last.failure=="restart_failed")
end)

test("ocserv never starting times out and retains recovery for retry",function()
    reset(); local before=clone(S.cfg); S.ocserv_never_ready=true; local started=S.now
    guard.begin("enable",admin); guard.work(false)
    check(S.now-started>=20 and S.now-started<30)
    check(equal(before,S.cfg) and not S.nft and guard.status(admin).phase=="recovery_failed")
    check(guard.status(admin).reason=="restart_failed")
    S.ocserv_never_ready=false; S.ocserv_delay=1; disable(); check(S.running and not guard.active())
end)
test("fw4 validation failure restores before activation",function()
    reset(); local before=clone(S.cfg); S.fail_command="/sbin/fw4 check"
    guard.begin("enable",admin); guard.work(false); check(equal(before,S.cfg) and not S.nft)
end)
test("partial UCI commit failure restores committed packages",function()
    reset(); local before=clone(S.cfg); S.fail_commit="firewall"
    guard.begin("enable",admin); guard.work(false); check(equal(before,S.cfg) and not S.nft)
end)
test("power loss with unconfirmed settings restores before firewall boot",function()
    reset(); local before=clone(S.cfg); enable()
    local state=decode(S.files[root.."state.json"]); state.phase="pending"; S.files[root.."state.json"]=encode(state)
    guard.work(true); check(equal(before,S.cfg) and not S.nft and not guard.active())
end)
test("queued request made stale never overwrites the other editor",function()
    reset(); guard.begin("enable",admin); S.cfg.firewall.defaults.flow_offloading="0"; S.files["/etc/config/firewall"]=encode(S.cfg.firewall)
    local before=clone(S.cfg); guard.work(false); check(equal(before,S.cfg) and not S.nft)
end)
test("queued request preserves an edited local configuration file",function()
    reset(); local before=clone(S.cfg); guard.begin("enable",admin)
    S.files["/etc/ocserv/ocserv.conf.local"]="# saved by administrator after queueing\n"
    guard.work(false)
    check(equal(before,S.cfg) and not S.nft and not guard.active())
    check(S.files["/etc/ocserv/ocserv.conf.local"]=="# saved by administrator after queueing\n")
    check(guard.status(admin).last.failure=="stale_revision")
end)
test("queued request cannot overwrite a newly created guard file",function()
    reset(); local before=clone(S.cfg); guard.begin("enable",admin)
    S.files[root.."guard.nft"]="# another administrator's rules\n"
    guard.work(false)
    check(equal(before,S.cfg) and not guard.active() and S.files[root.."guard.nft"]=="# another administrator's rules\n")
end)
test("boot recovery does not require network or fw4 to be running",function()
    reset(); local before=clone(S.cfg); enable()
    local state=decode(S.files[root.."state.json"]); state.phase="applying"; S.files[root.."state.json"]=encode(state)
    S.fail_command="/sbin/fw4 check"; guard.work(true)
    check(equal(before,S.cfg) and not guard.active() and S.fail_command=="/sbin/fw4 check")
end)
test("disable preserves later changes and newly added accounts",function()
    reset(); enable(); S.cfg.dhcp.dns.domain="later-edit"; S.cfg.openclash.config.lan_ac_mode="2"
    S.cfg.ocserv.newuser=row("ocservusers",{name="new-user",password="synthetic-hash",group="*"})
    disable(); check(S.cfg.dhcp.dns.domain=="later-edit" and S.cfg.openclash.config.lan_ac_mode=="2" and S.cfg.ocserv.newuser.name=="new-user")
    check(guard.status(admin).last.preserved==1)
end)
test("an edited owned include is still removed on disable",function()
    reset(); enable(); S.cfg.firewall.lansyra_guard.enabled="0"; disable(); check(not S.cfg.firewall.lansyra_guard and not S.nft)
end)
test("uncommitted edits in another page are not discarded during recovery",function()
    reset(); enable(); S.pending="firewall"; guard.begin("disable",admin); guard.work(false)
    check(S.pending=="firewall" and guard.status(admin).phase=="recovery_failed")
    S.pending=nil; disable(); check(not guard.active())
end)
test("existing VPN zone stays bound to VPN interfaces",function()
    reset(); S.cfg.firewall.vpn=row("zone",{name="ocvpn",device={"vpns+"},input="REJECT"}); local before=clone(S.cfg)
    enable(); check(equal(S.cfg.firewall.vpn.device,{"vpns+"}) and S.cfg.firewall.vpn.input=="ACCEPT")
    disable(); check(equal(before,S.cfg))
end)
test("a primary router with active WAN cannot use the side-router guard",function()
    reset(); S.wan=true; expect("guard_side_router_only",function()guard.begin("enable",admin)end); check(not S.files[root.."state.json"])
end)
test("non-LAN administration and overlapping address pools are rejected",function()
    reset(); expect("guard_admin_not_lan",function()guard.begin("enable","10.77.0.2")end)
    S.cfg.ocserv.config.ipaddr="192.168.19.0"; expect("guard_pool_overlap",function()guard.begin("enable",admin)end)
end)
test("extra shortcut acceleration is detected before configuration",function()
    reset(); S.files["/sys/module/shortcut_fe"]=""; expect("guard_extra_offload",function()guard.begin("enable",admin)end)
end)
test("unknown manual guard sections are not overwritten",function()
    reset(); S.cfg.firewall.lansyra_guard=row("include",{path="/etc/custom-guard.nft"}); local before=clone(S.cfg)
    expect("guard_section_conflict",function()guard.begin("enable",admin)end); check(equal(before,S.cfg))
end)
test("confirmation tokens are bound to the management address",function()
    reset(); enable(); local state=decode(S.files[root.."state.json"]); state.phase="pending"; state.token="fixture-token"; state.deadline=S.now+120
    S.files[root.."state.json"]=encode(state)
    expect("guard_confirmation_expired",function()guard.begin("confirm","192.168.19.3","fixture-token")end)
    expect("guard_confirmation_expired",function()guard.begin("confirm",admin,"wrong-token")end)
    guard.begin("confirm",admin,"fixture-token"); check(guard.status(admin).phase=="enabled")
end)
test("corrupt recovery records are reported instead of treated as disabled",function()
    reset(); S.files[root.."state.json"]="invalid"; check(guard.status(admin).phase=="recovery_failed" and guard.status(admin).reason=="guard_state_invalid")
end)
local log=require "luci.model.ocserv_easy.guard_log"
test("failed restart and restoration remain visible in structured logs",function()
    reset(); S.ocserv_never_ready=true; guard.begin("enable",admin); guard.work(false)
    local failed,timeout,restored=false,false,false
    for _,entry in ipairs(log.read().entries) do
        if entry.step=="operation_failed" and entry.code=="restart_failed" then failed=true end
        if entry.step=="ocserv_ready" and entry.code=="timeout" then timeout=true end
        if entry.step=="restore_failed" and entry.code=="restart_failed" then restored=true end
    end
    check(failed and timeout and restored)
end)
test("logs expire individually at 24 hours and the empty file is deleted",function()
    reset(); log.add("apply","start"); S.now=S.now+10; log.add("restore","completed")
    S.now=1000+86400; check(#log.read().entries==1)
    S.now=S.now+10; check(#log.read().entries==0 and not S.files["/var/run/ocserv-easy/guard-log.json"])
end)
test("background cleanup deletes expired logs without a browser request",function()
    reset(); log.add("apply","start"); S.now=1000+86399
    local original=nixio.poll; local polls=0
    nixio.poll=function(_,ms)
        polls=polls+1
        if polls==1 then check(ms==1000); S.now=S.now+1
        else error("test cleaner stop") end
    end
    local ok=pcall(log.clean_loop); nixio.poll=original
    check(not ok and polls==2 and not S.files["/var/run/ocserv-easy/guard-log.json"])
end)
test("logs are capped and raw command secrets are never retained",function()
    reset(); for i=1,300 do log.add("apply","start") end
    check(#log.read().entries==256)
    check(log.diagnostic("secret-password subscription=https://secret.invalid Address already in use")=="address already in use")
    check(log.diagnostic("password=do-not-store")==nil)
end)
test("oversized logs retain the newest valid entries in chronological order",function()
    reset(); local rows={}
    for i=1,1024 do rows[#rows+1]={time=S.now,step="apply",code=tostring(i)} end
    rows[#rows+1]={time=S.now+1,code="future"}
    rows[#rows+1]={time=S.now-86400,code="expired"}
    rows[#rows+1]={code="invalid"}
    S.files["/var/run/ocserv-easy/guard-log.json"]=encode(rows)
    local entries=log.read().entries
    check(#entries==256 and entries[1].code=="769" and entries[256].code=="1024")
    log.add("restore","latest"); entries=log.read().entries
    check(#entries==256 and entries[1].code=="770" and entries[256].code=="latest")
end)
test("log write failure cannot prevent configuration recovery",function()
    reset(); enable(); S.fail_write="/var/run/ocserv-easy/guard-log.json"; disable(); check(not guard.active())
end)
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
for _,name in ipairs({"nikki","momo"}) do
    test(name.." uses the VPN wildcard and restores ACLs, template and subscription",function()
        reset(); use_proxy(name); local before=clone(S.cfg)
        local path="/etc/"..name.."/ucode/hijack.ut"; local template=S.files[path]
        check(guard.status(admin).proxy==name and guard.status(admin).available)
        enable(); check(guard.status(admin).proxy==name)
        check(not S.cfg[name].proxy.lan_inbound_interface and S.cfg[name].proxy.ipv4_dns_hijack=="1")
        check(S.cfg[name].first.enabled=="0" and S.cfg[name].last.enabled=="0")
        check(equal(S.cfg[name].lansyra_guard.ip,{"10.77.0.0/24"}) and S.cfg[name].lansyra_guard.dns=="1")
        check(S.files[path]:find('["vpns*"]',1,true))
        check(S.cfg[name].config.profile==before[name].config.profile and equal(S.cfg.openclash,before.openclash))
        for _,command in ipairs(S.commands) do check(command~="/etc/init.d/openclash restart") end
        disable(); check(equal(before,S.cfg) and S.files[path]==template)
    end)
    test(name.." readiness cannot be satisfied by another core process",function()
        reset(); use_proxy(name); S.proxy_running=nil; S.openclash_process="mihomo"
        expect("guard_requires_proxy",function()guard.begin("enable",admin)end)
        check(not guard.active())
    end)
    test(name.." restart failure restores all transaction files",function()
        reset(); use_proxy(name); local before=clone(S.cfg); local path="/etc/"..name.."/ucode/hijack.ut"; local template=S.files[path]
        S.fail_command="/etc/init.d/"..name.." restart"
        guard.begin("enable",admin); guard.work(false)
        check(equal(before,S.cfg) and S.files[path]==template and not S.nft and not guard.active())
        check(guard.status(admin).last.failure=="guard_proxy_failed")
    end)
    test(name.." missing ingress rules roll back even when procd is running",function()
        reset(); use_proxy(name); local before=clone(S.cfg); S.proxy_rules_missing=true
        guard.begin("enable",admin); guard.work(false)
        check(equal(before,S.cfg) and not guard.active() and not S.nft)
        check(guard.status(admin).last.failure=="guard_proxy_failed")
    end)
    test(name.." unknown or duplicate template anchors fail before changes",function()
        for _,template in ipairs({"new incompatible version","const lan_inbound_device = [];\nconst lan_inbound_device = [];"}) do
            reset(); use_proxy(name); local before=clone(S.cfg)
            S.files["/etc/"..name.."/ucode/hijack.ut"]=template
            expect("guard_proxy_template",function()guard.begin("enable",admin)end)
            check(equal(before,S.cfg) and not guard.active())
        end
    end)
    test(name.." detects template changes between queue and apply",function()
        reset(); use_proxy(name); local before=clone(S.cfg); guard.begin("enable",admin)
        local path="/etc/"..name.."/ucode/hijack.ut"; S.files[path]="upgraded template"
        guard.work(false)
        check(equal(before,S.cfg) and S.files[path]=="upgraded template" and not guard.active())
        check(guard.status(admin).last.failure=="stale_revision")
    end)
    test(name.." restores before boot without requiring the proxy process",function()
        reset(); use_proxy(name); local before=clone(S.cfg); enable()
        local state=decode(S.files[root.."state.json"]); state.phase="pending"; S.files[root.."state.json"]=encode(state)
        S.proxy_running=nil; guard.work(true)
        check(equal(before,S.cfg) and not guard.active())
    end)
    test(name.." preserves later ACL and template edits on disable",function()
        reset(); use_proxy(name); enable()
        local path="/etc/"..name.."/ucode/hijack.ut"; S.files[path]="upgraded template"
        S.cfg[name].first.ip={"192.168.1.0/24"}; S.cfg[name].config.profile="subscription:new"
        disable()
        check(S.files[path]=="upgraded template" and S.cfg[name].first.enabled=="1")
        check(S.cfg[name].first.ip[1]=="192.168.1.0/24" and S.cfg[name].config.profile=="subscription:new")
    end)
    test(name.." pending UCI edits prevent changes",function()
        reset(); use_proxy(name); S.pending=name; local before=clone(S.cfg)
        expect("pending_changes",function()guard.begin("enable",admin)end)
        check(equal(before,S.cfg) and not guard.active())
    end)
    test(name.." core-only and invalid protocol modes are rejected",function()
        reset(); use_proxy(name); S.cfg[name].config.core_only="1"
        expect("guard_proxy_mode",function()guard.begin("enable",admin)end)
        S.cfg[name].config.core_only="0"; S.cfg[name].proxy.udp_mode="redirect"
        expect("guard_proxy_mode",function()guard.begin("enable",admin)end)
    end)
end
test("multiple enabled proxies are rejected without changing configuration",function()
    reset(); use_proxy("nikki"); S.cfg.openclash.config.enable="1"; local before=clone(S.cfg)
    expect("guard_proxy_conflict",function()guard.begin("enable",admin)end)
    check(equal(before,S.cfg) and not guard.active())
end)
local backend=require "luci.model.ocserv_easy.backend"
local function normal_proxy(name)
    reset();use_proxy(name)
    S.cfg.ocserv.route1=nil;S.files["/etc/config/ocserv"]=encode(S.cfg.ocserv)
    S.files["/etc/ocserv/ocserv.conf.local"]="# preserve me\n"
end
local function connect()
    return backend.sync_proxy()
end
for _,name in ipairs({"nikki","momo"}) do
    test(name.." normal VPN access retains LAN interfaces, ACLs and the VPN daemon",function()
        normal_proxy(name);local before=clone(S.cfg)
        check(connect().effect=="proxy_connected" and not guard.active())
        check(equal(S.cfg[name].proxy.lan_inbound_interface,before[name].proxy.lan_inbound_interface))
        check(equal(S.cfg[name].first,before[name].first) and equal(S.cfg[name].last,before[name].last))
        check(S.cfg[name].lansyra_vpn[".index"]==0 and equal(S.cfg[name].lansyra_vpn.ip,{"10.77.0.0/24"}))
        check(S.cfg[name].proxy.ipv4_dns_hijack=="1" and equal(S.cfg.ocserv,before.ocserv) and equal(S.cfg.firewall,before.firewall))
        for _,cmd in ipairs(S.commands) do check(cmd~="/etc/init.d/ocserv restart") end
        local raw=S.files["/etc/config/"..name];check(connect().effect=="unchanged" and S.files["/etc/config/"..name]==raw)
        local writes=S.flash_writes
        for _=1,10 do check(connect().effect=="unchanged") end
        check(S.flash_writes==writes,"repeat lifecycle events wrote to flash")
        enable();disable();check(S.files["/etc/config/"..name] and S.cfg[name].lansyra_vpn.enabled=="1")
    end)
    test(name.." normal proxy restart failure restores the template and configuration",function()
        normal_proxy(name);local before=clone(S.cfg);local path="/etc/"..name.."/ucode/hijack.ut";local template=S.files[path]
        S.fail_command="/etc/init.d/"..name.." restart"
        expect("guard_proxy_failed",connect)
        check(equal(S.cfg,before) and S.files[path]==template and not require("luci.model.ocserv_easy.configuration").pending())
    end)
    test(name.." automatic proxy integration respects pending edits and split tunneling",function()
        normal_proxy(name);local before=clone(S.cfg);S.pending=name
        expect("pending_changes",connect);check(equal(S.cfg,before))
        S.pending=nil;S.cfg.ocserv.route1=row("routes",{ip="192.168.40.0",netmask="255.255.255.0"})
        S.files["/etc/config/ocserv"]=encode(S.cfg.ocserv)
        connect();check(S.cfg.ocserv.route1 and S.cfg[name].lansyra_vpn.enabled=="1")
    end)
    test(name.." normal mode enables IPv6 DNS and proxy access only for a configured pool",function()
        normal_proxy(name);S.cfg.ocserv.config.ip6addr="fd77::/64";S.files["/etc/config/ocserv"]=encode(S.cfg.ocserv)
        connect();check(equal(S.cfg[name].lansyra_vpn.ip6,{"fd77::/64"}) and S.cfg[name].proxy.ipv6_dns_hijack=="1")
    end)
    test(name.." automatic integration remains dormant while isolation is active",function()
        normal_proxy(name);enable();local before=clone(S.cfg);local count=#S.commands
        check(connect().effect=="unchanged" and equal(before,S.cfg) and #S.commands==count)
        disable();connect();check(not guard.active() and S.cfg[name].lansyra_vpn.enabled=="1")
    end)
    test(name.." automatic integration repairs an upgraded template without replacing LAN settings",function()
        normal_proxy(name);connect()
        local path="/etc/"..name.."/ucode/hijack.ut";S.files[path]="{% const lan_inbound_device = []; %}\n# new upstream\n"
        check(connect().effect=="proxy_connected" and S.files[path]:find("# new upstream",1,true))
        check(S.cfg[name].first.enabled=="1" and S.cfg[name].proxy.lan_inbound_interface[1]=="lan")
    end)
    test(name.." completed proxy transactions discard obsolete recovery and staging copies",function()
        normal_proxy(name);connect()
        check(not S.files["/etc/ocserv/easy-transaction/"..name..".before"] and
            not S.files["/etc/ocserv/easy-transaction/"..name.."_template.after"] and
            not S.files["/var/run/ocserv-easy/config-stage/"..name])
        check(S.files["/etc/ocserv/easy-backup/"..name..".uci"])
    end)
end
test("automatic proxy integration waits on the shared lease without touching UCI",function()
    normal_proxy("nikki");S.locked=true;local before=clone(S.cfg)
    check(connect()==false and equal(before,S.cfg))
end)
TEST_GUARD_PREVIEW={reset=reset,guard=guard,use_proxy=use_proxy,state=function()return S end}
return results
