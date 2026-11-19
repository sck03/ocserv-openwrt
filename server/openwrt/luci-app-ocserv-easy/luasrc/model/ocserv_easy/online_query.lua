-- SPDX-License-Identifier: GPL-3.0-or-later
-- Online list and summary share one query, content version and failure boundary.
local logic = require "ocserv.logic"
local read = require("ocserv.storage").read
local query_cache = require "luci.model.ocserv_easy.query_cache"
local sessions = require "luci.model.ocserv_easy.occtl"
local configuration = require "luci.model.ocserv_easy.configuration"
local M = {}
local function summarize(online)
    return {online_count=#online.online,online_error=online.online_error==true}
end
local function valid_online(value)
    return logic.array(value.online,4096) and (value.online_error==nil or type(value.online_error)=="boolean")
end
local function valid_summary(value)
    return type(value.online_count)=="number" and value.online_count%1==0 and value.online_count>=0 and
        value.online_count<=4096 and type(value.online_error)=="boolean"
end
function M.status(is_running,fresh,summary)
    local result={running=is_running,online={},online_count=0,online_error=false,online_version="stopped",recovery_pending=configuration.pending()}
    if not result.running then return result end
    local identity=read("/var/run/ocserv.pid",64) or ""
    local online,version
    if summary then
        online,version=query_cache.projected("online",identity,5,sessions.online,summarize,sessions.cache_limit,valid_online,valid_summary)
        result.online_count=online.online_count
    else
        online,version=query_cache.get("online",identity,5,sessions.online,fresh,sessions.cache_limit,summarize,valid_online)
        result.online=online.online
        result.online_count=#online.online
    end
    result.online_version=version or "uncached"
    result.online_error=online.online_error==true
    return result
end
return M
