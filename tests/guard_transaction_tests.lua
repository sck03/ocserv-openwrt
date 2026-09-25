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
local function cursor()
    local cache={}
    local function db(package) if not cache[package] then cache[package]=clone(S.cfg[package] or {}) end; return cache[package] end
    local c={}
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
        table.sort(rows,function(a,b)return a[".name"]<b[".name"]end)
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
        S.cfg[package]=clone(db(package)); S.files["/etc/config/"..package]=encode(db(package)); return true
    end
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
    S.files[to]=S.files[from]; S.files[from]=nil; return true
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
    mode(permissions)
    S.files[path]=""
    return {write=function(_,v)S.files[path]=S.files[path]..v;return #v end,sync=function()return true end,close=function()end,lock=function()return not S.locked end}
end
function nixio.poll(_,ms)
    S.now=S.now+ms/1000
    local state=decode(S.files[root.."state.json"])
    if S.preview and state and state.phase=="pending" then coroutine.yield(); return end
    if S.auto_confirm and state and state.phase=="pending" then
        guard.begin("confirm",admin,state.token)
    end
end
local function run(args)
    local command=table.concat(args," "); S.commands[#S.commands+1]=command
    if command==S.fail_command then S.fail_command=nil; return 1,"failure" end
    if args[1]=="/bin/pidof" then
        return S.openclash_process==args[2] and 0 or 1,""
    end
    if args[1]=="/bin/ubus" then
        if args[3]=="network.interface.lan" then return 0,encode({up=true,l3_device="br-lan",["ipv4-address"]={{address="192.168.19.253",mask=24}}}) end
        return 0,encode({up=S.wan or false})
    end
    if command=="/usr/sbin/nft -f "..root.."guard.nft" then S.nft=true end
    if command=="/usr/sbin/nft delete table inet bulijie_guard" then S.nft=false end
    if command=="/usr/sbin/nft list table inet bulijie_guard" then return S.nft and 0 or 1,"" end
    return 0,""
end
package.loaded["nixio"]=nixio
package.loaded["nixio.fs"]=fs
package.loaded["luci.model.uci"]={cursor=cursor}
package.loaded["luci.jsonc"]={parse=decode,stringify=encode}
package.loaded["luci.model.ocserv_easy.process"]={run=run}
guard=require "luci.model.ocserv_easy.guard"
local function enable() guard.begin("enable",admin); guard.work(false); check(guard.status(admin).phase=="enabled",decode(S.files[root.."result.json"]) and decode(S.files[root.."result.json"]).failure) end
local function disable() guard.begin("disable",admin); guard.work(false); check(guard.status(admin).phase=="disabled") end

test("default is off and detection writes no router configuration",function()
    reset(); local before=clone(S.cfg); check(guard.status(admin).phase=="disabled" and guard.status(admin).available); check(equal(before,S.cfg) and not S.nft)
end)
test("Meta/Mihomo OpenClash core is accepted",function()
    reset(); S.openclash_process="mihomo"; local before=clone(S.cfg)
    check(guard.status(admin).phase=="disabled" and guard.status(admin).available)
    check(equal(before,S.cfg) and not S.nft); enable(); check(S.nft); disable()
end)
test("legacy host-form VPN pool is normalized for guard rules",function()
    reset(); S.cfg.ocserv.config.ipaddr="192.168.100.1"; local before=clone(S.cfg)
    check(guard.status(admin).phase=="disabled" and guard.status(admin).available)
    enable()
    check(S.cfg.ocserv.config.ipaddr=="192.168.100.1")
    check(equal(S.cfg.openclash.config.lan_ac_white_ips,{"192.168.100.0/24"}))
    check(S.files[root.."guard.nft"]:find("192.168.100.0/24",1,true))
    disable(); check(equal(before,S.cfg))
end)
test("enable queues work without blocking the LuCI request",function()
    reset(); local before=clone(S.cfg); guard.begin("enable",admin); check(guard.status(admin).phase=="queued" and equal(before,S.cfg) and not S.nft)
end)
test("enabling configures real interface guard and VPN-only proxy whitelist",function()
    reset(); S.cfg.openclash.config.enable_redirect_dns="1"; local before=clone(S.cfg)
    enable(); check(S.nft and S.cfg.openclash.config.lan_ac_mode=="1")
    check(equal(S.cfg.dhcp,before.dhcp))
    check(equal(S.cfg.openclash.config.lan_ac_white_ips,{"10.77.0.0/24"}) and not S.cfg.openclash.config.lan_ac_white_macs)
    check(S.cfg.openclash.config.enable_redirect_dns=="2")
    check(S.cfg.firewall.defaults.flow_offloading=="0" and S.cfg.firewall.defaults.flow_offloading_hw=="0")
    check(equal(S.cfg.firewall.lan.device,{"br-lan"}) and S.cfg.ocserv.bulijie_guard_dns.ip=="10.77.0.1" and not S.cfg.ocserv.route1)
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
    guard.begin("enable",admin); guard.work(false); check(equal(before,S.cfg) and not S.nft and guard.status(admin).last.failure=="guard_openclash_failed")
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
    reset(); enable(); S.cfg.firewall.bulijie_guard.enabled="0"; disable(); check(not S.cfg.firewall.bulijie_guard and not S.nft)
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
    reset(); S.cfg.firewall.bulijie_guard=row("include",{path="/etc/custom-guard.nft"}); local before=clone(S.cfg)
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
TEST_GUARD_PREVIEW={reset=reset,guard=guard,state=function()return S end}
return results
