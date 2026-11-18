-- Opt-in on OpenWrt: run with the installed Lua/UCI/nixio and service modules.
-- Only the private temporary directory is written; router settings are read.
local nixio = require "nixio"
local fs = require "nixio.fs"
local storage = require "ocserv.storage"
local runtime = require "ocserv.runtime"
local run = require("ocserv.process").run
local root = "/tmp/lansyra-runtime-check-" .. nixio.getpid()
assert(not fs.stat(root), "Temporary directory already exists")
storage.directory(root)
local checks = {}
local function passed(label)
    checks[#checks + 1] = label
    print("PASS " .. label)
end
local ok, failure = xpcall(function()
    local cursor = require("uci").cursor()
    local config, passwd = runtime.render(cursor)
    assert(type(config) == "string" and type(passwd) == "string")
    runtime.check(config, passwd, root .. "/check")
    assert(not fs.stat(root .. "/check.conf") and not fs.stat(root .. "/check.passwd"))
    passed("native UCI cursor renders and real ocserv validates the current configuration")

    assert(run({"/bin/sh", "-c", "exit 7"}, 5) == 7)
    assert(run({"/bin/sh", "-c", "printf runtime-ok"}, 5) == 0)
    assert(run({"sh", "-c", "exit 0"}, 5) == 127)
    passed("nixio execv uses explicit paths and preserves command status")
    local start = nixio.sysinfo().uptime
    assert(run({"/bin/sh", "-c", "sleep 20"}, 1) == 124)
    assert(nixio.sysinfo().uptime - start < 5)
    passed("the real process runner enforces deadlines")

    local path = root .. "/storage"
    for _, value in ipairs({"", "UTF-8: 测试\n", string.rep("x", 131072)}) do
        storage.atomic(path, value)
        assert(storage.read(path) == value)
        assert(fs.stat(path).modestr:sub(-9) == "rw-------")
    end
    storage.remove(path)
    assert(storage.read(path) == nil)
    passed("atomic writes, empty files, permissions, read limits and directory sync use native nixio")
end, function(err)
    return type(err) == "table" and err.code or tostring(err)
end)
for name in fs.dir(root) do
    if name ~= "." and name ~= ".." then assert(fs.remove(root .. "/" .. name)) end
end
assert(fs.rmdir(root))
if not ok then io.stderr:write("FAIL ", failure, "\n"); os.exit(1) end
print(require("luci.jsonc").stringify({passed = #checks, checks = checks}))
