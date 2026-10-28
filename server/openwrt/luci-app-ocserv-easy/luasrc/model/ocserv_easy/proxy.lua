-- SPDX-License-Identifier: GPL-3.0-or-later
-- Proxy-specific configuration; the guard owns persistence and rollback.
local fs=require "nixio.fs"
local json=require "luci.jsonc"
local logic=require "luci.model.ocserv_easy.logic"
local run=require("luci.model.ocserv_easy.process").run
local M={}
local supported={openclash="enable",nikki="enabled",momo="enabled"}
function M.running(name)
    logic.require(supported[name],"guard_state_invalid")
    if name=="openclash" then
        for _,core in ipairs({"clash","mihomo","clash_meta","clash-meta","mihomo-alpha"}) do
            if run({"/bin/pidof",core},3)==0 then return true end
        end
        return false
    end
    -- Nikki shares Mihomo with OpenClash. A global pidof cannot identify
    -- which service owns the process; use its named procd instance.
    local code,out=run({"/bin/ubus","call","service","list",json.stringify({name=name})},3)
    local data=code==0 and json.parse(out)
    local instances=type(data)=="table" and type(data[name])=="table" and data[name].instances
    local instance=type(instances)=="table" and instances[name]
    return type(instance)=="table" and instance.running==true
end
function M.detect(c)
    local selected
    for _,name in ipairs({"openclash","nikki","momo"}) do
        if c:get(name,"config",supported[name])=="1" then
            logic.require(not selected,"guard_proxy_conflict")
            selected=name
        end
    end
    logic.require(selected and fs.access("/etc/init.d/"..selected) and M.running(selected),"guard_requires_proxy")
    if selected~="openclash" then
        logic.require(c:get(selected,"config","core_only")~="1" and c:get(selected,"proxy"),"guard_proxy_mode")
        local tcp,udp=c:get(selected,"proxy","tcp_mode"),c:get(selected,"proxy","udp_mode")
        logic.require((tcp=="redirect" or tcp=="tproxy" or tcp=="tun") and (udp=="tproxy" or udp=="tun"),"guard_proxy_mode")
    end
    return selected
end
function M.ready(name,managed)
    if not M.running(name) then return false end
    if name=="openclash" or not managed then return true end
    -- procd can be running even if template generation or TUN startup failed.
    local code,out=run({"/usr/sbin/nft","list","set","inet",name,"lan_inbound_device"},3)
    return code==0 and (out or ""):find('"vpns*"',1,true)~=nil
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
function M.connect(c,name,pool,ipv6)
    -- Put the VPN rule first, so a LAN-wide catch-all ACL cannot bypass it.
    -- Other ACLs, LAN interfaces, router traffic and subscription rules stay intact.
    local id="lansyra_vpn"
    local existing=c:get_all(name,id)
    logic.require(not existing or existing[".type"]=="lan_access_control","guard_section_conflict")
    local changed=false
    local function set(section,key,value)
        if not logic.equal(c:get(name,section,key) or false,value or false) then
            if value then logic.require(c:set(name,section,key,value),"write_failed")
            else logic.require(c:delete(name,section,key),"write_failed") end
            changed=true
        end
    end
    if not existing then logic.require(c:set(name,id,"lan_access_control"),"write_failed"); changed=true end
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
    if first~=id then logic.require(c:reorder(name,id,0),"write_failed"); changed=true end
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
