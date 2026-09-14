-- SPDX-License-Identifier: GPL-3.0-or-later
-- Pure validation and rendering; shared by LuCI and the Lua 5.1 regression tests.
local M = {}

function M.fail(code) error({ code = code }, 0) end
function M.require(ok, code) if not ok then M.fail(code) end return ok end
function M.supported_version(value)
    if type(value)~="string" then return false end
    local major,minor=value:match("^(%d+)%.(%d+)%.%d+")
    return major~=nil and (tonumber(major)>1 or tonumber(major)==1 and tonumber(minor)>=5)
end
function M.text(value, limit)
    return type(value) == "string" and #value <= (limit or 1024) and not value:find("[%z\1-\31\127]")
end
function M.username(value)
    return M.text(value, 64) and value:match("^[A-Za-z0-9][A-Za-z0-9_.@-]*$") ~= nil
end
function M.group(value)
    return M.text(value, 128) and (value == "*" or value:match("^[A-Za-z0-9_.@-]+[,A-Za-z0-9_.@-]*$") ~= nil)
end
function M.password(value)
    -- Byte limits agree with the server's crypt interface. Never trim a password.
    return M.text(value, 128) and #value >= 8
end
function M.ipv4(value)
    if type(value) ~= "string" then return nil end
    local a,b,c,d = value:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$")
    if not a then return nil end
    local n = 0
    for _, part in ipairs({a,b,c,d}) do
        if #part > 3 or (#part > 1 and part:sub(1,1) == "0") or tonumber(part) > 255 then return nil end
        n = n * 256 + tonumber(part)
    end
    return n
end
function M.ipv6(value)
    if not M.text(value, 39) or not value:find(":", 1, true) or value:find("[^0-9a-fA-F:]") then return false end
    if value:find(":::", 1, true) then return false end
    local first = value:find("::", 1, true)
    if first and value:find("::", first + 2, true) then return false end
    if not first and (value:sub(1,1) == ":" or value:sub(-1) == ":") then return false end
    local count = 0
    for part in value:gmatch("[^:]+") do if #part > 4 then return false end count = count + 1 end
    return first and count < 8 or not first and count == 8
end
function M.prefix(mask)
    if type(mask) ~= "string" then return nil end
    if mask:match("^%d+$") then
        local p = tonumber(mask)
        if p <= 32 then return p end
        return nil
    end
    local n = M.ipv4(mask)
    if not n then return nil end
    for p = 0,32 do if n == 4294967296 - 2^(32-p) then return p end end
