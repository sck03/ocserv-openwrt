-- SPDX-License-Identifier: GPL-3.0-or-later
-- Installed APK metadata is authoritative; the UI has no independent version constant.
local storage = require "ocserv.storage"
local cache = require "luci.model.ocserv_easy.query_cache"
local M = {}
function M.read()
    local path = "/lib/apk/db/installed"
    return cache.get("ui-version",cache.identity(path),300,function()
        local selected, version = false, "unknown"
        local ok = pcall(storage.scan_lines,path,function(line)
            if line:sub(1,2)=="P:" then selected=line=="P:luci-app-ocserv-easy" end
            if selected and line:sub(1,2)=="V:" then
                local value=line:sub(3)
                if #value<=64 and value:match("^[%w%.%+%-_]+$") then version=value end
                return false
            end
        end,4194304)
        return {version=ok and version or "unknown",cacheable=ok and version~="unknown"}
    end).version
end
return M
