-- SPDX-License-Identifier: GPL-3.0-or-later
-- Proxy-specific configuration; the guard owns persistence and rollback.
local fs=require "nixio.fs"
local json=require "luci.jsonc"
local logic=require "luci.model.ocserv_easy.logic"
local run=require("luci.model.ocserv_easy.process").run
local registry=require "luci.model.ocserv_easy.proxy_registry"
local M={}
M.names=registry.names
function M.supports(name) local entry=registry.get(name); return entry and entry.adapter~=nil or false end
function M.running(name)
    local entry=logic.require(registry.get(name),"guard_state_invalid")
    -- OpenClash and Nikki share Mihomo; Momo and HomeProxy share sing-box.
    -- Only a running instance owned by this procd service counts.
    local code,out=run({"/bin/ubus","call","service","list",json.stringify({name=name})},3)
    local data=code==0 and json.parse(out)
    local instances=type(data)=="table" and type(data[name])=="table" and data[name].instances
    local instance=type(instances)=="table" and instances[name]
    if entry.adapter then return type(instance)=="table" and instance.running==true end
    for _,owned in pairs(type(instances)=="table" and instances or {}) do
        if type(owned)=="table" and owned.running==true then return true end
    end
    return false
end
function M.select(c)
    local selected
    for _,name in ipairs(M.names()) do
        local entry=registry.get(name)
        local enabled=entry.option and c:get(name,"config",entry.option)=="1"
        if enabled or not entry.adapter and fs.access("/etc/init.d/"..name) and M.running(name) then
            logic.require(not selected,"guard_proxy_conflict")
            selected=name
        end
    end
    return selected
end
function M.detect(c)
    local selected=M.select(c)
    logic.require(not selected or M.supports(selected),"guard_proxy_unsupported")
    logic.require(selected and fs.access("/etc/init.d/"..selected) and M.running(selected),"guard_requires_proxy")
    if selected~="openclash" then
        logic.require(c:get(selected,"config","core_only")~="1" and c:get(selected,"proxy"),"guard_proxy_mode")
        local tcp,udp=c:get(selected,"proxy","tcp_mode"),c:get(selected,"proxy","udp_mode")
        logic.require((tcp=="redirect" or tcp=="tproxy" or tcp=="tun") and (udp=="tproxy" or udp=="tun"),"guard_proxy_mode")
    end
    return selected
end
local function ingress_ready(name)
    if name=="openclash" then return true end
    -- procd can be running even if template generation or TUN startup failed.
    local code,out=run({"/usr/sbin/nft","list","set","inet",name,"lan_inbound_device"},3)
    return code==0 and (out or ""):find('"vpns*"',1,true)~=nil
