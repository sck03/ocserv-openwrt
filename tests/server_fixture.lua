-- In-memory OpenWrt boundary shared by browser fixtures and backend regressions.
local logic=require "ocserv.logic"
local function check(value,message) assert(value,message or "check failed") end
local function clone(value)
    if type(value)~="table" then return value end
    local out={}; for k,v in pairs(value) do out[k]=clone(v) end; return out
end
local function fixture_settings()
    return {port="4443",udp_port="",max_clients="16",max_same="1",dpd="60",udp="1",compression="0",cisco_compat="1",predictable_ips="1",
        ipaddr="10.77.0.0",netmask="24",ip6addr="",default_domain="",easy_public_url="https://vpn.example.com:4443",dns={"10.77.0.1"},route_mode="split",routes={"192.168.19.0/24"}}
end
local old_hash="$6$existingSalt$"..string.rep("a",86)
local test_pin="pin-sha256:"..TEST_BASE64(string.rep("a",32))
local function hasher() return "$6$newSalt$"..string.rep("b",86) end
local original={{id="employee",name="employee01",group="*",password=old_hash}}
-- In-memory OpenWrt boundary. Exercises real backend transactions, not a rewrite
-- of them. External UCI, nixio, occtl and procd are the mocked interfaces.
local S
local function serialize(db)
    local lines,rows={},{}
    if #db>0 then for _,row in ipairs(db) do rows[#rows+1]=row end
    else for _,row in pairs(db) do rows[#rows+1]=row end end
    table.sort(rows,function(a,b)return tostring(a[".name"])<tostring(b[".name"])end)
    for _,row in ipairs(rows) do
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
    S={files={},dirs={},history={},commands={},disk_events={},version="1.6.0",counter=0,online={},sessions={}}
    S.db={cfg,{[".name"]="employee",[".type"]="ocservusers",name="employee01",group="*",password=old_hash},
        {[".name"]="dns1",[".type"]="dns",ip="10.77.0.1"}}
    S.firewall={
        ocserv_vpn={ [".name"]="ocserv_vpn", [".type"]="zone", name="ocvpn", device={"vpns+"}, input="ACCEPT", output="ACCEPT", forward="REJECT", mtu_fix="1" },
        ocserv_vpn_to_lan={ [".name"]="ocserv_vpn_to_lan", [".type"]="forwarding", src="ocvpn", dest="lan" },
        ocserv_vpn_nat={ [".name"]="ocserv_vpn_nat", [".type"]="nat", name="ocserv VPN source NAT", src="lan", src_ip="10.77.0.0/24", proto="all", family="ipv4", target="MASQUERADE" },
        ocserv_vpn_entry={ [".name"]="ocserv_vpn_entry", [".type"]="rule", name="Allow ocserv from LAN", src="lan", proto="tcp udp", dest_port="4443", target="ACCEPT" }
    }
    S.files["/etc/config/ocserv"]=serialize(S.db);S.history[S.files["/etc/config/ocserv"]]=clone(S.db)
    S.files["/etc/config/firewall"]=serialize(S.firewall);S.history[S.files["/etc/config/firewall"]]=clone(S.firewall)
    S.files["/etc/ocserv/ocserv.conf.template"]=TEST_TEMPLATE
    S.files["/etc/ocserv/ocserv.conf.local"]=""
    S.files["/var/etc/ocpasswd"]=logic.passwd(original)
    S.files["/var/etc/ocserv.conf"]='auth = "plain[passwd=/var/etc/ocpasswd]"\n'
    S.files["/etc/ocserv/ca.pem"]="-----BEGIN CERTIFICATE-----\nVEVTVA==\n-----END CERTIFICATE-----\n"
    S.files["/etc/ocserv/server-cert.pem"]="-----BEGIN CERTIFICATE-----\nTEST SERVER\n-----END CERTIFICATE-----\n"
    S.files["/etc/ocserv/server-key.pem"]="TEST PRIVATE KEY"
    set_running(true)
end
local original_io_open=io.open
io.open=function(path)
    if S and S.fail_open==path then return nil,"Permission denied",13 end
    if not S or S.files[path]==nil then return nil,"No such file",2 end
    return {read=function(_,limit)
        if S.fail_read==path then return nil,"Input/output error",5 end
        local data=S.files[path]:sub(1,limit);return #data>0 and data or nil
    end,close=function()S.closed_read=path end}
end
local function mode(value)
    check(type(value)=="string" and value:match("^[0-7][0-7][0-7]$"),"nixio expects octal permission digits, not decimal bit masks")
    return tonumber(value,8)
end
local fs={
    stat=function(path)return S.dirs[path] or S.files[path] and {size=#S.files[path]} end,
    access=function(path)return S.dirs[path]~=nil or S.files[path]~=nil end,
    readlink=function(path)if S.running and path=="/proc/101/exe"then return "/usr/sbin/ocserv"end end,
    mkdir=function(path,value)S.dirs[path]={mode=mode(value)};return true end,
    chmod=function(path,value)mode(value);return true end,
    remove=function(path)
        if S.fail_remove==path then S.fail_remove=nil;return nil end
        S.files[path]=nil;S.disk_events[#S.disk_events+1]="remove:"..path;return true
    end,
    rename=function(from,to)
        if S.fail_write==to then S.fail_write=nil;return nil end
        S.disk_events[#S.disk_events+1]="rename:"..to
        S.files[to]=S.files[from];S.files[from]=nil
        if S.history[S.files[to]] then
            if to=="/etc/config/ocserv" then S.db=clone(S.history[S.files[to]]) end
            if to=="/etc/config/firewall" then S.firewall=clone(S.history[S.files[to]]) end
        end
        return true
    end
}
local nixio={bin={},getpid=function()return 42 end,open_flags=function()return 1 end,poll=function()
    if S and S.wait_ready then S.wait_ready=S.wait_ready-1; if S.wait_ready==0 then S.wait_ready=nil;set_running(true) end end
end}
nixio.bin.hexlify=function(value)return (value:gsub(".",function(c)return string.format("%02x",c:byte())end)) end
nixio.bin.b64encode=TEST_BASE64
nixio.crypt=function()if S.fail_hash then return "*0" end return hasher()end
nixio.open=function(path,flags,permissions)
    if path=="/dev/urandom" then return {read=function(_,count)S.counter=S.counter+1;return string.rep(string.char(S.counter%255),count)end,close=function()end} end
    if flags=="r" then return {sync=function()
        S.disk_events[#S.disk_events+1]="sync:"..path
        if S.fail_directory_sync==path then S.fail_directory_sync=nil;return false end
        return true
    end,close=function()end} end
    mode(permissions)
    S.files[path]=""
    return {write=function(_,data,offset)
        S.write_sizes=S.write_sizes or {};S.write_sizes[#S.write_sizes+1]=#data
        if S.write_stalled then return 0 end
        local part=data:sub((offset or 0)+1,(offset or 0)+(S.write_chunk or #data))
        S.files[path]=S.files[path]..part;return #part
    end,sync=function()S.disk_events[#S.disk_events+1]="sync:"..path;return true end,close=function()end,lock=function()return not S.busy end}
end
local function cursor(directory,savedir)
    directory=directory or "/etc/config"
    local db=clone(directory=="/etc/config" and S.db or S.history[S.files[directory.."/ocserv"]] or S.db)
    local fw=clone(directory=="/etc/config" and S.firewall or S.history[S.files[directory.."/firewall"]] or S.firewall)
    local function find(id)for _,row in ipairs(db)do if row[".name"]==id then return row end end end
    local function find_fw(id) return fw[id] end
    return {
        get_confdir=function()return directory end,get_savedir=function()return savedir end,
        load=function(_,package)return directory=="/etc/config" or S.fail_load~=package end,unload=function()end,
        get=function(_,package,id,key)local row=package=="firewall" and find_fw(id) or find(id);return row and row[key or ".type"]end,
        get_all=function(_,package,id)return clone(package=="firewall" and find_fw(id) or find(id))end,
        get_first=function()return "lan"end,
        foreach=function(_,package,kind,fn)if package=="ocserv"then for _,row in ipairs(db)do if row[".type"]==kind then fn(clone(row))end end end end,
        changes=function(_,package)return (S.pending==true or S.pending==package) and {[package]={"pending"}} or {}end,
        add=function(_,package,kind)local id="added"..tostring(#db+1);if package=="firewall" then id="added-fw"..tostring(#db+1);fw[id]={[".name"]=id,[".type"]=kind};return id end;db[#db+1]={[".name"]=id,[".type"]=kind};return id end,
        set=function(_,package,id,key,value)
            if package=="firewall" and value==nil then fw[id]={[".name"]=id,[".type"]=key};return true end
            local row=package=="firewall" and find_fw(id) or find(id);if not row then return nil end;row[key]=value;return true
        end,
        delete=function(_,package,id,key)
            local row=package=="firewall" and find_fw(id) or find(id)
            if key then if not row or row[key]==nil then return false end;row[key]=nil;return true end
            if package=="firewall" then fw[id]=nil;return true end
            for i,item in ipairs(db)do if item[".name"]==id then table.remove(db,i);return true end end
        end,
        revert=function()db=clone(S.db);fw=clone(S.firewall);return true end,
        commit=function(_,package)
            if S.fail_commit then return nil end
            if package=="firewall" then local raw=serialize(fw);S.history[raw]=clone(fw);S.files[directory.."/firewall"]=raw;if directory=="/etc/config" then S.firewall=clone(fw) end;return true end
            local raw=serialize(db);S.history[raw]=clone(db);S.files[directory.."/ocserv"]=raw;if directory=="/etc/config" then S.db=clone(db) end;return true
        end
    }
end
local function run(argv,timeout,consume)
    local command=table.concat(argv," ");S.commands[#S.commands+1]=command
    if command==S.fail_command then S.fail_command=nil;return 1,"fixture failure" end
    if argv[1]=="/usr/bin/certtool" then
        if argv[2]=="--verify" then return S.ca_verify_code or S.certtool_code or 0,"fixture verification" end
        return S.certtool_code or 0,S.certtool_output or ("Public Key PIN:\n\t"..test_pin.."\n")
    end
    if command=="/usr/sbin/ocserv --version"then
        if S.version_timeout then return 124,"" end
        return 0,"OpenConnect VPN Server "..S.version.."\n"
    end
    if command:find("--test-config",1,true)then S.candidate=S.files[argv[4]];return S.bad_config and 1 or 0,""end
    if command:find("occtl --help",1,true)or command:find("occtl help",1,true)then
        if S.capabilities_timeout then return 124,"" end
        return 0,"terminate user\nshow sessions all"
    end
    if command:find("occtl -j show",1,true) then
        local online=argv[4]=="users"
        local rows=online and S.online or S.sessions
        local raw=(online and S.online_output or S.sessions_output) or (next(rows) and TEST_JSON_ENCODE(rows) or "[]")
        local code=online and (S.online_code or 0) or (S.bad_sessions and 1 or 0)
        if not consume then return code,raw end
        for at=1,#raw,4096 do if consume(raw:sub(at,at+4095))==false then return 125,"" end end
        return code,""
    end
    if command:find("occtl terminate user",1,true)then return S.fail_terminate and 1 or 0,""end
    if command:find("occtl disconnect id",1,true)then return 0,"disconnected"end
    if command=="/etc/init.d/ocserv enabled"then return 0,""end
    if command=="/etc/init.d/ocserv initcerts"then return S.fail_certificates and 1 or 0,""end
    if command=="/etc/init.d/ocserv restart"then
        if S.fail_restart then S.fail_restart=nil;set_running(false);return 1,""end
        if S.restart_delay then set_running(false);S.wait_ready=S.restart_delay;return 0,""end
        set_running(true);return 0,""
    end
    if command=="/etc/init.d/ocserv stop"then set_running(false);return 0,""end
    if command=="/etc/init.d/ocserv start"then if not S.fail_start then set_running(true)end;return 0,""end
    return 0,""
end
package.loaded["nixio"]=nixio;package.loaded["nixio.fs"]=fs
-- Match OpenWrt 25.12: LuCI's rpcd cursor ignores custom directories.
package.loaded["luci.model.uci"]={cursor=function()return cursor()end}
package.loaded["uci"]={cursor=cursor}
package.loaded["ocserv.process"]={run=run}
package.loaded["luci.jsonc"]={
    stringify=function(value) S.json=S.json or {};local key="JSON"..tostring(#S.json+1);S.json[#S.json+1]=clone(value);return key end,
    parse=function(value)
        local index=type(value)=="string" and tonumber(value:match("^JSON(%d+)$"))
        if index then return S.json and clone(S.json[index]) end
        local ok,decoded=pcall(TEST_JSON_DECODE,value)
        return ok and decoded or nil
    end
}
local backend=require "luci.model.ocserv_easy.backend"
local function request(payload)payload.revision=payload.revision or backend.data().revision;return backend.action(payload)end
local function edit(password,enabled)return {action="save_user",id="employee",name="employee01",group="*",password=password or "",enabled=enabled~=false}end
local function contains_command(part)for _,cmd in ipairs(S.commands)do if cmd:find(part,1,true)then return true end end return false end
local function no_candidates()
    for path in pairs(S.files) do
        check(not path:match("^/var/run/ocserv%-easy/config%-stage/") and
            not path:match("^/var/run/ocserv%-easy/check%."),"temporary candidate retained: "..path)
    end
end
return {
    reset=reset,backend=backend,request=request,state=function()return S end,
    clone=clone,settings=fixture_settings,old_hash=old_hash,test_pin=test_pin,hasher=hasher,original=original,
    fs=fs,nixio=nixio,cursor=cursor,run=run,serialize=serialize,set_running=set_running,
    edit=edit,contains_command=contains_command,no_candidates=no_candidates,original_io_open=original_io_open
}
