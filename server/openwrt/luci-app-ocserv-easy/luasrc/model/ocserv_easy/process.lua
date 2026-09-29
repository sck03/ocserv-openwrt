-- SPDX-License-Identifier: GPL-3.0-or-later
local nixio=require "nixio"
local M={}
-- No shell, bounded output and execution. Secret input is never accepted here.
function M.run(argv,timeout)
    local input,output=nixio.pipe()
    if not input then return 127,"" end
    local pid=nixio.fork()
    if not pid then input:close(); output:close(); return 127,"" end
    if pid==0 then
        -- Give service scripts their own process group, so a timed-out restart
        -- cannot leave children applying stale configuration after rollback.
        if not nixio.setsid() then os.exit(127) end
        input:close()
        local null=nixio.open("/dev/null","r")
        if null then nixio.dup(null,nixio.stdin); null:close() end
        nixio.dup(output,nixio.stdout); nixio.dup(output,nixio.stderr); output:close()
        nixio.exec(unpack(argv))
        os.exit(127)
    end
    output:close(); input:setblocking(false)
    local chunks,size={},0
    -- Router wall time can jump when NTP synchronizes. Uptime keeps command
    -- deadlines bounded even after a backwards clock correction.
    local start=nixio.sysinfo().uptime
    local eof=false
    local code=124
    local function drain()
        while true do
            if nixio.sysinfo().uptime-start>=(timeout or 5) then return false end
            if eof then return true end
            local chunk=input:read(4096)
            if chunk=="" then eof=true; return true end
            if not chunk then return true end
            local remaining=131072-size
            chunks[#chunks+1]=chunk:sub(1,remaining)
            size=size+#chunk
            if size>131072 then return false end
        end
    end
    while true do
        if not drain() then
            nixio.kill(-pid,9); nixio.kill(pid,9); nixio.waitpid(pid); break
        end
        local child,state,status=nixio.waitpid(pid,"nohang")
        if child then
            code=state=="exited" and status or 128
            if not drain() then
                code=124
                nixio.kill(-pid,9)
            end
            break
        end
        -- A pipe at EOF is always readable/HUP. Waiting on it would spin at
        -- full CPU if a service closed stdout but had not exited yet.
        nixio.poll(eof and {} or {{fd=input,events=nixio.poll_flags("in","hup","err")}},100)
    end
    input:close()
    return code,table.concat(chunks)
end
return M
