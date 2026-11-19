-- SPDX-License-Identifier: GPL-3.0-or-later
-- Trust capabilities and export validation share one application service.
local nixio = require "nixio"
local fs = require "nixio.fs"
local logic = require "ocserv.logic"
local read = require("ocserv.storage").read
local cache = require "luci.model.ocserv_easy.query_cache"
local material = require "luci.model.ocserv_easy.certificate_material"
local profile = require "luci.model.ocserv_easy.client_profile"
local M = {}
local function capability(check, fallback)
    local ok, err = pcall(check)
    return {available=ok, error=not ok and (type(err)=="table" and err.code or fallback) or nil}
end
function M.status(s, rendered)
    local certificate_path=s.config.server_cert or "/etc/ocserv/server-cert.pem"
    local certificate=read(certificate_path,65536) or ""
    local key=fs.stat(s.config.server_key or "/etc/ocserv/server-key.pem")
    local identity=s.revision..":"..cache.identity("/etc/ocserv/ca.pem")..":"..cache.identity(certificate_path)
    local ready=certificate:find("-----BEGIN CERTIFICATE-----",1,true)~=nil and key~=nil and (key.size or 0)>0
    if not rendered then return {},ready end
    local ok,path=pcall(material.certificate,rendered)
    if ok then identity=identity..":"..cache.identity(path) end
    local trust=cache.get("client-trust",identity,300,function()
        local value={
            pin=capability(function() material.pin(rendered) end,"pin_unavailable"),
            profile=capability(function() material.ca(rendered) end,"ca_unavailable"),
            address={available=true},
        }
        -- A failed probe is not a lasting statement about installed trust material.
        value.cacheable=value.pin.available and value.profile.available
        return value
    end)
    return trust,ready
end
function M.validate(s,rendered,url,mode,platform)
    local server,cert_mode,target=profile.validate(url,mode,platform,logic.settings(s.config,s.dns,{}))
    local value
    if cert_mode=="pin" then value=material.pin(rendered)
    elseif cert_mode=="profile" then
        local _,hostname=logic.url(server)
        value=nixio.bin.b64encode(material.ca(rendered,hostname))
    end
    return server,cert_mode,target,value
end
function M.export(s,rendered,kind,url,platform)
    logic.require(kind=="ca" or kind=="profile" or kind=="address" or kind=="pin","bad_request")
    if kind=="ca" then return material.ca(rendered),"ca.pem" end
    local server,mode,_,value=M.validate(s,rendered,url,kind,platform)
    return profile.encode(server,mode,value),"Lansyra.vpn"
end
return M
