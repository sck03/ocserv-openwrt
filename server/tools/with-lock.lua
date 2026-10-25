-- SPDX-License-Identifier: GPL-3.0-or-later
-- Run shell maintenance under the same lease as LuCI and recovery workers.
local nixio = require "nixio"
local directory = (arg[0] or ""):match("^(.*)/[^/]+$") or "."
local lease = assert(loadfile(directory .. "/lease.lua"))()
local file, code = lease.acquire()
if not file then
    io.stderr:write(code == "busy" and "Lansyra 正在执行其他维护操作，请稍后重试。\n" or "无法取得 Lansyra 维护锁。\n")
    os.exit(1)
end
assert(#arg >= 1, "A command is required")
-- nixio opens without CLOEXEC. POSIX record locks survive exec in this process;
-- child processes do not inherit ownership, so daemons cannot retain the lease.
if nixio.setenv("OCSERV_EASY_LOCK_OWNER", tostring(nixio.getpid())) then
    nixio.exec(unpack(arg))
end
lease.release(file)
io.stderr:write("无法启动 Lansyra 维护命令。\n")
os.exit(1)
