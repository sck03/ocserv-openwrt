local fixture=require "test.guard_fixture"
local results,S={},nil
local guard,root,admin=fixture.guard,fixture.root,fixture.admin
local clone,equal,check=fixture.clone,fixture.equal,fixture.check
local encode,decode,row=fixture.encode,fixture.decode,fixture.row
local cursor,fs,nixio,run=fixture.cursor,fixture.fs,fixture.nixio,fixture.run
local enable,disable,use_proxy=fixture.enable,fixture.disable,fixture.use_proxy
local function reset()fixture.reset();S=fixture.state()end
local function test(name,fn)
    local ok,err=pcall(fn)
    assert(ok,name..": "..(type(err)=="table" and tostring(err.code) or tostring(err)))
    results[#results+1]=name
end
local function expect(code,fn) local ok,err=pcall(fn); check(not ok and type(err)=="table" and err.code==code,"expected "..code) end
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
test("unchanged active polling only parses the small projection",function()
    reset();enable()
    guard.status(admin,false)
    local json=require "luci.jsonc";local parse=json.parse;local full=0
    json.parse=function(raw)
        if raw==S.files[root.."state.json"] then full=full+1 end
        return parse(raw)
    end
    for _=1,20 do check(guard.status(admin,false).phase=="enabled") end
    json.parse=parse
    check(full==0,"polling decoded the recovery journal")
    S.files[root.."state.json"]="invalid"
    check(guard.status(admin,false).phase=="recovery_failed")
end)
test("default is off and detection writes no router configuration",function()
    reset(); local before=clone(S.cfg); check(guard.status(admin).phase=="disabled" and guard.status(admin).available); check(equal(before,S.cfg) and not S.nft)
end)
test("OpenClash core alone cannot complete activation before DNS rules exist",function()
    reset(); S.proxy_dns_missing=true; local before=clone(S.cfg)
    guard.begin("enable",admin); guard.work(false)
    check(equal(before,S.cfg) and not S.nft and guard.status(admin).last.failure=="guard_proxy_failed")
end)
test("OpenClash DNS rules cannot complete activation before the core binds its sockets",function()
    reset(); S.files["/proc/net/udp"]=nil; local before=clone(S.cfg)
    guard.begin("enable",admin); guard.work(false)
    check(equal(before,S.cfg) and not S.nft and guard.status(admin).last.failure=="guard_proxy_failed")
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
    local state=decode(S.files[root.."state.json"]); state.phase="pending"; state.token="fixture-token"; S.files[root.."state.json"]=encode(state)
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
    local state=decode(S.files[root.."state.json"]); state.phase="applying"; state.token="fixture-token"; S.files[root.."state.json"]=encode(state)
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
test("malformed guard lists cannot silently skip isolation restoration",function()
    for _,field in ipairs({"ops","files"}) do
        for _,shape in ipairs({"object","mixed","hole","invalid_tail","duplicate"}) do
            reset();enable();local state=decode(S.files[root.."state.json"])
            local item=state[field][1]
            if shape=="object" then state[field]={entry=item}
            elseif shape=="mixed" then state[field].extra=item
            elseif shape=="hole" then state[field][1]=nil
            elseif shape=="invalid_tail" then state[field][#state[field]+1]={}
            else state[field][#state[field]+1]=clone(item) end
            S.files[root.."state.json"]=encode(state)
            local files=clone(S.files);local cfg=clone(S.cfg);local commands=#S.commands
            check(guard.status(admin,false).phase=="recovery_failed",field..":"..shape)
            expect("guard_state_invalid",function()guard.begin("disable",admin)end)
            expect("guard_state_invalid",function()guard.work(true)end)
            check(equal(files,S.files) and equal(cfg,S.cfg) and #S.commands==commands and S.nft)
        end
    end
end)
test("incomplete guard metadata is reported without breaking status or losing recovery",function()
    for _,field in ipairs({"pool","prefix","lan","admin","deadline","was_running","started","before"}) do
        reset();enable();local state=decode(S.files[root.."state.json"])
        if field=="pool" or field=="prefix" or field=="lan" or field=="admin" then state.info[field]=nil
        elseif field=="before" then state.before.ocserv=nil
        else state[field]=nil end
        S.files[root.."state.json"]=encode(state)
        local files=clone(S.files);local cfg=clone(S.cfg)
        local status=guard.status(admin,false)
        check(status.phase=="recovery_failed" and status.reason=="guard_state_invalid",field)
        expect("guard_state_invalid",function()guard.begin("disable",admin)end)
        check(equal(files,S.files) and equal(cfg,S.cfg) and S.nft)
    end
end)
test("wire null guard values cannot truncate recovery or discard UCI options",function()
    local json=require "luci.jsonc";local parse=json.parse
    for _,field in ipairs({"ops","files","option","list"}) do
        reset();enable();local state=decode(S.files[root.."state.json"])
        if field=="ops" or field=="files" then state[field][#state[field]+1]="NULL_FIXTURE"
        else
            local op
            for _,item in ipairs(state.ops) do if item.key then op=item;break end end
            check(op)
            op.before=field=="list" and {"lan","NULL_FIXTURE"} or "NULL_FIXTURE"
        end
        local raw=TEST_JSON_ENCODE(state):gsub('"NULL_FIXTURE"','null')
        S.files[root.."state.json"]=raw
        json.parse=function(value)if value==raw then return TEST_JSON_DECODE(value)end return parse(value)end
        local files=clone(S.files);local cfg=clone(S.cfg);local commands=#S.commands
        local status=guard.status(admin,false)
        check(status.phase=="recovery_failed" and status.reason=="guard_state_invalid",field)
        expect("guard_state_invalid",function()guard.begin("disable",admin)end)
        expect("guard_state_invalid",function()guard.work(true)end)
        check(equal(files,S.files) and equal(cfg,S.cfg) and #S.commands==commands and S.nft)
    end
    json.parse=parse
end)
test("quoted null in guard snapshots survives wire decoding and restoration",function()
    reset();S.files["/etc/ocserv/ocserv.conf.local"]='# annulled "null" \\ path\n'
    local before=S.files["/etc/ocserv/ocserv.conf.local"]
    enable();local state=decode(S.files[root.."state.json"])
    local raw=TEST_JSON_ENCODE(state)
    S.files[root.."state.json"]=raw
    local json=require "luci.jsonc";local parse=json.parse
    json.parse=function(value)if value==raw then return TEST_JSON_DECODE(value)end return parse(value)end
    check(guard.status(admin,false).phase=="enabled")
    disable();check(S.files["/etc/ocserv/ocserv.conf.local"]==before)
    json.parse=parse
end)
test("invalid guard targets and UCI values are rejected before any recovery side effect",function()
    local corruptions={
        function(s)s.ops[1].package="network"end,
        function(s)s.ops[1].id=1 end,
        function(s)s.ops[1].key={}end,
        function(s)s.ops[1].before=nil end,
        function(s)s.ops[1].after={named="not a UCI list"}end,
        function(s)s.files[1].path="/etc/unrelated"end,
        function(s)s.files[1].before=nil end,
        function(s)s.files[1].after=false end,
        function(s)s.before.unrelated="unexpected snapshot"end,
        function(s)s.phase="restoring";s.started=nil end,
        function(s)s.deadline=math.huge end,
        function(s)s.ops={}end
    }
    for _,corrupt in ipairs(corruptions) do
        reset();enable();local state=decode(S.files[root.."state.json"]);corrupt(state)
        S.files[root.."state.json"]=encode(state)
        local files=clone(S.files);local cfg=clone(S.cfg);local commands=#S.commands
        check(guard.status(admin,false).phase=="recovery_failed")
        expect("guard_state_invalid",function()guard.work(true)end)
        check(equal(files,S.files) and equal(cfg,S.cfg) and #S.commands==commands and S.nft)
    end
end)
test("obsolete guard schemas are retained for inspection without implicit migration",function()
    reset();enable();local state=decode(S.files[root.."state.json"]);state.version=2
    S.files[root.."state.json"]=encode(state)
    local files=clone(S.files);local cfg=clone(S.cfg)
    expect("guard_state_invalid",function()guard.begin("disable",admin)end)
    check(equal(files,S.files) and equal(cfg,S.cfg) and S.nft)
end)
test("a queued cancellation records that application never started",function()
    reset();local cfg=clone(S.cfg);guard.begin("enable",admin)
    check(decode(S.files[root.."state.json"]).started==false)
    guard.begin("disable",admin);guard.work(true)
    check(equal(cfg,S.cfg) and not S.nft and not guard.active())
end)
test("unreadable guard journals cannot disable isolation or start another operation",function()
    for _,failure in ipairs({"fail_open","fail_read"}) do
        reset();enable();local files=clone(S.files);local cfg=clone(S.cfg)
        S[failure]=root.."state.json"
        local status=guard.status(admin,false)
        check(status.phase=="recovery_failed" and status.enabled)
        expect("read_failed",function()guard.begin("enable",admin)end)
        check(equal(files,S.files) and equal(cfg,S.cfg) and S.nft)
        S[failure]=nil;disable()
    end
end)
test("empty guard journals are invalid rather than an absent transaction",function()
    reset();S.files[root.."state.json"]=""
    check(guard.status(admin,false).phase=="recovery_failed")
    expect("guard_state_invalid",function()guard.begin("enable",admin)end)
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
test("unreadable logs are reported and never overwritten by later events",function()
    for _,failure in ipairs({"fail_open","fail_read"}) do
        reset();log.add("apply","start")
        local path="/var/run/ocserv-easy/guard-log.json"
        local saved=S.files[path];S[failure]=path
        expect("read_failed",function()log.read()end)
        log.add("restore","completed");check(S.files[path]==saved)
        S[failure]=nil;check(#log.read().entries==1)
    end
end)
test("failed log replacement retains history and removes its temporary file",function()
    reset();log.add("apply","start")
    local path="/var/run/ocserv-easy/guard-log.json"
    local saved=S.files[path];S.fail_write=path
    log.add("restore","failed");check(S.files[path]==saved)
    for name in pairs(S.files) do check(name==path or name:sub(1,#path)~=path,"stranded log temporary") end
    log.add("restore","completed");check(#log.read().entries==2)
end)
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
        reset(); use_proxy(name); S.proxy_running="openclash"
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
        local state=decode(S.files[root.."state.json"]); state.phase="pending"; state.token="fixture-token"; S.files[root.."state.json"]=encode(state)
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
for _,name in ipairs({"passwall","passwall2","homeproxy","shadowsocksr"}) do
    test(name.." is detected without borrowing another adapter or changing UCI",function()
        reset(); S.cfg.openclash.config.enable="0"; S.files["/etc/init.d/"..name]="executable"; S.proxy_running=name
        local before=clone(S.cfg)
        expect("guard_proxy_unsupported",function()guard.begin("enable",admin)end)
        check(equal(before,S.cfg) and not guard.active())
        S.cfg.openclash.config.enable="1"
        expect("guard_proxy_conflict",function()guard.begin("enable",admin)end)
        S.proxy_running="openclash";check(guard.status(admin).available)
    end)
end
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
    test(name.." normal integration bootstraps before a disabled proxy has nft rules",function()
        normal_proxy(name);S.cfg[name].proxy.enabled="0";S.proxy_table_absent=true
        S.files["/etc/config/"..name]=encode(S.cfg[name])
        check(connect().effect=="proxy_connected")
        check(S.cfg[name].proxy.enabled=="1" and not S.proxy_table_absent)
    end)
    test(name.." failed preparation releases candidate files without changing the proxy",function()
        for _,target in ipairs({"/etc/ocserv/easy-backup/ocserv.uci",
            "/var/run/ocserv-easy/config-stage/"..name,"/etc/ocserv/easy-transaction/state.json"}) do
            normal_proxy(name);local before=clone(S.cfg)
            S.fail_write=target
            expect("write_failed",connect)
            check(equal(before,S.cfg) and not require("luci.model.ocserv_easy.configuration").pending())
            for path in pairs(S.files) do
                check(not path:match("^/var/run/ocserv%-easy/config%-stage/"),path)
            end
            for _,cmd in ipairs(S.commands) do check(cmd~="/etc/init.d/"..name.." restart") end
            check(connect().effect=="proxy_connected")
        end
    end)
    test(name.." repeated startup events wait for rule generation without restarting again",function()
        normal_proxy(name);connect();S.proxy_table_absent=true
        local count=#S.commands;local writes=S.flash_writes
        for _=1,5 do check(connect()==false) end
        for i=count+1,#S.commands do check(S.commands[i]~="/etc/init.d/"..name.." restart") end
        check(S.flash_writes==writes)
        S.proxy_table_absent=false;check(connect().effect=="unchanged")
    end)
    test(name.." ingress set alone cannot complete sync before DNS rules exist",function()
        normal_proxy(name);connect();S.proxy_dns_missing=true
        local count=#S.commands;local writes=S.flash_writes
        check(connect()==false)
        check(S.flash_writes==writes)
        for i=count+1,#S.commands do check(S.commands[i]~="/etc/init.d/"..name.." restart") end
        S.proxy_dns_missing=false;check(connect().effect=="unchanged")
    end)
    test(name.." enabled proxy still starting is retried instead of silently skipped",function()
        normal_proxy(name);S.proxy_running=nil;local before=clone(S.cfg)
        check(connect()==false and equal(S.cfg,before))
        S.proxy_running=name;check(connect().effect=="proxy_connected")
    end)
    test(name.." custom VPN pool is connected and unchanged events avoid staging writes",function()
        normal_proxy(name);S.cfg.ocserv.config.ipaddr="192.168.100.0"
        S.files["/etc/config/ocserv"]=encode(S.cfg.ocserv)
        connect();check(equal(S.cfg[name].lansyra_vpn.ip,{"192.168.100.0/24"}))
        local writes=S.stage_writes
        for _=1,10 do check(connect().effect=="unchanged") end
        check(S.stage_writes==writes,"unchanged event rewrote private UCI candidates")
    end)
    test(name.." on-demand status reports missing ingress without changing configuration",function()
        normal_proxy(name);local before=clone(S.cfg)
        local status=backend.proxy_status()
        check(status.proxy==name and status.running and not status.ingress_ready)
        check(equal(before,S.cfg) and not S.stage_writes and not status.subscription)
        connect();check(backend.proxy_status().ingress_ready)
    end)
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
test("preflight write failures clean password and configuration candidates",function()
    for _,path in ipairs({"/var/run/ocserv-easy/guard-check.passwd","/var/run/ocserv-easy/guard-check.conf"}) do
        reset();guard.begin("enable",admin);S.fail_write=path;guard.work(false)
        check(guard.status(admin).phase=="disabled")
        check(not S.files["/var/run/ocserv-easy/guard-check.passwd"] and not S.files["/var/run/ocserv-easy/guard-check.conf"])
    end
end)
TEST_GUARD_PREVIEW={reset=reset,guard=guard,use_proxy=use_proxy,state=function()return S end}
return results
