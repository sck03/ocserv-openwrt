-- SPDX-License-Identifier: GPL-3.0-or-later
module("luci.controller.ocserv_easy", package.seeall)

function index()
    if not require("nixio.fs").access("/etc/config/ocserv") then return end
    entry({"admin","vpn"},firstchild(),"VPN",45).dependent=false
    local base={"admin","vpn","ocserv"}
    local page=entry(base,call("page"),"布利杰VPN",50)
    page.dependent=false
    page.acl_depends={"luci-app-ocserv-easy"}
    -- Old bookmarks lead to the one maintained page, without duplicate tabs.
    for _,name in ipairs({"easy","main","user-config","users"}) do
        entry({"admin","vpn","ocserv",name},alias("admin","vpn","ocserv")).acl_depends={"luci-app-ocserv-easy"}
    end
    for _,item in ipairs({{"data",call("data")},{"status",call("status")},{"action",post("action")},{"download",post("download")}}) do
        local child=entry({"admin","vpn","ocserv",item[1]},item[2])
        child.leaf=true
        child.acl_depends={"luci-app-ocserv-easy"}
    end
end
local function can_write()
    local context=require("luci.dispatcher").context
    local result=require("luci.util").ubus("session","access",{
        ubus_rpc_session=context.authsession,scope="uci",object="ocserv",["function"]="write"})
    return type(result)=="table" and result.access==true
end
local function response(operation)
    local http=require "luci.http"
    http.header("Cache-Control","no-store")
    http.header("X-Content-Type-Options","nosniff")
    local ok,result=xpcall(operation,function(err)
        if type(err)=="table" and type(err.code)=="string" then return err end
        -- Only code locations are logged. Do not log request bodies, UCI values
        -- or an exception message which could contain a password.
        pcall(function() require("nixio").syslog("err","ocserv-easy: "..debug.traceback("backend failure",2):gsub("\n"," | ")) end)
        return {code="internal_error"}
    end)
    http.prepare_content("application/json")
    if ok then http.write_json({ok=true,data=result})
    else
        local code=type(result)=="table" and result.code or "internal_error"
        http.status(code=="forbidden" and 403 or code=="stale_revision" and 409 or 400,"Request failed")
        http.write_json({ok=false,error=code})
    end
end
function page()
    require("luci.http").header("Cache-Control","no-store")
    require("luci.template").render("ocserv_easy/index",{writable=can_write()})
end
function data() response(function() return require("luci.model.ocserv_easy.backend").data(require("luci.http").getenv("REMOTE_ADDR")) end) end
function status() response(function()
    local result=require("luci.model.ocserv_easy.backend").status()
    result.guard=require("luci.model.ocserv_easy.guard").status(require("luci.http").getenv("REMOTE_ADDR"))
    return result
end) end
function action()
    response(function()
        local logic=require "luci.model.ocserv_easy.logic"
        logic.require(can_write(),"forbidden")
        local raw=require("luci.http").formvalue("payload")
        logic.require(type(raw)=="string" and #raw<=65536,"bad_request")
        local request=require("luci.jsonc").parse(raw)
        if type(request)=="table" and request.action=="guard" then
            local context=require("luci.dispatcher").context
            for _,package in ipairs({"firewall","dhcp","openclash"}) do
                local permission=require("luci.util").ubus("session","access",{ubus_rpc_session=context.authsession,scope="uci",object=package,["function"]="write"})
                logic.require(type(permission)=="table" and permission.access==true,"forbidden")
            end
        end
        return require("luci.model.ocserv_easy.backend").action(request,require("luci.http").getenv("REMOTE_ADDR"))
    end)
end
function download()
    local http=require "luci.http"
    local ok,content,filename=pcall(function()
        return require("luci.model.ocserv_easy.backend").export(http.formvalue("kind"),http.formvalue("server"))
    end)
    http.header("Cache-Control","no-store")
    http.header("X-Content-Type-Options","nosniff")
    if not ok then
        http.status(400,"Invalid export")
        http.prepare_content("text/plain; charset=utf-8")
        http.write("Cannot export. Check the server address and, for CA exports, the public CA certificate. / 无法导出，请检查服务器地址；附带 CA 时还需检查公共 CA 证书。")
        return
    end
    http.header("Content-Disposition",'attachment; filename="'..filename..'"')
    http.prepare_content("application/octet-stream")
    http.write(content)
end
