-- SPDX-License-Identifier: GPL-3.0-or-later
-- POSIX record locks match nixio.lockf used by older management packages.
-- Keep the descriptor open and never unlink the lock file while in use.
local nixio = require "nixio"
local fs = require "nixio.fs"
local M = { path = "/var/run/ocserv-easy/lock" }

function M.acquire(attempts, delay)
    local root = M.path:match("^(.*)/[^/]+$")
    if not fs.stat(root) and not fs.mkdir(root, "700") then return nil, "write_failed" end
    if not fs.chmod(root, "700") then return nil, "write_failed" end
    local file = nixio.open(M.path, nixio.open_flags("wronly", "creat"), "600")
    if not file then return nil, "write_failed" end
    for attempt = 1, attempts or 1 do
        if file:lock("tlock") then return file end
        if attempt < (attempts or 1) then nixio.poll({}, delay or 100) end
    end
    file:close()
    return nil, "busy"
end

function M.release(file)
    file:lock("ulock")
    file:close()
end

return M