end
local function target_ports(data,kind)
    local ports,seen={},{}
    for _,item in ipairs(type(data)=="table" and type(data.nftables)=="table" and data.nftables or {}) do
        local rule=type(item)=="table" and item.rule
        for _,expr in ipairs(type(rule)=="table" and type(rule.expr)=="table" and rule.expr or {}) do
            local value=type(expr)=="table" and expr[kind]
            local raw=type(value)=="table" and value.port
            local port=(type(raw)=="number" or type(raw)=="string") and tonumber(raw)
            if port and port>=1 and port<=65535 and port%1==0 and not seen[port] then
                seen[port]=true; ports[#ports+1]=port
            end
        end
    end
    table.sort(ports)
    return ports
end
local function listeners()
    local read=require("luci.model.ocserv_easy.storage").read
    local sockets={}
    for _,protocol in ipairs({"tcp","udp"}) do
        local known=true
        sockets[protocol]={}
        for _,suffix in ipairs({"","6"}) do
            local ok,raw=pcall(read,"/proc/net/"..protocol..suffix)
            if ok and raw then
                for line in raw:gmatch("[^\n]+") do
                    local port,state=line:match("^%s*%d+:%s+%x+:(%x+)%s+%x+:%x+%s+(%x+)")
                    if port and state==(protocol=="tcp" and "0A" or "07") then sockets[protocol][tonumber(port,16)]=true end
                end
            else known=false end
        end
        sockets[protocol].known=known
    end
    return sockets
end
function M.ready(name,managed)
    if not M.running(name) then return false end
    if not managed then return true end
    if name~="openclash" and not ingress_ready(name) then return false end
    -- The interface set can exist before DNS rules finish generating.
    -- OpenClash also starts its core before installing firewall DNS hijacking.
    local table_name=name=="openclash" and "fw4" or name
    local chain=name=="openclash" and "openclash_dns_redirect" or "lan_dns_hijack"
    local code,out=run({"/usr/sbin/nft","-j","list","chain","inet",table_name,chain},3)
    local data=code==0 and json.parse(out)
    local ports=target_ports(data,"redirect")
    if #ports==0 then return false end
    local sockets=listeners()
    for _,port in ipairs(ports) do
        if not sockets.tcp[port] or not sockets.udp[port] then return false end
    end
    return true
end
-- On-demand diagnostics only: use generated nft targets, never mixed-port,
-- subscription contents or defaults that would become stale after a port edit.
function M.diagnostics(c,name)
    local entry=logic.require(registry.get(name),"guard_state_invalid")
    local running=M.running(name)
    local result={running=running,label=entry.label,supported=M.supports(name),targets={}}
    if not result.supported then return result end
    result.ingress_ready=running and ingress_ready(name)
    if name=="openclash" then return result end
    result.tcp_mode=c:get(name,"proxy","tcp_mode")
    result.udp_mode=c:get(name,"proxy","udp_mode")
    -- A stopped service cannot own the other provider's TUN in a shared table.
    if not running then return result end
    local sockets=listeners()
    local function targets(chain,kind)
        local code,out=run({"/usr/sbin/nft","-j","list","chain","inet",name,chain},3)
        local data=code==0 and json.parse(out)
        local target={available=type(data)=="table" and type(data.nftables)=="table",ports={}}
        for _,port in ipairs(target_ports(data,kind)) do
            local entry={port=port}
            for _,protocol in ipairs({"tcp","udp"}) do
                if sockets[protocol][port] then entry[protocol]=true
                elseif sockets[protocol].known then entry[protocol]=false end
            end
            target.ports[#target.ports+1]=entry
        end
        result.targets[chain]=target
    end
    targets("lan_dns_hijack","redirect")
    if result.tcp_mode=="redirect" then targets("lan_redirect","redirect") end
    if result.tcp_mode=="tproxy" or result.udp_mode=="tproxy" then targets("lan_tproxy","tproxy") end
    if result.tcp_mode=="tun" or result.udp_mode=="tun" then
        -- Both upstream init scripts use table 81 when the option is absent.
        local routing_table=tonumber(c:get(name,"routing","tun_route_table") or "81")
        result.tun={}
        if routing_table and routing_table>=1 and routing_table<=4294967295 and routing_table%1==0 then
            result.tun.table=routing_table
            local code,out=run({"/sbin/ip","-j","-4","route","show","table",tostring(routing_table)},3)
            local routes=code==0 and json.parse(out)
            for _,route in ipairs(type(routes)=="table" and routes or {}) do
                if type(route)=="table" and route.dst=="default" and type(route.dev)=="string" then
                    result.tun.device=route.dev
                    break
                end
            end
            code,out=run({"/sbin/ip","-j","-4","rule","show"},3)
            local rules=code==0 and json.parse(out)
            if type(rules)=="table" then result.tun.rule=false end
            for _,rule in ipairs(type(rules)=="table" and rules or {}) do
                if type(rule)=="table" and tonumber(rule.table)==routing_table and rule.fwmark~=nil then result.tun.rule=true end
            end
        end
    end
    return result
end
function M.ingress(before)
    logic.require(type(before)=="string","guard_proxy_template")
    local _,anchors=before:gsub("const lan_inbound_device = ","")
    logic.require(anchors==1,"guard_proxy_template")
    local seed='const lan_inbound_device = ["vpns*"]; /* Lansyra VPN ingress */'
    if before:find(seed,1,true) then return before end
    local after,count=before:gsub("const lan_inbound_device = %[%];",seed)
    logic.require(count==1,"guard_proxy_template")
    return after
end
function M.connect(c,name,pool,ipv6,dry_run)
    -- Put the VPN rule first, so a LAN-wide catch-all ACL cannot bypass it.
    -- Other ACLs, LAN interfaces, router traffic and subscription rules stay intact.
    local id="lansyra_vpn"
    local existing=c:get_all(name,id)
    logic.require(not existing or existing[".type"]=="lan_access_control","guard_section_conflict")
    local changed=false
    local function set(section,key,value)
        if not logic.equal(c:get(name,section,key) or false,value or false) then
            if not dry_run then
                if value then logic.require(c:set(name,section,key,value),"write_failed")
                else logic.require(c:delete(name,section,key),"write_failed") end
            end
            changed=true
        end
    end
    if not existing then
        if not dry_run then logic.require(c:set(name,id,"lan_access_control"),"write_failed") end
        changed=true
    end
    for _,key in ipairs({"enabled","lan_proxy","ipv4_proxy","ipv4_dns_hijack"}) do set("proxy",key,"1") end
    if ipv6~="" then
        for _,key in ipairs({"ipv6_proxy","ipv6_dns_hijack"}) do set("proxy",key,"1") end
    end
    for _,key in ipairs({"enabled","dns","proxy"}) do set(id,key,"1") end
    set(id,"ip",{pool}); set(id,"ip6",ipv6~="" and {ipv6} or false)
    -- A preexisting section with our name must never broaden access by MAC.
    set(id,"mac",false)
    local first
    c:foreach(name,"lan_access_control",function(s) first=first or s[".name"] end)
    if first~=id then
        if not dry_run then logic.require(c:reorder(name,id,0),"write_failed") end
        changed=true
    end
    return changed
end
function M.plan(c,info,option,whole,read)
    local name=info.proxy
    local pool=info.pool.."/"..info.prefix
    if name=="openclash" then
        option(name,"config","lan_ac_mode","1")
        option(name,"config","lan_ac_white_ips",{pool})
        option(name,"config","lan_ac_white_macs",false)
        -- Firewall DNS redirection retains the source address in Fake-IP mode.
        option(name,"config","enable_redirect_dns","2")
        return
    end
    local path="/etc/"..name.."/ucode/hijack.ut"
    local before=read(path)
    -- Upstream resolves logical interfaces via ubus, which cannot describe
    -- ocserv's future per-session devices. Seed the native ifname interval set
    -- once; it then covers reconnects and proxy restarts without a polling job.
    local after=M.ingress(before)
    logic.require(not c:get(name,"lansyra_guard"),"guard_section_conflict")
    for _,key in ipairs({"enabled","lan_proxy","ipv4_proxy","ipv4_dns_hijack"}) do option(name,"proxy",key,"1") end
    option(name,"proxy","lan_inbound_interface",false)
    -- Keep existing ACL sections in place and only suspend them, preserving
    -- their order and any later unrelated edits when the guard is disabled.
    c:foreach(name,"lan_access_control",function(s) option(name,s[".name"],"enabled","0") end)
    whole(name,"lansyra_guard",{[".type"]="lan_access_control",enabled="1",ip={pool},dns="1",proxy="1"})
    return {path=path,before=before,after=after}
end
return M
