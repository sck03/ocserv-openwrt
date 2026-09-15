-- Exercised with the actual Lua 5.1 runtime used by LuCI. No router is modified.
local logic=require "luci.model.ocserv_easy.logic"
local results={}
local function check(value,message) assert(value,message or "check failed") end
local function expect(code,fn)
    local ok,err=pcall(fn)
    check(not ok and type(err)=="table" and err.code==code,"expected error: "..code)
end
local function test(name,fn) fn(); results[#results+1]=name end
local function clone(value)
    if type(value)~="table" then return value end
    local out={}; for k,v in pairs(value) do out[k]=clone(v) end; return out
end
local function fixture_settings()
    return {port="4443",udp_port="",max_clients="16",max_same="1",dpd="60",udp="1",compression="0",cisco_compat="1",predictable_ips="1",
        ipaddr="10.77.0.0",netmask="24",ip6addr="",default_domain="",easy_public_url="https://vpn.example.com:4443",dns={"10.77.0.1"},route_mode="split",routes={"192.168.19.0/24"}}
end
local old_hash="$6$existingSalt$"..string.rep("a",86)
local function hasher() return "$6$newSalt$"..string.rep("b",86) end
local original={{id="employee",name="employee01",group="*",password=old_hash}}
test("1.5 minimum and future versions",function()
    for _,v in ipairs({"1.5.0","1.5.0-r1","1.6.0","2.0.0"}) do check(logic.supported_version(v)) end
    for _,v in ipairs({"1.3.0","1.4.9","unknown","1.5","invalid"}) do check(not logic.supported_version(v)) end
end)
test("URL validation blocks credentials and injection",function()
    for _,s in ipairs({"https://vpn.example.com:4443","https://192.168.19.254:4443","https://[2001:db8::1]:443"}) do check(logic.url(s)) end
    for _,s in ipairs({"http://example.com","https://user:pass@example.com","https://example.com\nPassword=bad","https://example.com?secret","https://example.com:0","https://999.1.1.1","https://example.com\\bad","https://[:::1]"}) do check(not logic.url(s)) end
end)
test("IPv4 and IPv6 boundary validation",function()
    check(logic.ipv4("255.255.255.255")==4294967295)
    check(not logic.ipv4("192.168.01.1")); check(not logic.ipv4("1.2.3.256"))
    for _,s in ipairs({"::","::1","2001:db8::","2001:db8:1:2:3:4:5:6"}) do check(logic.ipv6(s)) end
    for _,s in ipairs({"1::2::3",":1:2:3:4:5:6:7", "1:2:3:4:5:6:7:","gg::1","12345::1"}) do check(not logic.ipv6(s)) end
    check(logic.prefix("255.255.255.0")==24); check(not logic.prefix("255.0.255.0"))
end)
test("public account data never includes hashes",function()
    local rows=logic.public_users(original)
    check(rows[1].name=="employee01" and rows[1].password==nil and rows[1].enabled)
end)
test("blank edit preserves password and avoids revocation",function()
    local rows,revoke=logic.change_user(original,{action="save_user",id="employee",name="employee01",group="*",password="",enabled=true},hasher)
    check(rows[1].password==old_hash and revoke==nil)
end)
test("password reset revokes only the old account",function()
    local rows,revoke=logic.change_user(original,{action="save_user",id="employee",name="employee01",group="*",password="FixturePassword",enabled=true},hasher)
    check(rows[1].password~=old_hash and revoke=="employee01")
    check(original[1].password==old_hash)
end)
test("disable and enable preserve original crypt hash",function()
    local rows,revoke=logic.change_user(original,{action="save_user",id="employee",name="employee01",group="*",password="",enabled=false},hasher)
    check(rows[1].password=="!"..old_hash and revoke=="employee01")
    rows,revoke=logic.change_user(rows,{action="save_user",id="employee",name="employee01",group="*",password="",enabled=true},hasher)
    check(rows[1].password==old_hash and revoke==nil)
end)
test("renaming and deletion revoke the original name",function()
    local rows,revoke=logic.change_user(original,{action="save_user",id="employee",name="renamed",password="",enabled=true},hasher)
    check(rows[1].name=="renamed" and revoke=="employee01")
    rows,revoke=logic.change_user(original,{action="delete_user",id="employee"},hasher)
    check(#rows==0 and revoke=="employee01")
end)
test("new account requires password",function()
    expect("password_required",function() logic.change_user(original,{action="save_user",name="new",enabled=true},hasher) end)
end)
test("duplicates and unknown section rejected",function()
    expect("duplicate_user",function() logic.change_user(original,{action="save_user",name="employee01",password="FixturePassword",enabled=true},hasher) end)
    expect("user_not_found",function() logic.change_user(original,{action="delete_user",id="missing"},hasher) end)
end)
test("password-like crypt strings are still hashed as input",function()
    local called=false
    local rows=logic.change_user(original,{action="save_user",name="new",password="$6$notAnExistingHash",enabled=true},function(value)called=true;return hasher(value)end)
    check(called and rows[2].password==hasher())
end)
test("account delimiters and weak hashes fail closed",function()
    for _,name in ipairs({"-option","a:b","a\nb","a;id","a'$(id)",string.rep("a",65)}) do
        expect("invalid_username",function()logic.change_user(original,{action="save_user",name=name,password="FixturePassword",enabled=true},hasher)end)
    end
    expect("hash_failed",function()logic.change_user(original,{action="save_user",name="new",password="FixturePassword",enabled=true},function()return "*0"end)end)
end)
test("network defaults generate native UCI values",function()
    local config,dns,routes=logic.validate_settings(fixture_settings())
    check(config.netmask=="255.255.255.0" and dns[1].ip=="10.77.0.1" and routes[1].netmask=="255.255.255.0")
end)
test("invalid pool, DNS endpoint and routes rejected",function()
    local s=fixture_settings();s.ipaddr="10.77.0.1";expect("pool_not_network",function()logic.validate_settings(s)end)
    s=fixture_settings();s.netmask="255.0.255.0";expect("invalid_pool",function()logic.validate_settings(s)end)
    s=fixture_settings();s.dns={"vpn.example.com"};expect("invalid_dns",function()logic.validate_settings(s)end)
    s=fixture_settings();s.easy_public_url="https://10.77.0.1:4443";expect("dns_is_endpoint",function()logic.validate_settings(s)end)
    s=fixture_settings();s.routes={"192.168.19.1/24"};expect("invalid_routes",function()logic.validate_settings(s)end)
end)
test("full tunnel and zero DNS validation",function()
    local s=fixture_settings();s.route_mode="all";s.routes={};local _,_,routes=logic.validate_settings(s);check(#routes==0)
    s.dns={};expect("invalid_dns",function()logic.validate_settings(s)end)
end)
test("default authentication is plain and export guards custom auth",function()
    local cfg,dns,routes=logic.validate_settings(fixture_settings())
    local rendered=logic.render(TEST_TEMPLATE,"",cfg,dns,routes,{domain="lan"})
    check(logic.standard_auth(rendered));check(not rendered:find("|PORT|",1,true));check(rendered:find("max-same-clients = 1",1,true))
    check(not logic.standard_auth(rendered..'\nauth = "pam"\n'))
    cfg.auth="pam";expect("plain_auth_required",function()logic.render(TEST_TEMPLATE,"",cfg,dns,routes)end)
end)
test("unknown templates and proxy ARP not silently overwritten",function()
    local cfg=logic.validate_settings(fixture_settings())
    expect("unknown_template",function()logic.render("|NEW_UNSUPPORTED_OPTION|","",cfg,{}, {})end)
    cfg.proxy_arp="1";expect("proxy_arp_managed",function()logic.render(TEST_TEMPLATE,"",cfg,{}, {})end)
end)

-- In-memory OpenWrt boundary. Exercises real backend transactions, not a rewrite
-- of them. External UCI, nixio, occtl and procd are the mocked interfaces.
local S
local function serialize(db)
    local lines={}
    for _,row in ipairs(db) do
        lines[#lines+1]=row[".name"]..":"..row[".type"]
        local keys={};for key in pairs(row) do if key:sub(1,1)~="." then keys[#keys+1]=key end end
        table.sort(keys);for _,key in ipairs(keys) do lines[#lines+1]=key.."="..tostring(row[key]) end
    end
    return table.concat(lines,"\n")
end
local function set_running(value)
    S.running=value;S.files["/var/run/ocserv.pid"]=value and "101\n" or nil
    S.files["/proc/101/cmdline"]=value and "ocserv-main\0" or nil
end
local function reset()
    local cfg=logic.validate_settings(fixture_settings());cfg[".name"]="config";cfg[".type"]="ocserv"
    S={files={},dirs={},history={},commands={},version="1.5.0",counter=0,online={},sessions={}}
    S.db={cfg,{[".name"]="employee",[".type"]="ocservusers",name="employee01",group="*",password=old_hash},
        {[".name"]="dns1",[".type"]="dns",ip="10.77.0.1"}}
    S.files["/etc/config/ocserv"]=serialize(S.db);S.history[S.files["/etc/config/ocserv"]]=clone(S.db)
    S.files["/etc/ocserv/ocserv.conf.template"]=TEST_TEMPLATE
    S.files["/etc/ocserv/ocserv.conf.local"]=""
    S.files["/var/etc/ocpasswd"]=logic.passwd(original)
    S.files["/var/etc/ocserv.conf"]='auth = "plain[passwd=/var/etc/ocpasswd]"\n'
    S.files["/etc/ocserv/ca.pem"]="-----BEGIN CERTIFICATE-----\nTEST PUBLIC CA\n-----END CERTIFICATE-----\n"
    set_running(true)
end
local original_io_open=io.open
io.open=function(path)
    if not S or S.files[path]==nil then return nil end
    return {read=function(_,limit)return S.files[path]:sub(1,limit)end,close=function()end}
end
local fs={
    stat=function(path)return S.dirs[path] or S.files[path] and {} end,
    access=function(path)return S.dirs[path]~=nil or S.files[path]~=nil end,
    readlink=function(path)if S.running and path=="/proc/101/exe"then return "/usr/sbin/ocserv"end end,
    mkdir=function(path,mode)S.dirs[path]={mode=mode};return true end,
    chmod=function()return true end,
    remove=function(path)S.files[path]=nil;return true end,
    rename=function(from,to)
        if S.fail_write==to then S.fail_write=nil;return nil end
        S.files[to]=S.files[from];S.files[from]=nil
        if to=="/etc/config/ocserv" and S.history[S.files[to]] then S.db=clone(S.history[S.files[to]]) end
        return true
    end
}
local nixio={bin={},getpid=function()return 42 end,open_flags=function()return 1 end,poll=function()end}
nixio.bin.hexlify=function(value)return (value:gsub(".",function(c)return string.format("%02x",c:byte())end)) end
nixio.bin.b64encode=TEST_BASE64
nixio.crypt=function()if S.fail_hash then return "*0" end return hasher()end
nixio.open=function(path,flags,mode)
    if path=="/dev/urandom" then return {read=function(_,count)S.counter=S.counter+1;return string.rep(string.char(S.counter%255),count)end,close=function()end} end
    S.files[path]=""
    return {write=function(_,data)S.files[path]=S.files[path]..data;return #data end,sync=function()return true end,close=function()end,lock=function()return not S.busy end}
end
local function cursor()
    local db=clone(S.db)
    local function find(id)for _,row in ipairs(db)do if row[".name"]==id then return row end end end
    return {
        load=function()return true end,unload=function()end,
        get=function(_,package,id,key)local row=find(id);return row and row[key or ".type"]end,
        get_all=function(_,package,id)return clone(find(id))end,
        get_first=function()return "lan"end,
        foreach=function(_,package,kind,fn)if package=="ocserv"then for _,row in ipairs(db)do if row[".type"]==kind then fn(clone(row))end end end end,
        changes=function()return S.pending and {ocserv={"pending"}} or {}end,
        add=function(_,package,kind)local id="added"..tostring(#db+1);db[#db+1]={[".name"]=id,[".type"]=kind};return id end,
        set=function(_,package,id,key,value)local row=find(id);if not row then return nil end;row[key]=value;return true end,
        delete=function(_,package,id)for i,row in ipairs(db)do if row[".name"]==id then table.remove(db,i);return true end end end,
        revert=function()db=clone(S.db);return true end,
        commit=function()
            if S.fail_commit then return nil end
            local raw=serialize(db);S.history[raw]=clone(db);S.files["/etc/config/ocserv"]=raw;S.db=clone(db);return true
        end
    }
end
local function run(argv)
    local command=table.concat(argv," ");S.commands[#S.commands+1]=command
    if command=="/usr/sbin/ocserv --version"then return 0,"ocserv "..S.version.."\n"end
    if command:find("--test-config",1,true)then S.candidate=S.files[argv[4]];return S.bad_config and 1 or 0,""end
    if command:find("occtl --help",1,true)or command:find("occtl help",1,true)then return 0,"terminate user\nshow sessions all"end
    if command:find("occtl -j show users",1,true)then return 0,"ONLINE"end
    if command:find("occtl -j show sessions all",1,true)then return S.bad_sessions and 1 or 0,"SESSIONS"end
    if command:find("occtl terminate user",1,true)then return S.fail_terminate and 1 or 0,""end
    if command:find("occtl disconnect id",1,true)then return 0,"disconnected"end
    if command=="/etc/init.d/ocserv enabled"then return 0,""end
    if command=="/etc/init.d/ocserv restart"then
        if S.fail_restart then S.fail_restart=nil;set_running(false);return 1,""end
        set_running(true);return 0,""
    end
    if command=="/etc/init.d/ocserv stop"then set_running(false);return 0,""end
    if command=="/etc/init.d/ocserv start"then set_running(true);return 0,""end
    return 0,""
end
package.loaded["nixio"]=nixio;package.loaded["nixio.fs"]=fs
package.loaded["luci.model.uci"]={cursor=cursor}
package.loaded["luci.model.ocserv_easy.process"]={run=run}
package.loaded["luci.jsonc"]={parse=function(value)if value=="ONLINE"then return clone(S.online)elseif value=="SESSIONS"then return clone(S.sessions)end end}
local backend=require "luci.model.ocserv_easy.backend"
local function request(payload)payload.revision=payload.revision or backend.data().revision;return backend.action(payload)end
local function edit(password,enabled)return {action="save_user",id="employee",name="employee01",group="*",password=password or "",enabled=enabled~=false}end
local function contains_command(part)for _,cmd in ipairs(S.commands)do if cmd:find(part,1,true)then return true end end return false end
test("backend default plain auth and no hash disclosure",function()
    reset();local data=backend.data();check(data.auth=="plain" and data.supported and data.running and data.users[1].password==nil)
end)
test("saving an unchanged account avoids flash writes",function()
    reset();local result=request(edit());check(result.effect=="unchanged" and S.files["/etc/ocserv/easy-backup/ocserv.uci"]==nil)
end)
test("new account live update without service restart",function()
    reset();local result=request({action="save_user",name="new",group="*",password="FixturePassword",enabled=true})
    check(result.effect=="updated_live" and S.files["/var/etc/ocpasswd"]:find("new:*:",1,true))
    check(not contains_command("restart") and not contains_command("terminate user"))
    check(not S.files["/etc/config/ocserv"]:find("FixturePassword",1,true))
end)
test("password reset sends only username to occtl",function()
    reset();request(edit("FixturePassword"));check(contains_command("occtl terminate user employee01"))
    check(not contains_command("FixturePassword") and not contains_command("restart"))
end)
test("live disable then enable matches native password-file format",function()
    reset();request(edit("",false));check(S.files["/var/etc/ocpasswd"]:find(":!$6$",1,true))
    request(edit("",true));check(not S.files["/var/etc/ocpasswd"]:find(":!",1,true))
end)
test("stale revision and pending UCI changes rejected before writes",function()
    reset();local raw=S.files["/etc/config/ocserv"]
    expect("stale_revision",function()request({action="delete_user",id="employee",revision="stale"})end)
    S.pending=true;expect("pending_changes",function()request({action="delete_user",id="employee"})end)
    check(S.files["/etc/config/ocserv"]==raw)
end)
test("runtime password write failure rolls back persistent config",function()
    reset();local raw=S.files["/etc/config/ocserv"];local passwd=S.files["/var/etc/ocpasswd"]
    S.fail_write="/var/etc/ocpasswd";expect("write_failed",function()request(edit("FixturePassword"))end)
    check(S.files["/etc/config/ocserv"]==raw and S.files["/var/etc/ocpasswd"]==passwd)
end)
test("failed revocation with remaining session rolls back",function()
    reset();local raw=S.files["/etc/config/ocserv"];S.fail_terminate=true;S.sessions={{Username="employee01"}}
    expect("terminate_failed",function()request(edit("FixturePassword"))end)
    check(S.files["/etc/config/ocserv"]==raw)
end)
test("offline account with no cached sessions may be deleted",function()
    reset();S.fail_terminate=true;S.sessions={};request({action="delete_user",id="employee"})
    check(#backend.data().users==0 and contains_command("show sessions all"))
end)
test("custom password paths fail closed",function()
    reset();S.files["/var/etc/ocserv.conf"]='auth = "plain[passwd=/etc/custom]"\n'
    expect("custom_auth_file",function()request(edit("FixturePassword"))end)
end)
test("old ocserv versions require upgrade",function()
    reset();S.version="1.3.0";check(not backend.data().supported)
    expect("upgrade_required",function()request(edit("FixturePassword"))end)
end)
test("configuration is tested before commit",function()
    reset();local raw=S.files["/etc/config/ocserv"];S.bad_config=true
    expect("config_check_failed",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
    check(S.files["/etc/config/ocserv"]==raw and not contains_command("restart"))
end)
test("network settings apply after validation and preserve users",function()
    reset();local settings=fixture_settings();settings.port="5443"
    local result=request({action="settings",settings=settings,allow_restart=true})
    check(result.effect=="restarted" and S.candidate:find("tcp-port = 5443",1,true))
    check(backend.data().users[1].name=="employee01")
end)
test("failed restart restores old configuration and service",function()
    reset();local raw=S.files["/etc/config/ocserv"];S.fail_restart=true
    expect("restart_failed",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
    check(S.files["/etc/config/ocserv"]==raw and S.running)
end)
test("service stopped remains stopped when settings saved",function()
    reset();set_running(false);local result=request({action="settings",settings=fixture_settings()})
    check(result.effect=="saved" and not contains_command("restart") and not S.running)
end)
test("custom extra configuration is preserved and not silently overridden",function()
    reset();S.files["/etc/ocserv/ocserv.conf.local"]="max-same-clients = 7\n"
    expect("custom_override",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
end)
test("downloads include public CA only and reject private-key mixtures",function()
    reset();local profile,name=backend.export("profile","https://vpn.example.com:4443")
    check(name=="BulijieVPN.bvpn" and profile:find("Server=https://vpn.example.com:4443",1,true))
    check(not profile:find("Password",1,true) and not profile:find(old_hash,1,true))
    S.files["/etc/ocserv/ca.pem"]=S.files["/etc/ocserv/ca.pem"].."-----BEGIN PRIVATE KEY-----\n"
    expect("ca_unavailable",function()backend.export("ca","")end)
end)
test("address profiles work without a CA and normalize a LAN address",function()
    reset();S.files["/etc/ocserv/ca.pem"]=nil
    local profile,name=backend.export("address","192.168.19.253:4443")
    check(name=="BulijieVPN.bvpn" and profile=="[VPN]\nServer=https://192.168.19.253:4443\n")
end)
test("profile downloads reject insecure addresses and injected lines",function()
    reset()
    for _,url in ipairs({"http://vpn.example.com","vpn.example.com\nServerPin=bad","vpn.example.com:99999","user:password@vpn.example.com"}) do
        expect("invalid_url",function()backend.export("address",url)end)
    end
    expect("bad_request",function()backend.export("private-key","")end)
end)
-- A separate local preview process may keep this isolated in-memory backend.
TEST_PREVIEW={reset=reset,backend=backend,request=request,state=function()return S end}
if not TEST_KEEP_SANDBOX then io.open=original_io_open end
return results
