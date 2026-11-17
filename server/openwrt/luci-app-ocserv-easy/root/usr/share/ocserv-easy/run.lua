-- SPDX-License-Identifier: GPL-3.0-or-later
-- Shell maintenance uses the same bounded process runner as the management API.
local directory = (arg[0] or ""):match("^(.*)/[^/]+$") or "."
local bundled = loadfile(directory .. "/process.lua")
local process = bundled and bundled() or require "ocserv.process"
local seconds = tonumber(table.remove(arg, 1))
assert(seconds and seconds > 0 and seconds <= 600 and #arg > 0, "Invalid command deadline")
local code, output = process.run(arg, seconds)
if output ~= "" then io.stdout:write(output) end
os.exit(code)
