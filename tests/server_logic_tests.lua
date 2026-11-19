-- Exercised with the actual Lua 5.1 runtime used by LuCI. No router is modified.
local logic=require "ocserv.logic"
local results={}
local function check(value,message) assert(value,message or "check failed") end
local function expect(code,fn)
    local ok,err=pcall(fn)
    check(not ok and type(err)=="table" and err.code==code,"expected error: "..code)
end
local function test(name,fn)
    local ok,err=pcall(fn)
    check(ok,name..": "..(type(err)=="table" and tostring(err.code) or tostring(err)))
    results[#results+1]=name
end
local fixture=require "test.server_fixture"
local clone,fixture_settings,hasher=fixture.clone,fixture.settings,fixture.hasher
local old_hash,test_pin,original=fixture.old_hash,fixture.test_pin,fixture.original
local fs,nixio,cursor,run=fixture.fs,fixture.nixio,fixture.cursor,fixture.run
local serialize,set_running=fixture.serialize,fixture.set_running
local backend,request,edit=fixture.backend,fixture.request,fixture.edit
local contains_command,no_candidates=fixture.contains_command,fixture.no_candidates
local original_io_open=fixture.original_io_open
local S
local function reset()fixture.reset();S=fixture.state()end
test("JSON null tokens are rejected before jsonc can discard fields or array tails",function()
    for _,raw in ipairs({'null','{"dns":["1.1.1.1",null]}','{"routes":[null]}',
        '{"settings":{"udp":null}}','{"password":"null","settings":null}'}) do
        check(logic.json_has_null(raw),raw)
    end
end)
test("JSON null filtering preserves quoted text and escaped string boundaries",function()
    for _,raw in ipairs({'{"password":"null"}','{"null":"annulled","enabled":false}',
        [[{"password":"escaped\"null"}]],[[{"password":"backslash\\null"}]],
        [[{"password":"escaped\"null\\","enabled":true}]],'{"dns":[]}'}) do
        check(not logic.json_has_null(raw),raw)
    end
    check(logic.json_has_null([[{"password":"backslash\\","dns":[null]}]]))
end)
test("JSON null scanning handles large snapshots and every escaped quote boundary",function()
    for count=0,32 do
        local prefix='{"snapshot":"'..string.rep('\\',count)
        local raw=prefix..'"null"}'
        check(logic.json_has_null(raw)==(count%2==0))
    end
    local snapshot='{"snapshot":"'..string.rep('annulled \\" null ',10000)..'"'
    check(not logic.json_has_null(snapshot..'}'))
    check(logic.json_has_null(snapshot..',"files":[null]}'))
end)
test("1.6 minimum and future versions",function()
    for _,v in ipairs({"1.6.0","1.6.0-r1","1.7.0","2.0.0"}) do check(logic.supported_version(v)) end
    for _,v in ipairs({"1.5.0","1.5.9","unknown","1.6","invalid"}) do check(not logic.supported_version(v)) end
end)
test("revision respects OpenWrt signed 32-bit formatting",function()
    local native_format=string.format
    string.format=function(format,...)
        for _,value in ipairs({...})do
            check(type(value)=="number" and value%1==0 and value>=-2147483648 and value<=2147483647,"OpenWrt integer formatter rejected an argument")
        end
        return native_format(format,...)
    end
    local ok,result=pcall(logic.revision,string.rep("overflow-fixture",100))
    string.format=native_format
    check(ok and #result==24,"revision failed on OpenWrt's integer ABI")
end)
test("revision consumes complete chunks, empty fields and binary separators",function()
    check(logic.revision("a","\0","bc","\0","def","\0","")=="d16bd13ac484f87400000009")
    check(logic.revision("","a\0b","","c\0def\0")=="d16bd13ac484f87400000009")
    local first,last=string.rep("x",262144),string.rep("y",262144)
    check(logic.revision(first,"\0",last)==logic.revision(first.."\0"..last))
    check(logic.revision(first,"\0",last)~=logic.revision(last,"\0",first))
    check(logic.revision("a","\0","bc")~=logic.revision("ab","\0","c"))
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
test("compressed IPv6 rejects dangling colons in addresses and public URLs",function()
    for _,s in ipairs({"::1:",":1::","2001:db8::1:",":2001:db8::", "1:2:3:4:5:6:7:8::"}) do
        check(not logic.ipv6(s),s)
        check(not logic.url("https://["..s.."]:4443"),s)
    end
    for _,s in ipairs({"::", "::1", "1::", "1::8", "1:2:3:4:5:6:7::"}) do check(logic.ipv6(s),s) end
end)
test("public account data never includes hashes",function()
    local rows=logic.public_users(original)
    check(rows[1].name=="employee01" and rows[1].password==nil and rows[1].enabled)
end)
test("invalid account records cannot enter ocpasswd as plaintext",function()
    local users={{id="legacy",name="legacy",password="literal:password"},{id="broken",name="broken",password="$6$invalid"}}
    local rows=logic.public_users(users)
    check(rows[1].needs_password and not rows[1].enabled and rows[1].group=="*")
    local passwd=logic.passwd(users)
    check(passwd=="legacy:*:!\nbroken:*:!\n" and not passwd:find("literal",1,true))
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
test("DNS and split routes reject sparse or object-shaped lists",function()
    for _,field in ipairs({"dns","routes"}) do
        local value=field=="dns" and "1.1.1.1" or "192.168.19.0/24"
        for _,list in ipairs({{value,nil,value},{nil,value,value},{["1"]=value},{value,extra=value},{}}) do
            local s=fixture_settings(); s[field]=list
            expect("invalid_"..field,function()logic.validate_settings(s)end)
        end
    end
end)
test("network lists enforce input budgets before normalization",function()
    for _,field in ipairs({"dns","routes"}) do
        local s=fixture_settings(); s[field]={}
        local limit=field=="dns" and 3 or 128
        for i=1,limit+1 do s[field][i]=field=="dns" and "1.1.1.1" or "192.168.19.0/24" end
        expect("invalid_"..field,function()logic.validate_settings(s)end)
    end
end)
test("equivalent routes are emitted once while distinct prefixes retain order",function()
    local s=fixture_settings(); s.ip6addr="fd77::/64"
    s.routes={"192.168.19.0/24","2001:db8::/64","192.168.19.0/255.255.255.0",
        "2001:0DB8:0:0:0:0:0:0/064","192.168.19.0/25","2001:db8::/65"}
    local cfg,dns,routes=logic.validate_settings(s)
    check(#routes==4 and routes[1].ip=="192.168.19.0" and routes[2].ip=="2001:db8::")
    check(routes[3].netmask=="255.255.255.128" and routes[4].netmask=="65")
    local _,count=logic.render(TEST_TEMPLATE,"",cfg,dns,routes):gsub("\nroute = ","")
    check(count==4,"duplicate routes reached the daemon configuration")
    s.routes={"192.168.19.0/24","192.168.19.0/24","invalid"}
    expect("invalid_routes",function()logic.validate_settings(s)end)
end)
test("invalid pool, DNS endpoint and routes rejected",function()
    local s=fixture_settings();s.ipaddr="10.77.0.1";expect("pool_not_network",function()logic.validate_settings(s)end)
    s=fixture_settings();s.netmask="255.0.255.0";expect("invalid_pool",function()logic.validate_settings(s)end)
    s=fixture_settings();s.dns={"vpn.example.com"};expect("invalid_dns",function()logic.validate_settings(s)end)
    s=fixture_settings();s.easy_public_url="https://10.77.0.1:4443";expect("dns_is_endpoint",function()logic.validate_settings(s)end)
    s=fixture_settings();s.routes={"192.168.19.1/24"};expect("invalid_routes",function()logic.validate_settings(s)end)
end)
test("IPv6 DNS validates addresses rather than their textual spelling",function()
    for _,address in ipairs({"0:0:0:0:0:0:0:0", "0:0:0:0:0:0:0:1", "fe80::53", "ff02::53"}) do
        local s=fixture_settings(); s.dns={address}
        expect("invalid_dns",function()logic.validate_settings(s)end)
    end
    local s=fixture_settings(); s.dns={"2001:db8::53", "2001:0DB8:0:0:0:0:0:0053"}
    expect("duplicate_dns",function()logic.validate_settings(s)end)
    s.dns={"2001:0DB8:0:0:0:0:0:0053"}; s.easy_public_url="https://[2001:db8::53]:4443"
    expect("dns_is_endpoint",function()logic.validate_settings(s)end)
end)
test("IPv6 pools and routes require aligned network prefixes",function()
    for _,pool in ipairs({"fd77::1/64", "::/64", "fe80::/64", "ff00::/64"}) do
        local s=fixture_settings(); s.ip6addr=pool
        expect("invalid_ipv6_pool",function()logic.validate_settings(s)end)
    end
    local s=fixture_settings(); s.ip6addr="fd77::/64"; s.routes={"2001:db8::1/64"}
    expect("invalid_routes",function()logic.validate_settings(s)end)
    s.routes={"::/0", "2001:db8:1234:8000::/49", "2001:db8::1/128"}
    local _,_,routes=logic.validate_settings(s); check(#routes==3)
end)
test("full tunnel and zero DNS validation",function()
    local s=fixture_settings();s.route_mode="all";s.routes={};local _,_,routes=logic.validate_settings(s);check(#routes==0)
    s.dns={};expect("invalid_dns",function()logic.validate_settings(s)end)
end)
test("default authentication is plain and export guards custom auth",function()
    local cfg,dns,routes=logic.validate_settings(fixture_settings())
    local rendered=logic.render(TEST_TEMPLATE,"",cfg,dns,routes,{domain="lan"})
    check(logic.standard_auth(rendered));check(not rendered:find("|PORT|",1,true));check(rendered:find("max-same-clients = 1",1,true))
    check(rendered:find("listen-host-is-dyndns = true",1,true),"DDNS on an upstream router must also re-resolve on reconnect")
    check(not logic.standard_auth(rendered..'\nauth = "pam"\n'))
    cfg.auth="pam";expect("plain_auth_required",function()logic.render(TEST_TEMPLATE,"",cfg,dns,routes)end)
end)
test("unknown templates and proxy ARP not silently overwritten",function()
    local cfg=logic.validate_settings(fixture_settings())
    expect("unknown_template",function()logic.render("|NEW_UNSUPPORTED_OPTION|","",cfg,{}, {})end)
    cfg.proxy_arp="1";expect("proxy_arp_managed",function()logic.render(TEST_TEMPLATE,"",cfg,{}, {})end)
end)
test("large configurations preserve custom content and all route and DNS entries",function()
    local input=fixture_settings();input.routes={};input.dns={"1.1.1.1","8.8.8.8","2001:4860:4860::8888"}
    for i=0,127 do input.routes[#input.routes+1]="10."..i..".0.0/24" end
    local cfg,dns,routes=logic.validate_settings(input)
    local extra=string.rep("# preserved custom setting\n",4096)
    local rendered=logic.render(TEST_TEMPLATE,extra,cfg,dns,routes)
    check(rendered:find(extra,1,true) and logic.standard_auth(rendered))
    local addresses,servers={},{}
    for line in rendered:gmatch("[^\n]+") do
        local address,mask=line:match("^route = ([^/]+)/(.+)$")
        if address then check(mask=="255.255.255.0");addresses[#addresses+1]=address end
        local server=line:match("^dns = (.+)$")
        if server then servers[#servers+1]=server end
    end
    check(#addresses==128 and logic.equal(servers,input.dns))
    for i=0,127 do check(addresses[i+1]=="10."..i..".0.0") end
end)

test("failed candidate writes release earlier staged configuration files",function()
    reset();local before=S.files["/etc/config/ocserv"]
    S.fail_write="/var/run/ocserv-easy/config-stage/firewall"
    expect("write_failed",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
    no_candidates()
    check(S.files["/etc/config/ocserv"]==before and not contains_command("restart"))
end)
test("failed journal creation releases the private UCI candidate",function()
    reset();local before=S.files["/etc/config/ocserv"]
    S.fail_write="/etc/ocserv/easy-transaction/state.json"
    expect("write_failed",function()request(edit("FixturePassword"))end)
    no_candidates()
    check(S.files["/etc/config/ocserv"]==before and not contains_command("terminate user"))
end)
test("failed candidate loading removes all temporary UCI files",function()
    reset();local before=S.files["/etc/config/ocserv"];S.fail_load="ocserv"
    expect("write_failed",function()request(edit("FixturePassword"))end)
    no_candidates()
    check(S.files["/etc/config/ocserv"]==before and not contains_command("terminate user"))
end)
test("journal sync failure retains durable recovery but releases temporary files",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before=S.files["/etc/config/ocserv"]
    S.fail_directory_sync="/etc/ocserv/easy-transaction"
    expect("write_failed",function()request(edit("FixturePassword"))end)
    no_candidates()
    check(tx.pending() and S.files["/etc/config/ocserv"]==before)
    check(backend.recover(false) and not tx.pending())
end)
test("completion failure releases candidates without discarding the recovery journal",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before,passwd=S.files["/etc/config/ocserv"],S.files["/var/etc/ocpasswd"]
    S.fail_remove="/etc/ocserv/easy-transaction/state.json"
    expect("write_failed",function()request(edit("FixturePassword"))end)
    no_candidates()
    check(tx.pending() and S.files["/etc/config/ocserv"]~=before)
    check(backend.recover(false) and not tx.pending())
    check(S.files["/etc/config/ocserv"]==before and S.files["/var/etc/ocpasswd"]==passwd)
end)
test("repeated failed transactions leave no candidate files and allow a later save",function()
    reset();local before=S.files["/etc/config/ocserv"]
    for _=1,100 do
        S.fail_write="/etc/ocserv/easy-transaction/state.json"
        expect("write_failed",function()request(edit("FixturePassword"))end)
        no_candidates()
        check(S.files["/etc/config/ocserv"]==before)
    end
    check(request(edit("FixturePassword")).effect=="terminated")
    no_candidates()
end)
test("failed preflight writes release the temporary password file",function()
    reset();local before=S.files["/etc/config/ocserv"]
    S.fail_write="/var/run/ocserv-easy/check.conf"
    expect("write_failed",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
    no_candidates()
    check(S.files["/etc/config/ocserv"]==before and not contains_command("--test-config"))
end)
test("the shared maintenance lease excludes actions and recovery",function()
    reset();S.busy=true
    local before=S.files["/etc/config/ocserv"]
    expect("busy",function()request(edit("FixturePassword"))end)
    check(S.files["/etc/config/ocserv"]==before)
    S.busy=false
    local tx=require "luci.model.ocserv_easy.configuration"
    local state=tx.begin(false);tx.replace(state,"ocserv",before,before.."\ninterrupted")
    S.busy=true;check(not backend.recover(false) and tx.pending())
    S.busy=false;check(backend.recover(false) and S.files["/etc/config/ocserv"]==before)
end)
test("atomic replacement syncs the file before rename and its directory afterward",function()
    reset();local storage=require "ocserv.storage"
    storage.atomic("/etc/config/ocserv","durable candidate")
    local events=S.disk_events
    check(#events==3 and events[1]:match("^sync:/etc/config/ocserv%.easy%-") and
        events[2]=="rename:/etc/config/ocserv" and events[3]=="sync:/etc/config")
end)
test("bounded reads distinguish missing and empty files and accept the exact limit",function()
    reset();local storage=require "ocserv.storage"
    check(storage.read("/missing")==nil)
    S.files["/empty"]="";check(storage.read("/empty")=="")
    S.files["/bounded"]="1234";check(storage.read("/bounded",4)=="1234")
    expect("file_too_large",function()storage.read("/bounded",3)end)
    check(S.closed_read=="/bounded")
end)
test("unreadable config cannot be treated as an absent snapshot",function()
    reset();local before=clone(S.files);S.fail_open="/etc/config/ocserv"
    expect("read_failed",function()request(edit("FixturePassword"))end)
    check(logic.equal(before,S.files) and #S.disk_events==0)
end)
test("read errors close the file and preserve password data before any commit",function()
    reset();local revision=backend.data().revision;local before=clone(S.files);S.disk_events={}
    S.fail_read="/var/etc/ocpasswd"
    local payload=edit("FixturePassword");payload.revision=revision
    expect("read_failed",function()backend.action(payload)end)
    check(S.closed_read==S.fail_read)
    for _,event in ipairs(S.disk_events) do
        check(event:find(":/var/run/ocserv-easy",1,true),"failed read wrote persistent data")
    end
    check(S.files["/etc/config/ocserv"]==before["/etc/config/ocserv"] and
        S.files["/var/etc/ocpasswd"]==before["/var/etc/ocpasswd"])
    check(not require("luci.model.ocserv_easy.configuration").pending())
end)
test("unreadable recovery journals remain pending without touching live files",function()
    for _,failure in ipairs({"fail_open","fail_read"}) do
        reset();local tx=require "luci.model.ocserv_easy.configuration"
        local before=S.files["/etc/config/ocserv"];local state=tx.begin(false)
        tx.replace(state,"ocserv",before,before.."\ninterrupted")
        local files=clone(S.files);local writes=#S.disk_events
        S[failure]="/etc/ocserv/easy-transaction/state.json"
        expect("read_failed",function()backend.recover(false)end)
        check(tx.pending() and #S.disk_events==writes and
            S.files["/etc/config/ocserv"]==files["/etc/config/ocserv"] and
            S.files["/etc/ocserv/easy-transaction/state.json"]==files["/etc/ocserv/easy-transaction/state.json"])
        S[failure]=nil;check(backend.recover(false) and S.files["/etc/config/ocserv"]==before)
    end
end)
test("short writes reuse the original buffer and preserve all bytes",function()
    reset();local storage=require "ocserv.storage"
    local data=string.rep("abcd",16384);S.write_chunk=997
    storage.atomic("/short",data)
    check(S.files["/short"]==data and #S.write_sizes>1)
    for _,size in ipairs(S.write_sizes) do check(size==#data,"short writes copied a suffix") end
end)
test("stalled writes remove their candidate and retain the original file",function()
    reset();local storage=require "ocserv.storage"
    local before=clone(S.files);S.write_stalled=true
    expect("write_failed",function()storage.atomic("/etc/config/ocserv","replacement")end)
    check(logic.equal(S.files,before))
end)
test("directory sync failure after replacement is recovered before reporting failure",function()
    reset();local before=S.files["/etc/config/ocserv"]
    S.fail_directory_sync="/etc/config"
    expect("write_failed",function()request(edit("FixturePassword"))end)
    check(S.files["/etc/config/ocserv"]==before and not require("luci.model.ocserv_easy.configuration").pending())
end)
test("transaction completion persists removal of its recovery marker",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    tx.begin(false);S.disk_events={};tx.finish()
    check(#S.disk_events==2 and S.disk_events[1]=="remove:/etc/ocserv/easy-transaction/state.json" and
        S.disk_events[2]=="sync:/etc/ocserv/easy-transaction" and not tx.pending())
end)
test("query snapshots share live work, expire, refresh and survive corrupt caches",function()
    reset()
    local cache=require "luci.model.ocserv_easy.query_cache"
    local original_time=os.time
    local time,calls=1000,0
    os.time=function()return time end
    local function query()calls=calls+1;return {value=calls}end
    check(cache.get("test","one",5,query).value==1)
    check(cache.get("test","one",5,query).value==1 and calls==1)
    time=1005;check(cache.get("test","one",5,query).value==2)
    check(cache.get("test","two",5,query).value==3)
    check(cache.get("test","two",5,query,true).value==4)
    S.files["/var/run/ocserv-easy/query-test.json"]="invalid"
    check(cache.get("test","two",5,query).value==5)
    S.fail_write="/var/run/ocserv-easy/query-test.json"
    check(cache.get("test","three",5,query).value==6)
    cache.invalidate("test");check(not S.files["/var/run/ocserv-easy/query-test.json"])
    os.time=original_time
end)
test("cache lifetime starts after a slow query completes",function()
    reset()
    local cache=require "luci.model.ocserv_easy.query_cache"
    local original_time=os.time
    local time,calls=1000,0
    os.time=function()return time end
    local function query()calls=calls+1;time=time+4;return {value=calls}end
    check(cache.get("test","one",5,query).value==1)
    time=time+2
    check(cache.get("test","one",5,query).value==1 and calls==1)
    os.time=original_time
end)
test("failed forced refresh cannot revive a previous cached success",function()
    reset()
    local cache=require "luci.model.ocserv_easy.query_cache"
    cache.get("test","one",300,function()return {value="old"}end)
    cache.get("test","one",300,function()return {cacheable=false}end,true)
    check(cache.get("test","one",300,function()return {value="new"}end).value=="new")
end)
test("an invalidated in-flight query cannot repopulate the next snapshot",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    local peer=package.preload["luci.model.ocserv_easy.query_cache"]()
    check(cache.get("test","one",300,function()
        peer.invalidate("test")
        return {value="stale"}
    end).value=="stale")
    check(cache.get("test","one",300,function()return {value="live"}end).value=="live",
        "in-flight query revived an invalidated snapshot")
end)
test("invalidation during publication still rejects the late snapshot",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    local rename=fs.rename
    fs.rename=function(from,to)
        if to=="/var/run/ocserv-easy/query-test.json" then
            fs.rename=rename
            cache.invalidate("test")
        end
        return rename(from,to)
    end
    cache.get("test","one",300,function()return {value="old"}end)
    check(cache.get("test","one",300,function()return {value="live"}end).value=="live")
end)
test("invalidation between snapshot and marker reads rejects the old value",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    cache.get("test","one",300,function()return {value="old"}end)
    local storage=require "ocserv.storage";local read=storage.read
    storage.read=function(path,limit)
        local value=read(path,limit)
        if path=="/var/run/ocserv-easy/query-test.json" then
            storage.read=read;cache.invalidate("test")
        end
        return value
    end
    check(cache.get("test","one",300,function()return {value="live"}end).value=="live")
end)
test("failed generation writes preserve live queries and reject pending old writers",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    cache.get("test","one",300,function()
        S.fail_write="/var/run/ocserv-easy/query-test.generation"
        check(cache.get("test","one",300,function()return {value="new"}end,true).value=="new")
        return {value="old"}
    end)
    check(cache.get("test","one",300,function()return {value="live"}end).value=="live")
end)
test("missing corrupt and unreadable markers fall back to live queries",function()
    local marker="/var/run/ocserv-easy/query-test.generation"
    local cache=require "luci.model.ocserv_easy.query_cache"
    for _,failure in ipairs({"missing","corrupt","oversized","unreadable"}) do
        reset();cache.get("test","one",300,function()return {value="old"}end)
        if failure=="missing" then S.files[marker]=nil
        elseif failure=="corrupt" then S.files[marker]="invalid"
        elseif failure=="oversized" then S.files[marker]=string.rep("x",33)
        else S.fail_read=marker end
        check(cache.get("test","one",300,function()return {value="live"}end).value=="live",failure)
    end
end)
test("a late query cannot replace a completed forced refresh",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    cache.get("test","one",300,function()
        cache.get("test","one",300,function()return {value="new"}end,true)
        return {value="old"}
    end)
    check(cache.get("test","one",300,function()error("new snapshot was lost")end).value=="new",
        "late query replaced a newer result")
end)
test("a failed concurrent refresh cannot be replaced by an older success",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    cache.get("test","one",300,function()
        local ok=pcall(function()cache.get("test","one",300,function()error("offline")end,true)end)
        check(not ok)
        return {value="old"}
    end)
    check(cache.get("test","one",300,function()return {value="live"}end).value=="live")
end)
test("an unreadable disposable cache still returns the live query",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    cache.get("test","one",300,function()return {value="old"}end)
    S.fail_read="/var/run/ocserv-easy/query-test.json"
    check(cache.get("test","one",300,function()return {value="live"}end).value=="live")
    S.fail_read=nil
    check(cache.get("test","one",300,function()error("cache not replaced")end).value=="live")
end)
test("cache write failure preserves the live result without reviving stale online rows",function()
    reset();S.online={{ID=1,Username="old"}};check(backend.status().online[1].name=="old")
    S.online={{ID=2,Username="new"}}
    S.fail_write="/var/run/ocserv-easy/query-online.json"
    check(backend.status(true).online[1].name=="new")
    check(backend.status().online[1].name=="new")
end)
test("frequent online snapshots avoid repeated occtl and explicit refresh stays live",function()
    reset();backend.status();local count=#S.commands;local writes=#S.disk_events
    for _=1,100 do backend.status()end
    check(#S.commands==count and #S.disk_events==writes)
    backend.status(true);check(#S.commands==count+1)
    request(edit("FixturePassword"));count=#S.commands
    backend.status();check(#S.commands==count+1)
end)
test("independent query modules reuse 4096-session snapshots across 240 polls",function()
    reset()
    for id=1,4096 do
        S.online[id]={ID=id,Username=string.format("user-%04d",id),Groupname="staff",
            ["Remote IP"]="192.0.2.1",IPv4="10.77.0.2",["Last connected at"]="now",State="connected"}
    end
    local peers={}
    for index=1,8 do peers[index]=package.preload["luci.model.ocserv_easy.query_cache"]() end
    local sessions=require "luci.model.ocserv_easy.occtl"
    local original_time=os.time
    local json=require "luci.jsonc"
    local stringify,parse=json.stringify,json.parse
    local time,hits=1000,0
    os.time=function()return time end
    json.stringify,json.parse=TEST_JSON_ENCODE,TEST_JSON_DECODE
    local ok,err=pcall(function()
        for round=1,30 do
            time=1000+(round-1)*3
            for _,peer in ipairs(peers) do
                local commands,writes=#S.commands,#S.disk_events
                local value=peer.get("online","fixture",5,sessions.online,false,sessions.cache_limit)
                check(not value.online_error and #value.online==4096 and value.online[4096].id=="4096")
                if #S.commands==commands then
                    hits=hits+1
                    check(#S.disk_events==writes,"a cache hit wrote runtime files")
                end
            end
        end
        check(#S.commands==15 and hits==225,"independent readers did not share the completed snapshot")
        TEST_CACHE_REUSE={readers=8,rounds=30,requests=240,query_calls=#S.commands,cache_hits=hits,
            sessions=4096,cache_bytes=#S.files["/var/run/ocserv-easy/query-online.json"]}
    end)
    json.stringify,json.parse=stringify,parse
    os.time=original_time
    if not ok then error(err) end
end)
test("malformed online replies never become an empty or partial successful list",function()
    for _,rows in ipairs({{error="unavailable"}, {[2]={ID=2,Username="user"}},
        {{ID=1,Username="valid"},false}, {{ID=0,Username="user"}},
        {{ID=1.5,Username="user"}}, {{ID=math.huge,Username="user"}},
        {{ID=2147483648,Username="user"}}, {{ID=1,Username={}}}}) do
        reset();S.online=rows
        local status=backend.status()
        check(status.online_error and #status.online==0,"malformed online response was accepted")
    end
    reset();S.online_output="{}"
    check(backend.status().online_error,"empty JSON object was accepted as a session list")
end)
test("online parsing retains valid fields and bounds displayed text",function()
    reset();S.online={{ID="2147483647",Username=string.rep("u",100),Groupname="staff",
        ["Remote IP"]="192.0.2.1",IPv4="10.77.0.2",["Last connected at"]="now",State="connected"}}
    local status=backend.status();local row=status.online[1]
    check(not status.online_error and row.id=="2147483647" and #row.name==64 and row.group==nil)
    check(row.ip=="192.0.2.1" and row.vpn_ip=="10.77.0.2" and row.since=="now" and row.state=="connected")
end)
test("IPv6-only online sessions display their assigned VPN address",function()
    reset();S.online={{ID=1,Username="ipv6-user",IPv6="fd77::2",["Last connected at"]="now"}}
    check(backend.status().online[1].vpn_ip=="fd77::2")
end)
test("wire null entries cannot hide online or cached sessions",function()
    local json=require "luci.jsonc";local parse=json.parse
    for _,raw in ipairs({'[null]','[{"ID":1,"Username":"other"},null]',
        '[null,{"ID":1,"Username":"other"}]','[{"ID":1,"Username":null}]'}) do
        reset();S.online_output=raw;S.sessions_output=raw;S.fail_terminate=true
        json.parse=function(value)if value==raw then return TEST_JSON_DECODE(value)end return parse(value)end
        check(backend.status().online_error,"null online entry was lost: "..raw)
        local config,passwd=S.files["/etc/config/ocserv"],S.files["/var/etc/ocpasswd"]
        expect("terminate_failed",function()request(edit("FixturePassword"))end)
        check(S.files["/etc/config/ocserv"]==config and S.files["/var/etc/ocpasswd"]==passwd)
    end
    json.parse=parse
end)
test("quoted null and empty session arrays remain valid wire replies",function()
    reset();local json=require "luci.jsonc";local parse=json.parse
    local raw='[{"ID":1,"Username":"null","Groupname":"escaped\\\"null\\\\"}]'
    json.parse=function(value)if value==raw or value=="[]" then return TEST_JSON_DECODE(value)end return parse(value)end
    S.online_output=raw;check(backend.status(true).online[1].name=="null")
    S.online_output="[]";check(not backend.status(true).online_error)
    S.sessions_output="[]";S.fail_terminate=true
    check(request(edit("FixturePassword")).effect=="terminated")
    json.parse=parse
end)
test("failed online command cannot publish partial output as success",function()
    reset();S.online_code=124;S.online={{ID=1,Username="partial"}}
    check(backend.status().online_error)
    S.online_code=0
    check(backend.status(true).online[1].name=="partial")
end)
test("repeated refresh and invalidation leave bounded runtime files",function()
    reset();local cache=require "luci.model.ocserv_easy.query_cache"
    for _=1,100 do
        cache.get("test","one",300,function()return {value="live"}end,true)
        cache.invalidate("test")
    end
    local count=0
    for path in pairs(S.files) do
        if path:find("/var/run/ocserv-easy/",1,true) then
            check(path=="/var/run/ocserv-easy/query-test.generation" or path=="/var/run/ocserv-easy/query-test.json.lock",path)
            count=count+1
        end
    end
    check(count==2)
end)
test("the official ocserv version banner enables the page",function()
    reset();check(backend.data().supported and backend.data().version=="1.6.0")
end)
test("temporary capability query failures are not cached as an obsolete server",function()
    reset();S.capabilities_timeout=true
    check(not backend.data().capabilities.terminate)
    S.capabilities_timeout=nil
    check(backend.data().capabilities.terminate)
    check(request(edit("FixturePassword")).effect=="terminated")
end)
test("a failed version query is not misreported as an outdated server",function()
    reset();S.version_timeout=true
    check(backend.data().version=="unknown" and not backend.data().supported)
    expect("version_unavailable",function()request(edit("FixturePassword"))end)
end)
test("invalid plaintext requires an explicit password reset",function()
    reset();local c=cursor();c:set("ocserv","employee","password","test");c:set("ocserv","employee","group","");c:commit("ocserv")
    check(backend.data().users[1].needs_password)
    expect("password_required",function()request(edit())end)
    request(edit("FixturePassword"))
    check(backend.data().users[1].enabled and not backend.data().users[1].needs_password)
    check(S.files["/var/etc/ocpasswd"]=="employee01:*:"..hasher().."\n")
    check(not contains_command("restart"))
end)
test("a malformed hash can be reset without treating the old record as valid",function()
    reset();local c=cursor();c:set("ocserv","employee","password","$6$invalid");c:commit("ocserv")
    expect("password_required",function()request(edit())end)
    request(edit("FixturePassword"))
    check(backend.data().users[1].enabled and not backend.data().users[1].needs_password)
end)
test("backend default plain auth and no hash disclosure",function()
    reset();local data=backend.data();check(data.api_version==5 and data.settings_supported and data.supported and data.running and data.users[1].password==nil)
end)
test("configuration snapshots reject files changed after UCI loading or final field reads",function()
    local uci=package.loaded["luci.model.uci"]
    local original_cursor=uci.cursor
    for _,package in ipairs({"ocserv","firewall"}) do
        for _,phase in ipairs({"load","fields"}) do
            reset()
            local function change()
                local writer=cursor()
                if package=="ocserv" then writer:set(package,"config","max_clients","32")
                else writer:set(package,"ocserv_vpn_entry","dest_port","5555") end
                writer:commit(package)
            end
            uci.cursor=function()
                local c=cursor()
                if phase=="load" then change()
                else
                    local get=c.get_all
                    c.get_all=function(...)
                        local value=get(...);change();return value
                    end
                end
                return c
            end
            local ok,err=pcall(backend.data)
            uci.cursor=original_cursor
            check(not ok and type(err)=="table" and err.code=="stale_revision",
                "published old UCI fields with a new revision: "..package.."/"..phase)
            check(#S.disk_events==0 and #S.commands==0,"inconsistent snapshot performed runtime work")
            local data=backend.data()
            if package=="ocserv" then check(data.settings.max_clients=="32") end
            check(data.revision==logic.revision(S.files["/etc/config/ocserv"].."\0"..
                S.files["/etc/config/firewall"].."\0"..TEST_TEMPLATE.."\0"))
        end
    end
end)
test("snapshot verification propagates reread failures without writes or commands",function()
    local uci=package.loaded["luci.model.uci"]
    local original_cursor=uci.cursor
    for _,path in ipairs({"/etc/config/ocserv","/etc/config/firewall"}) do
        reset()
        uci.cursor=function()local c=cursor();S.fail_read=path;return c end
        local ok,err=pcall(backend.data)
        uci.cursor=original_cursor
        check(not ok and type(err)=="table" and err.code=="read_failed")
        check(#S.disk_events==0 and #S.commands==0 and S.closed_read==path)
        S.fail_read=nil
        check(backend.data().settings.max_clients=="16")
    end
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
test("a non-isolated UCI cursor is rejected before changing live configuration",function()
    reset();local raw=S.files["/etc/config/ocserv"];local native=package.loaded["uci"]
    package.loaded["uci"]={cursor=function()return cursor()end}
    expect("write_failed",function()request(edit("FixturePassword"))end)
    package.loaded["uci"]=native
    check(S.files["/etc/config/ocserv"]==raw and S.files["/var/etc/ocpasswd"]==logic.passwd(original))
    no_candidates()
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
test("unverifiable revocation replies roll back both account files",function()
    for _,rows in ipairs({{{}},{{Username=false}},{{Username=""}},
        {[2]={Username="other"}}, {error="unavailable"}}) do
        reset();S.fail_terminate=true;S.sessions=rows
        local config,passwd=S.files["/etc/config/ocserv"],S.files["/var/etc/ocpasswd"]
        expect("terminate_failed",function()request(edit("FixturePassword"))end)
        check(S.files["/etc/config/ocserv"]==config and S.files["/var/etc/ocpasswd"]==passwd)
    end
    reset();S.fail_terminate=true;S.sessions_output="{}"
    expect("terminate_failed",function()request(edit("FixturePassword"))end)
end)
test("offline account revocation tolerates unrelated valid sessions",function()
    reset();S.fail_terminate=true;S.sessions={{Username="other"}}
    check(request(edit("FixturePassword")).effect=="terminated")
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
    no_candidates()
end)
test("invalid network lists leave live files untouched and never restart services",function()
    for _,field in ipairs({"dns","routes"}) do
        reset(); local settings=fixture_settings()
        settings[field]={[2]=settings[field][1],[3]=settings[field][1]}
        local data=backend.data(); local files=clone(S.files); S.commands={}
        expect("invalid_"..field,function()
            backend.action({action="settings",settings=settings,revision=data.revision,allow_restart=true})
        end)
        for path,value in pairs(files) do
            if path:match("^/etc/") or path:match("^/var/etc/") then check(S.files[path]==value,path) end
        end
        check(S.files["/etc/ocserv/easy-transaction/state.json"]==nil and
            S.files["/etc/ocserv/easy-backup/ocserv.uci"]==nil)
        check(not contains_command("/etc/init.d/") and not contains_command("--test-config"))
    end
end)
test("duplicate-only route edits avoid configuration writes and service restarts",function()
    reset(); local settings=fixture_settings()
    request({action="settings",settings=settings,allow_restart=true})
    local data=backend.data(); local before=clone(S.files); S.disk_events={}; S.commands={}
    data.settings.routes={"192.168.19.0/24","192.168.19.0/255.255.255.0"}
    local result=backend.action({action="settings",settings=data.settings,revision=data.revision})
    check(result.effect=="unchanged" and not contains_command("/etc/init.d/"))
    for _,event in ipairs(S.disk_events) do
        check(not event:find(":/etc/",1,true) and not event:find(":/var/etc/",1,true),event)
    end
    check(S.files["/etc/config/ocserv"]==before["/etc/config/ocserv"])
end)
test("network settings apply after validation and preserve users",function()
    reset();local settings=fixture_settings();settings.port="5443"
    local result=request({action="settings",settings=settings,allow_restart=true})
    check(result.effect=="restarted" and S.candidate:find("tcp-port = 5443",1,true))
    check(backend.data().users[1].name=="employee01")
end)
test("network settings keep the managed firewall NAT and entry ports in sync",function()
    reset();local settings=fixture_settings();settings.ipaddr="10.88.0.0";settings.netmask="23";settings.port="5443";settings.udp_port="5444"
    local result=request({action="settings",settings=settings,allow_restart=true})
    check(result.effect=="restarted")
    check(S.files["/etc/config/firewall"]:find("src_ip=10.88.0.0/23",1,true))
    check(S.files["/etc/config/firewall"]:find("dest_port=5443 5444",1,true))
end)
test("unchanged service settings repair a drifted managed firewall",function()
    reset();S.firewall.ocserv_vpn_nat.src_ip="10.77.0.0/16";S.files["/etc/config/firewall"]=serialize(S.firewall)
    local result=request({action="settings",settings=backend.data().settings})
    check(result.effect=="firewall_repaired")
    check(S.files["/etc/config/firewall"]:find("src_ip=10.77.0.0/24",1,true))
end)
test("firewall repair also saves a changed public endpoint",function()
    reset();S.firewall.ocserv_vpn_nat.src_ip="10.77.0.0/16"
    local settings=backend.data().settings;settings.easy_public_url="https://new.example.com:5443"
    check(request({action="settings",settings=settings}).effect=="firewall_repaired")
    check(backend.data().settings.easy_public_url==settings.easy_public_url and not contains_command("restart"))
end)
test("unapplied firewall changes block service settings before any writes",function()
    reset();S.pending="firewall";local raw=S.files["/etc/config/ocserv"];local fw=S.files["/etc/config/firewall"]
    local settings=backend.data().settings;settings.port="5443"
    expect("pending_changes",function()request({action="settings",settings=settings,allow_restart=true})end)
    check(S.files["/etc/config/ocserv"]==raw and S.files["/etc/config/firewall"]==fw)
    check(not contains_command("restart") and not S.files["/etc/ocserv/easy-backup/ocserv.uci"])
end)
test("disabled or constrained managed rules are repaired and missing sections recreated",function()
    reset();S.firewall.ocserv_vpn_nat.enabled="0";S.firewall.ocserv_vpn_entry.src_ip="192.168.19.2"
    S.firewall.ocserv_vpn_to_lan=nil
    check(request({action="settings",settings=backend.data().settings}).effect=="firewall_repaired")
    check(not S.firewall.ocserv_vpn_nat.enabled and not S.firewall.ocserv_vpn_entry.src_ip)
    check(S.firewall.ocserv_vpn_to_lan.dest=="lan")
    check(request({action="settings",settings=backend.data().settings}).effect=="unchanged")
end)
test("all network changes retain a firewall recovery copy and roll back reload failures",function()
    reset();local raw=S.files["/etc/config/ocserv"];local fw=S.files["/etc/config/firewall"]
    S.fail_command="/etc/init.d/firewall reload"
    local settings=backend.data().settings;settings.port="5443";settings.ipaddr="10.88.0.0"
    expect("firewall_reload_failed",function()request({action="settings",settings=settings,allow_restart=true})end)
    check(S.files["/etc/ocserv/easy-backup/firewall.uci"]==fw)
    check(S.files["/etc/config/ocserv"]==raw and S.files["/etc/config/firewall"]==fw)
    check(S.firewall.ocserv_vpn_nat.src_ip=="10.77.0.0/24" and S.running)
end)
test("public endpoint changes preserve live LAN sessions and network sections",function()
    reset();local settings=backend.data().settings;local before=clone(S.db)
    settings.easy_public_url="https://[2001:db8::10]:4443"
    local result=request({action="settings",settings=settings})
    check(result.effect=="address_saved" and S.running and not contains_command("restart"))
    check(not contains_command("--test-config"),"address-only metadata does not need to reconfigure ocserv")
    check(backend.data().settings.easy_public_url==settings.easy_public_url)
    for i=2,#before do check(serialize({before[i]})==serialize({S.db[i]}),"network/account sections were rewritten") end
end)
test("unchanged service settings avoid restarts and flash writes",function()
    reset();local raw=S.files["/etc/config/ocserv"]
    local result=request({action="settings",settings=backend.data().settings})
    check(result.effect=="unchanged" and S.files["/etc/config/ocserv"]==raw)
    check(S.files["/etc/ocserv/easy-backup/ocserv.uci"]==nil and not contains_command("restart"))
end)
test("client settings persist normalized LAN addresses and verification mode without changing the VPN",function()
    reset();local before=clone(S.db)
    local result=request({action="client_profile",server="192.168.19.254:4443",cert_mode="profile"})
    check(result.effect=="profile_settings_saved" and S.running)
    local data=backend.data()
    check(data.settings.easy_public_url=="https://192.168.19.254:4443" and data.client_profile.cert_mode=="profile")
    check(not contains_command("restart") and not contains_command("--test-config"))
    for i=2,#before do check(serialize({before[i]})==serialize({S.db[i]})) end
    local raw=S.files["/etc/config/ocserv"]
    check(request({action="client_profile",server="https://192.168.19.254:4443",cert_mode="profile"}).effect=="unchanged")
    check(S.files["/etc/config/ocserv"]==raw)
    expect("invalid_cert_mode",function()request({action="client_profile",server="vpn.example.com",cert_mode="none"})end)
    expect("invalid_url",function()request({action="client_profile",server="http://vpn.example.com",cert_mode="pin"})end)
end)
test("certificate readiness permits external server certificates without a bootstrap CA",function()
    reset();check(backend.client_status().certificates_ready and backend.client_status().client_trust.profile.available)
    S.files["/etc/ocserv/ca.pem"]=nil
    check(backend.client_status().certificates_ready and not backend.client_status().client_trust.profile.available)
    S.files["/etc/ocserv/server-key.pem"]=""
    check(not backend.client_status().certificates_ready)
    S.files["/etc/ocserv/easy-install-pending"]="incomplete"
    check(backend.data().installation_pending)
end)
test("failed certificate initialization blocks starts and settings commits",function()
    reset();set_running(false);S.fail_certificates=true
    local raw=S.files["/etc/config/ocserv"]
    expect("certificate_init_failed",function()request({action="service",command="start"})end)
    expect("certificate_init_failed",function()request({action="settings",settings=fixture_settings()})end)
    check(not contains_command("/etc/init.d/ocserv start") and S.files["/etc/config/ocserv"]==raw)
end)
test("service startup success requires the daemon to be running",function()
    reset();set_running(false);S.fail_start=true
    expect("service_failed",function()request({action="service",command="start"})end)
    S.fail_start=false
    check(request({action="service",command="start"}).effect=="service_changed" and S.running)
end)
test("failed restart restores old configuration and service",function()
    reset();local raw=S.files["/etc/config/ocserv"];S.fail_restart=true
    expect("restart_failed",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
    check(S.files["/etc/config/ocserv"]==raw and S.running)
end)
test("rollback waits for asynchronous service startup",function()
    reset();local raw=S.files["/etc/config/ocserv"];S.fail_restart=true;S.restart_delay=3
    expect("restart_failed",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
    check(S.files["/etc/config/ocserv"]==raw and S.running and not S.wait_ready)
end)
test("service stopped remains stopped when settings saved",function()
    reset();set_running(false);local result=request({action="settings",settings=fixture_settings()})
    check(result.effect=="saved" and not contains_command("restart") and not S.running)
end)
test("custom extra configuration is preserved and not silently overridden",function()
    for _,directive in ipairs({"max-same-clients = 7\n", "\t dns\t= 8.8.8.8", "route=10.0.0.0/8\r\n", "auth = pam"}) do
        reset();S.files["/etc/ocserv/ocserv.conf.local"]="# retained comment\n"..directive
        expect("custom_override",function()request({action="settings",settings=fixture_settings(),allow_restart=true})end)
    end
    reset();S.files["/etc/ocserv/ocserv.conf.local"]="# dns = 8.8.8.8\nroute-add-cmd = /bin/true\n"
    local result=request({action="settings",settings=fixture_settings(),allow_restart=true})
    check(result.effect=="restarted" and S.candidate:find("route-add-cmd = /bin/true",1,true))
end)
test("downloads include public CA only and reject private-key mixtures",function()
    reset();local profile,name=backend.export("profile","https://vpn.example.com:4443")
    check(name=="Lansyra.vpn" and profile:find("Server=https://vpn.example.com:4443",1,true))
    check(not profile:find("Password",1,true) and not profile:find(old_hash,1,true))
    S.files["/etc/ocserv/ca.pem"]=S.files["/etc/ocserv/ca.pem"].."-----BEGIN PRIVATE KEY-----\n"
    expect("ca_unavailable",function()backend.export("ca","")end)
end)
test("address profiles work without a CA and normalize a LAN address",function()
    reset();S.files["/etc/ocserv/ca.pem"]=nil
    local profile,name=backend.export("address","192.168.19.253:4443")
    check(name=="Lansyra.vpn" and profile=="[VPN]\nVersion=1\nServer=https://192.168.19.253:4443\nProtocol=anyconnect\nTrust=prompt\n")
end)
test("pinned profiles support a public hostname or IPv6 without the initial CA",function()
    reset();S.files["/etc/ocserv/ca.pem"]=nil
    for _,url in ipairs({"vpn.example.com:4443","[2001:db8::1]:4443"}) do
        local profile,name=backend.export("pin",url)
        check(name=="Lansyra.vpn" and profile=="[VPN]\nVersion=1\nServer=https://"..url.."\nProtocol=anyconnect\nTrust=pin\nServerPin="..test_pin.."\n")
        check(not profile:find("CABase64",1,true) and not profile:find(old_hash,1,true))
    end
    check(contains_command("certtool --pubkey-info --load-certificate /etc/ocserv/server-cert.pem"))
end)
test("pin export uses the configured certificate and detects key rotation",function()
    reset();local c=cursor();c:set("ocserv","config","server_cert","/etc/ocserv/public-cert.pem");c:commit("ocserv")
    S.certtool_output="Public Key PIN:\n\tpin-sha256:"..TEST_BASE64(string.rep("b",32)).."\n"
    local profile=backend.export("pin","vpn.example.com:4443")
    check(profile:find(TEST_BASE64(string.rep("b",32)),1,true))
    check(contains_command("--load-certificate /etc/ocserv/public-cert.pem"))
end)
test("failed or ambiguous certificate exports never fall back to unpinned profiles",function()
    reset();S.certtool_code=1
    expect("pin_unavailable",function()backend.export("pin","vpn.example.com")end)
    S.certtool_code=0;S.certtool_output="pin-sha256:truncated="
    expect("pin_unavailable",function()backend.export("pin","vpn.example.com")end)
    reset();S.files["/etc/ocserv/ocserv.conf.local"]="server-cert = /etc/ocserv/another.pem\n"
    expect("pin_unavailable",function()backend.export("pin","vpn.example.com")end)
    reset();S.files["/etc/ocserv/ocserv.conf.local"]="[vhost:other.example.com]\n"
    expect("pin_unavailable",function()backend.export("pin","vpn.example.com")end)
end)
test("unavailable CA and pin cannot be persisted as export preferences",function()
    reset(); S.certtool_code=1; S.files["/etc/ocserv/ca.pem"]=nil
    local revision=backend.data().revision
    for mode,code in pairs({pin="pin_unavailable",profile="ca_unavailable"}) do
        expect(code,function()request({action="client_profile",server="vpn.example.test",cert_mode=mode,platform="windows"})end)
        check(backend.data().revision==revision)
    end
end)
test("profile downloads reject insecure addresses and injected lines",function()
    reset()
    for _,url in ipairs({"http://vpn.example.com","vpn.example.com\nServerPin=bad","vpn.example.com:99999","user:password@vpn.example.com"}) do
        expect("invalid_url",function()backend.export("address",url)end)
    end
    expect("bad_request",function()backend.export("private-key","")end)
end)
-- A separate local preview process may keep this isolated in-memory backend.
test("oversized candidate is rejected before changing the live UCI file",function()
    reset();local c=cursor();c:set("ocserv","config","padding","");c:commit("ocserv")
    c=cursor();c:set("ocserv","config","padding",string.rep("x",262140-#S.files["/etc/config/ocserv"]));c:commit("ocserv")
    local before=S.files["/etc/config/ocserv"]
    expect("file_too_large",function()request({action="save_user",name="newuser",group="*",password="FixturePassword",enabled=true})end)
    check(S.files["/etc/config/ocserv"]==before and backend.data().users[1].name=="employee01")
end)
test("an interrupted multi-file transaction restores both files on recovery",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before=S.files["/etc/config/ocserv"];local fw=S.files["/etc/config/firewall"]
    local state=tx.begin(true)
    tx.replace(state,"ocserv",before,before.."\ninterrupted")
    tx.replace(state,"firewall",fw,fw.."\ninterrupted")
    tx.mark(state,"firewall");tx.mark(state,"service")
    check(backend.status().recovery_pending and backend.recover(false))
    check(S.files["/etc/config/ocserv"]==before and S.files["/etc/config/firewall"]==fw)
    check(contains_command("firewall reload") and contains_command("ocserv restart") and not tx.pending())
end)
test("recovery keeps its journal when a later editor changed a committed file",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before=S.files["/etc/config/ocserv"];local state=tx.begin(false)
    tx.replace(state,"ocserv",before,before.."\ninterrupted")
    S.files["/etc/config/ocserv"]="later administrator changes"
    expect("rollback_failed",function()backend.recover(false)end)
    check(tx.pending() and S.files["/etc/config/ocserv"]=="later administrator changes")
end)
test("malformed recovery lists never discard a journal or partially restore files",function()
    local tx=require "luci.model.ocserv_easy.configuration"
    local json=require "luci.jsonc"
    local marker="/etc/ocserv/easy-transaction/state.json"
    for _,shape in ipairs({"object","mixed","hole","invalid_tail","duplicate"}) do
        reset();local before=S.files["/etc/config/ocserv"];local state=tx.begin(true)
        tx.replace(state,"ocserv",before,before.."\ninterrupted")
        local item=state.files[1]
        if shape=="object" then state.files={ocserv=item}
        elseif shape=="mixed" then state.files.extra=item
        elseif shape=="hole" then state.files={[1]=item,[3]={name="firewall",existed=true}}
        elseif shape=="invalid_tail" then state.files[2]={name="unknown",existed=true}
        else state.files[2]=clone(item) end
        S.files[marker]=json.stringify(state)
        local lease=require "luci.model.ocserv_easy.lease";lease.release(assert(lease.acquire()))
        local files=clone(S.files);local writes=#S.disk_events;local commands=#S.commands
        expect("rollback_failed",function()backend.recover(false)end)
        check(logic.equal(files,S.files) and #S.disk_events==writes and #S.commands==commands,shape)
    end
end)
test("repeated restoration skips unchanged files and retains the journal for runtime recovery",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before=S.files["/etc/config/ocserv"];local state=tx.begin(true)
    tx.replace(state,"ocserv",before,before.."\ninterrupted")
    tx.replace(state,"nikki_template",nil,"new template")
    tx.restore();local writes=#S.disk_events
    check(S.files["/etc/config/ocserv"]==before and S.files["/etc/nikki/ucode/hijack.ut"]==nil)
    tx.restore();check(#S.disk_events==writes and tx.pending())
    tx.finish();check(not tx.pending())
end)
test("wire null recovery entries retain every file and the durable journal",function()
    local tx=require "luci.model.ocserv_easy.configuration"
    local json=require "luci.jsonc";local parse=json.parse
    local marker="/etc/ocserv/easy-transaction/state.json"
    for _,field in ipairs({"files","proxy"}) do
        reset();local before=S.files["/etc/config/ocserv"];local state=tx.begin(true)
        tx.replace(state,"ocserv",before,before.."\ninterrupted")
        if field=="files" then state.files[#state.files+1]="NULL_FIXTURE" else state.proxy="NULL_FIXTURE" end
        local raw=TEST_JSON_ENCODE(state):gsub('"NULL_FIXTURE"','null')
        S.files[marker]=raw
        json.parse=function(value)if value==raw then return TEST_JSON_DECODE(value)end return parse(value)end
        local lease=require "luci.model.ocserv_easy.lease";lease.release(assert(lease.acquire()))
        local files=clone(S.files);local writes=#S.disk_events;local commands=#S.commands
        expect("rollback_failed",function()backend.recover(false)end)
        check(logic.equal(files,S.files) and #S.disk_events==writes and #S.commands==commands,field)
    end
    json.parse=parse
end)
test("boot recovery restores configuration without starting runtime services",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before=S.files["/etc/config/ocserv"];local state=tx.begin(false)
    tx.replace(state,"ocserv",before,before.."\ninterrupted");tx.mark(state,"service")
    check(backend.recover(true) and S.files["/etc/config/ocserv"]==before and not contains_command("ocserv restart"))
end)
test("runtime recovery failures retain a retryable journal",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before=S.files["/etc/config/ocserv"];local state=tx.begin(true)
    tx.replace(state,"ocserv",before,before.."\ninterrupted");tx.mark(state,"service")
    S.fail_command="/etc/init.d/ocserv restart"
    expect("rollback_failed",function()backend.recover(false)end)
    check(tx.pending() and S.files["/etc/config/ocserv"]==before and backend.recover(false) and not tx.pending())
end)
test("reboot clears volatile accounts without preventing durable rollback",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration"
    local before=S.files["/etc/config/ocserv"];local password=S.files["/var/etc/ocpasswd"]
    local state=tx.begin(true)
    tx.replace(state,"ocserv",before,before.."\ninterrupted")
    tx.replace(state,"passwd",password,"partial:*:!\n")
    for path in pairs(S.files) do if path:match("^/var/") then S.files[path]=nil end end
    check(backend.recover(true) and S.files["/etc/config/ocserv"]==before and not tx.pending())
    require("ocserv.runtime").start()
    check(S.files["/var/etc/ocpasswd"]==password and logic.standard_auth(S.files["/var/etc/ocserv.conf"]))
end)
test("live recovery retains conflicts when the runtime password file disappears",function()
    reset();local tx=require "luci.model.ocserv_easy.configuration";local state=tx.begin(true)
    tx.replace(state,"passwd",S.files["/var/etc/ocpasswd"],"partial:*:!\n")
    S.files["/var/etc/ocpasswd"]=nil
    expect("rollback_failed",function()backend.recover(false)end)
    check(tx.pending())
end)
test("runtime render and publication failures preserve both previous files",function()
    local runtime=require "ocserv.runtime"
    for _,path in ipairs({"/var/etc/ocserv-stage/check.passwd","/var/etc/ocserv-stage/check.conf",
        "/var/etc/ocpasswd","/var/etc/ocserv.conf"}) do
        reset();local config=S.files["/var/etc/ocserv.conf"];local passwd=S.files["/var/etc/ocpasswd"]
        S.fail_write=path
        expect("write_failed",runtime.start)
        check(S.files["/var/etc/ocserv.conf"]==config and S.files["/var/etc/ocpasswd"]==passwd,path)
        check(not S.files["/var/etc/ocserv-stage/check.passwd"] and not S.files["/var/etc/ocserv-stage/check.conf"])
    end
end)
test("full online snapshots fit their own cache budget including escaped display text",function()
    reset();local json=require "luci.jsonc";local stringify=json.stringify
    json.stringify=TEST_JSON_ENCODE
    local ok,err=pcall(function()
        for i=1,4096 do S.online[i]={ID=i,Username="user"..i,Groupname=string.rep('"',128),
            ["Remote IP"]=string.rep('\\',128),IPv4=string.rep('"',128),
            ["Last connected at"]=string.rep('\\',128),State=string.rep('\1',128)} end
        check(#backend.status().online==4096)
        local count=#S.commands
        check(#backend.status().online==4096 and #S.commands==count,"online cache missed")
        local raw=S.files["/var/run/ocserv-easy/query-online.json"]
        check(#raw>262144 and #raw<=require("luci.model.ocserv_easy.occtl").cache_limit)
    end)
    json.stringify=stringify
    if not ok then error(err) end
end)
test("public export capability contract rejects unsupported target policies",function()
    reset()
    local profile=backend.export("pin","vpn.example.com","ios")
    check(profile:find("Version=1\n",1,true) and profile:find("Trust=pin\n",1,true))
    expect("unsupported_client_profile",function()backend.export("profile","vpn.example.com","macos")end)
    expect("unsupported_client_profile",function()backend.export("pin","vpn.example.com","linux")end)
    request({action="client_profile",server="vpn.example.com",cert_mode="address",platform="linux"})
    check(backend.data().client_profile.platform=="linux")
end)
test("installed package version and advanced fields have one runtime contract",function()
    reset()
    S.files["/lib/apk/db/installed"]="P:unrelated\nV:1.0\n\nP:luci-app-ocserv-easy\nV:9.2.1-r3\n"
    check(backend.data().ui_version=="9.2.1-r3")
    local settings=fixture_settings()
    settings.default_domain="vpn.example.com";settings.ping_leases="1"
    request({action="settings",settings=settings,allow_restart=true})
    check(backend.data().settings.split_dns==nil and S.candidate:find("ping-leases = true",1,true))
    check(S.candidate:find("tunnel-all-dns = true",1,true))
    check(backend.export("pin","vpn.example.com","android"))
end)
test("a reachable DNS family is required before restarting",function()
    reset(); local original=backend.data(); local s=original.settings
    s.dns={"2001:db8::53"}; s.ip6addr=""
    expect("ipv4_dns_required",function()request({action="settings",settings=s,allow_restart=true})end)
    check(backend.data().revision==original.revision and not contains_command("restart"))
    s.dns[2]="1.1.1.1"; logic.validate_settings(s)
    s.dns[2]=nil; s.ip6addr="fd77::/64"; logic.validate_settings(s)
end)
test("settings, saved profiles and one-off downloads agree on endpoint conflicts",function()
    reset(); local original=backend.data(); local url="https://"..original.settings.dns[1]..":4443"
    expect("dns_is_endpoint",function()request({action="client_profile",platform="windows",server=url,cert_mode="address"})end)
    expect("dns_is_endpoint",function()backend.export("address",url,"windows")end)
    original.settings.easy_public_url=url
    expect("dns_is_endpoint",function()request({action="settings",settings=original.settings})end)
    check(backend.data().revision==original.revision)
end)
test("removed DNS settings are rejected and all clients receive tunnel DNS",function()
    reset(); local s=backend.data().settings; s.split_dns="1"; s.default_domain="example.test"
    local function select_target(target) return request({action="client_profile",platform=target,server="vpn.example.test",cert_mode="address"}) end
    select_target("android")
    expect("invalid_settings",function()request({action="settings",settings=s,allow_restart=true})end)
    select_target("windows")
    expect("invalid_settings",function()request({action="settings",settings=s,allow_restart=true})end)
    s.split_dns=nil;request({action="settings",settings=s,allow_restart=true})
    local snapshot=backend.data()
    for _,target in ipairs(snapshot.client_targets) do
        check(target.unavailable==nil)
    end
    check(backend.export("address","vpn.example.test","android"))
    expect("unsupported_dns_policy",function()logic.render(TEST_TEMPLATE,"split-dns = example.test",s,{}, {})end)
end)
test("CA distribution must validate the active server certificate",function()
    reset()
    local settings=backend.data().settings
    settings.server_cert="/etc/ocserv/external-cert.pem"
    S.files[settings.server_cert]="-----BEGIN CERTIFICATE-----\nEXTERNAL CHAIN\n-----END CERTIFICATE-----\n"
    S.ca_verify_code=1
    request({action="settings",settings=settings,allow_restart=true})
    local data=backend.client_status()
    check(not data.client_trust.profile.available and data.client_trust.profile.error=="ca_certificate_mismatch")
    expect("ca_certificate_mismatch",function()backend.export("profile","vpn.example.test","windows")end)
    expect("ca_certificate_mismatch",function()request({action="client_profile",server="vpn.example.test",platform="windows",cert_mode="profile"})end)
    check(backend.data().revision==data.revision)
    S.ca_verify_code=0;S.files["/etc/ocserv/ca.pem"]="-----BEGIN CERTIFICATE-----\nTUFUQ0hJTkc=\n-----END CERTIFICATE-----\n"
    check(backend.client_status().client_trust.profile.available)
    check(backend.export("profile","vpn.example.test","windows"))
    check(contains_command("--verify --verify-purpose 1.3.6.1.5.5.7.3.1 --load-ca-certificate /etc/ocserv/ca.pem --infile /etc/ocserv/external-cert.pem"))
end)
test("summary cache hits never decode the full online snapshot",function()
    reset()
    for id=1,4096 do S.online[id]={ID=id,Username="user-"..id} end
    backend.status()
    local storage=require "ocserv.storage";local read=storage.read
    local full_reads=0
    storage.read=function(path,limit)
        if path=="/var/run/ocserv-easy/query-online.json" then full_reads=full_reads+1 end
        return read(path,limit)
    end
    local commands,writes=#S.commands,#S.disk_events
    local ok,err=pcall(function()
        for _=1,50 do
            local summary=backend.summary()
            check(summary.online==nil and summary.online_count==4096 and not summary.online_error)
        end
        check(full_reads==0 and #S.commands==commands and #S.disk_events==writes)
        TEST_SUMMARY_REUSE={sessions=4096,requests=50,large_snapshot_reads=full_reads,
            query_calls=#S.commands-commands,writes=#S.disk_events-writes}
    end)
    storage.read=read
    if not ok then error(err) end
    local summary=backend.summary()
    summary.guard={phase="disabled",enabled=false}
    TEST_SUMMARY_REUSE.full_response_bytes=#TEST_JSON_ENCODE({ok=true,data=backend.status()})
    TEST_SUMMARY_REUSE.summary_response_bytes=#TEST_JSON_ENCODE({ok=true,data=summary})
end)
test("a cache waiter reuses the query completed by another owner",function()
    reset()
    local cache=require "luci.model.ocserv_easy.query_cache"
    local json=require "luci.jsonc"
    local path="/var/run/ocserv-easy/query-test.json"
    local marker="/var/run/ocserv-easy/query-test.generation"
    cache.get("test","identity",300,function()return {value="shared"}end)
    local published=json.parse(S.files[path])
    cache.invalidate("test")
    published.generation=S.files[marker]
    local open,poll=nixio.open,nixio.poll
    local waiting,waits,closed=true,0,false
    nixio.open=function(name,...)
        if name~=path..".lock" then return open(name,...) end
        return {lock=function(_,mode)return mode=="ulock" or not waiting end,close=function()closed=true end}
    end
    nixio.poll=function()
        waits=waits+1;waiting=false;S.files[path]=json.stringify(published)
    end
    local ok,result=pcall(function()return cache.get("test","identity",300,function()error("duplicate query")end)end)
    nixio.open,nixio.poll=open,poll
    check(ok and result.value=="shared" and waits==1 and closed)
end)
test("unchanged online contents retain their version across expiry and row ordering",function()
    reset()
    local clock=nixio.sysinfo;local time=100
    nixio.sysinfo=function()return {uptime=time}end
    S.online={{ID=2,Username="two"},{ID=1,Username="one"}}
    local first=backend.status()
    local marker=S.files["/var/run/ocserv-easy/query-online.generation"]
    time=106;S.online={{ID=1,Username="one"},{ID=2,Username="two"}}
    local second=backend.summary()
    check(first.online_version==second.online_version and second.online_count==2)
    check(marker~=S.files["/var/run/ocserv-easy/query-online.generation"])
    check(backend.status().online_version==second.online_version)
    time=112;S.online[2].Username="changed"
    check(backend.summary().online_version~=second.online_version)
    nixio.sysinfo=clock
end)
test("pin capability describes the same restrictions as the export operation",function()
    reset()
    S.files["/etc/ocserv/ocserv.conf.local"]="server-cert = /etc/ocserv/another.pem\n"
    local data=backend.client_status()
    check(data.certificates_ready and not data.client_trust.pin.available)
    check(data.client_trust.pin.error=="pin_unavailable" and data.client_trust.address.available)
    expect("pin_unavailable",function()backend.export("pin","vpn.example.test","windows")end)
end)
test("failed trust probes recover immediately and basic reads spawn no certificate probes",function()
    reset();S.certtool_code=124
    check(not backend.client_status().client_trust.pin.available)
    S.certtool_code=0
    check(backend.client_status().client_trust.pin.available)
    S.commands={};backend.data()
    check(not contains_command("certtool"))
end)
test("unsupported configuration remains readable while optional trust reports its reason",function()
    for _,entry in ipairs({{"auth","radius","plain_auth_required"},{"proxy_arp","1","proxy_arp_managed"}}) do
        reset();S.db[1][entry[1]]=entry[2]
        S.files["/etc/config/ocserv"]=serialize(S.db);S.history[S.files["/etc/config/ocserv"]]=clone(S.db)
        check(not backend.data().settings_supported)
        local status=backend.client_status()
        check(not status.client_trust.pin.available and status.client_trust.pin.error==entry[3])
        expect(entry[3],function()backend.export("pin","vpn.example.test","windows")end)
    end
end)
TEST_PREVIEW={reset=reset,backend=backend,request=request,state=function()return S end}
if not TEST_KEEP_SANDBOX then io.open=original_io_open end
return results