end
function M.mask(prefix)
    local n = 4294967296 - 2^(32-prefix)
    local parts = {}
    for i = 3,0,-1 do parts[#parts+1] = tostring(math.floor(n/256^i)%256) end
    return table.concat(parts, ".")
end
function M.domain(value)
    if not M.text(value, 253) or value == "" or value:find("[^A-Za-z0-9.-]") then return false end
    if value:find("..", 1, true) then return false end
    for label in value:gmatch("[^.]+") do
        if #label > 63 or not label:match("^[A-Za-z0-9]") or not label:match("[A-Za-z0-9]$") then return false end
    end
    return value:sub(1,1) ~= "." and value:sub(-1) ~= "."
end
function M.url(value)
    if not M.text(value, 512) then return nil end
    local authority = value:match("^https://([^/]+)/?$")
    if not authority then return nil end
    local host, port
    if authority:sub(1,1) == "[" then
        host, port = authority:match("^%[([^%]]+)%]:(%d+)$")
        if not host then host = authority:match("^%[([^%]]+)%]$") end
        if not host or not M.ipv6(host) then return nil end
    else
        host, port = authority:match("^([^:]+):(%d+)$")
        if not host then host = authority end
        if not M.domain(host) then return nil end
        if host:match("^[%d.]+$") and not M.ipv4(host) then return nil end
    end
    if port and (#port > 5 or tonumber(port) < 1 or tonumber(port) > 65535) then return nil end
    return value:gsub("/$", ""), host
end
function M.revision(value)
    -- Optimistic concurrency token, not a password hash or an authentication token.
    local a, b = 1, 7
    for i = 1,#value do local c=value:byte(i); a=(a*131+c)%4294967296; b=(b*65599+c)%4294967296 end
    return string.format("%08x%08x%08x", a, b, #value)
end
function M.public_users(users)
    local rows = {}
    for _, u in ipairs(users) do
        if u.name and u.name ~= "" then
            rows[#rows+1] = { id=u.id, name=u.name, group=u.group or "*", enabled=type(u.password)=="string" and u.password:sub(1,1)=="$" }
        end
    end
    return rows
end
function M.passwd(users)
    local seen, rows = {}, {}
    for _, u in ipairs(users) do
        if u.name and u.name ~= "" then
            M.require(M.username(u.name), "invalid_existing_user")
            M.require(not seen[u.name], "duplicate_user")
            seen[u.name] = true
            M.require(M.group(u.group or "*"), "invalid_group")
            M.require(M.text(u.password, 512) and u.password ~= "" and not u.password:find(":", 1, true), "invalid_existing_password")
            M.require(u.password:match("^!?%$") or u.password:match("^[!*]+$"), "invalid_existing_password")
            rows[#rows+1] = u.name .. ":" .. (u.group or "*") .. ":" .. u.password .. "\n"
        end
    end
    return table.concat(rows)
end
function M.change_user(users, request, hash_password)
    M.require(type(request) == "table", "bad_request")
    M.require(request.action == "save_user" or request.action == "delete_user", "bad_action")
    local target, index
    local copy = {}
    for i,u in ipairs(users) do
        copy[i] = { id=u.id, name=u.name, group=u.group, password=u.password }
        if request.id and request.id ~= "" and u.id == request.id then target=copy[i]; index=i end
    end
    M.require(not request.id or request.id == "" or target, "user_not_found")
    local revoke
    if request.action == "delete_user" then
        M.require(target, "user_not_found")
        revoke = target.name
        table.remove(copy, index)
    else
        M.require(M.username(request.name), "invalid_username")
        M.require(M.group(request.group or "*"), "invalid_group")
        M.require(type(request.enabled) == "boolean", "bad_request")
        for _,u in ipairs(copy) do M.require(u == target or u.name ~= request.name, "duplicate_user") end
        local password = request.password or ""
        M.require(type(password) == "string", "invalid_password")
        M.require(target or password ~= "", "password_required")
        if password ~= "" then M.require(M.password(password), "invalid_password") end
        local previous = target and target.password or ""
        local encoded = password ~= "" and hash_password(password) or previous:gsub("^!+", "")
        M.require(type(encoded)=="string" and encoded:match("^%$[A-Za-z0-9]+%$") and #encoded <= 512 and not encoded:find("[%s:]"), "hash_failed")
        if not request.enabled then encoded = "!" .. encoded end
        if target and (target.name ~= request.name or (target.group or "*") ~= (request.group or "*") or
           (target.password ~= encoded and not (target.password:sub(1,1)=="!" and request.enabled and password==""))) then revoke=target.name end
        target = target or {}
        if not index then copy[#copy+1]=target end
        target.name=request.name; target.group=request.group or "*"; target.password=encoded
    end
    M.passwd(copy)
    return copy, revoke
end

M.defaults = { port="4443", udp_port="", max_clients="8", max_same="2", dpd="120", udp="1", compression="0",
    cisco_compat="1", predictable_ips="1", ipaddr="192.168.100.0", netmask="255.255.255.0", ip6addr="", default_domain="", easy_public_url="" }
local numbers={port={1,65535},max_clients={1,4096},max_same={1,64},dpd={10,3600}}
local flags={udp=true,compression=true,cisco_compat=true,predictable_ips=true}
function M.settings(config, dns, routes)
    local s={dns={},routes={}}
    for key,value in pairs(M.defaults) do s[key] = config[key] or value end
    for _,row in ipairs(dns) do if row.ip and row.ip~="" then s.dns[#s.dns+1]=row.ip end end
    for _,row in ipairs(routes) do if row.ip and row.ip~="" then s.routes[#s.routes+1]=row.ip.."/"..(row.netmask or "") end end
    s.route_mode = #s.routes == 0 and "all" or "split"
    return s
end
function M.validate_settings(input)
    M.require(type(input)=="table", "bad_request")
    local out={}
    for key,default in pairs(M.defaults) do
        local value=input[key]
        if value==nil then value=default end
        M.require(M.text(value,512), "invalid_settings")
        if numbers[key] then
            local n=value:match("^%d+$") and tonumber(value)
            M.require(n and n>=numbers[key][1] and n<=numbers[key][2], "invalid_"..key)
            value=tostring(n)
        elseif flags[key] then M.require(value=="0" or value=="1", "invalid_settings") end
        out[key]=value
    end
    if out.udp_port~="" then
        M.require(out.udp_port:match("^%d+$") and tonumber(out.udp_port)>=1 and tonumber(out.udp_port)<=65535, "invalid_port")
    end
    M.require(tonumber(out.max_same)<=tonumber(out.max_clients), "invalid_client_limit")
    local ip=M.ipv4(out.ipaddr); local prefix=M.prefix(out.netmask)
    M.require(ip and ip>0 and ip<3758096384 and math.floor(ip/16777216)~=127 and prefix and prefix>=8 and prefix<=30, "invalid_pool")
    M.require(ip%2^(32-prefix)==0, "pool_not_network")
    out.netmask=M.mask(prefix)
    if out.ip6addr~="" then
        local address,p=out.ip6addr:match("^(.+)/(%d+)$")
        M.require(address and M.ipv6(address) and tonumber(p)>=16 and tonumber(p)<=112 and address:lower():sub(1,2)~="ff", "invalid_ipv6_pool")
    end
    M.require(out.default_domain=="" or M.domain(out.default_domain), "invalid_domain")
    M.require(out.easy_public_url=="" or M.url(out.easy_public_url), "invalid_url")
    M.require(type(input.dns)=="table" and #input.dns>=1 and #input.dns<=3, "invalid_dns")
    local dns,seen={},{}
    local _,endpoint=M.url(out.easy_public_url)
    for _,address in ipairs(input.dns) do
        local number=M.ipv4(address)
        M.require((number and number>0 and number<3758096384 and math.floor(number/16777216)~=127) or
            (M.ipv6(address) and address~="::" and address~="::1" and address:lower():sub(1,2)~="ff"), "invalid_dns")
        M.require(address~=endpoint, "dns_is_endpoint")
        M.require(not seen[address], "duplicate_dns"); seen[address]=true
        dns[#dns+1]={ip=address}
    end
    M.require(input.route_mode=="all" or input.route_mode=="split", "invalid_route_mode")
    local routes={}
    if input.route_mode=="split" then
        M.require(type(input.routes)=="table" and #input.routes>=1 and #input.routes<=128, "invalid_routes")
        for _,route in ipairs(input.routes) do
            M.require(M.text(route,100), "invalid_routes")
            local address,mask=route:match("^([^/]+)/([^/]+)$")
            local number=M.ipv4(address); local p=M.prefix(mask)
            if number then
                M.require(p and number%2^(32-p)==0, "invalid_routes")
                mask=M.mask(p)
            else
                M.require(M.ipv6(address) and mask and mask:match("^%d+$") and tonumber(mask)<=128, "invalid_routes")
                M.require(out.ip6addr~="", "route_needs_ipv6")
            end
            routes[#routes+1]={ip=address,netmask=mask}
        end
    end
    return out,dns,routes
end
function M.render(template, extra, config, dns, routes, context)
    context=context or {}
    M.require((config.auth or "plain")=="plain", "plain_auth_required")
    M.require(config.proxy_arp~="1", "proxy_arp_managed")
    local function flag(key,default) return (config[key] or default)=="1" and "true" or "false" end
    local domain=config.default_domain or ""
    if domain=="" then domain=context.domain or "" end
    M.require(domain=="" or M.domain(domain), "invalid_domain")
    local values={ PORT=config.port or "4443", UDP_PORT=config.udp_port~="" and config.udp_port or config.port or "4443",
        MAX_CLIENTS=config.max_clients or "8", MAX_SAME=config.max_same or "2", DPD=config.dpd or "120",
        AUTH="plain[passwd=/var/etc/ocpasswd]", DYNDNS=context.dyndns and "true" or "false",
        PREDICTABLE_IPS=flag("predictable_ips","1"), DEFAULT_DOMAIN=domain, ENABLE_DEFAULT_DOMAIN=domain~="" and "" or "#",
        ENABLE_SPLIT_DNS=config.split_dns=="1" and "" or "#", CISCO_COMPAT=flag("cisco_compat","1"),
        PING_LEASES=flag("ping_leases","0"), UDP=(config.udp or "1")=="1" and "" or "#", COMPRESSION=config.compression=="1" and "" or "#",
        IPV4ADDR=config.ipaddr or "192.168.100.0", NETMASK=config.netmask or "255.255.255.0", IPV6ADDR=config.ip6addr or "",
        ENABLE_IPV6=config.ip6addr and config.ip6addr~="" and "" or "#",
        SERVER_CERT=config.server_cert or "/etc/ocserv/server-cert.pem", SERVER_KEY=config.server_key or "/etc/ocserv/server-key.pem" }
    for _,value in pairs(values) do M.require(M.text(value,512) and not value:find("[~&]"), "unsafe_config_value") end
    local output=template:gsub("|([A-Z0-9_]+)|",function(key) M.require(values[key]~=nil,"unknown_template"); return values[key] end)
    output=output.."\n"..(extra or "").."\n"
    for _,row in ipairs(routes) do output=output.."route = "..row.ip.."/"..row.netmask.."\n" end
    for _,row in ipairs(dns) do output=output.."dns = "..row.ip.."\n" end
    return output
end
function M.standard_auth(runtime)
    local count=0
    for line in (runtime.."\n"):gmatch("([^\n]*)\n") do
        if line:match("^%s*auth%s*=") then
            if not line:match('^%s*auth%s*=%s*"plain%[passwd=/var/etc/ocpasswd%]"%s*$') then return false end
            count=count+1
        end
    end
    return count==1
end
return M
