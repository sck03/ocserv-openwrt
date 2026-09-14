-- SPDX-License-Identifier: GPL-3.0-or-later
module("luci.controller.ocserv_easy", package.seeall)

function index()
    if not require("nixio.fs").access("/etc/config/ocserv") then return end
    local base={"admin","vpn","ocserv","easy"}
    local page=entry(base,call("page"),"布利杰VPN",100)
    page.dependent=true
    page.acl_depends={"luci-app-ocserv-easy"}
    for _,item in ipairs({{"data",call("data")},{"status",call("status")},{"action",post("action")},{"download",post("download")}}) do
        local child=entry({"admin","vpn","ocserv","easy",item[1]},item[2])
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
    local ok,result=pcall(operation)
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
function data() response(function() return require("luci.model.ocserv_easy.backend").data() end) end
function status() response(function() return require("luci.model.ocserv_easy.backend").status() end) end
function action()
    response(function()
        local logic=require "luci.model.ocserv_easy.logic"
        logic.require(can_write(),"forbidden")
        local raw=require("luci.http").formvalue("payload")
        logic.require(type(raw)=="string" and #raw<=65536,"bad_request")
        local request=require("luci.jsonc").parse(raw)
        return require("luci.model.ocserv_easy.backend").action(request)
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
        http.write("Cannot export. Check the HTTPS server address and the server CA certificate. / 无法导出，请检查服务器地址和 CA 证书。")
        return
    end
    http.header("Content-Disposition",'attachment; filename="'..filename..'"')
    http.prepare_content("application/octet-stream")
    http.write(content)
end
