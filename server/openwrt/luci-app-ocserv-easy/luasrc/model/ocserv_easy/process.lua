-- SPDX-License-Identifier: GPL-3.0-or-later
local nixio=require "nixio"
local M={}
-- No shell, bounded output and execution. Secret input is never accepted here.
-- 124: deadline/output limit; 125: runner I/O failure; 127: spawn/exec failure.
function M.run(argv,timeout)
    local input,output=nixio.pipe()
    if not input then return 127,"" end
    -- Configure before fork: a failed fcntl must never leave a blocking read
    -- that prevents the parent from enforcing the command deadline.
    if not input:setblocking(false) then input:close(); output:close(); return 125,"" end
    local pid=nixio.fork()
    if not pid then input:close(); output:close(); return 127,"" end
    if pid==0 then
        -- Give service scripts their own process group, so a timed-out restart
        -- cannot leave children applying stale configuration after rollback.
        if not nixio.setsid() then os.exit(127) end
        input:close()
        local null=nixio.open("/dev/null","r")
        if not null or not nixio.dup(null,nixio.stdin) or
            not nixio.dup(output,nixio.stdout) or not nixio.dup(output,nixio.stderr) then os.exit(127) end
        null:close(); output:close()
        nixio.exec(unpack(argv))
        os.exit(127)
    end
    output:close()
    local chunks,size={},0
    -- Router wall time can jump when NTP synchronizes. Uptime keeps command
    -- deadlines bounded even after a backwards clock correction.
    local start=nixio.sysinfo().uptime
    local eof=false
    local code,reaped=124,false
    local watch={{fd=input,events=nixio.poll_flags("in","hup","err")}}
    local idle={}
    local function drain()
        while true do
            if nixio.sysinfo().uptime-start>=(timeout or 5) then return 124 end
            if eof then return true end
            local chunk=input:read(4096)
            if chunk=="" then eof=true; return true end
            -- nixio returns false for EAGAIN/EWOULDBLOCK, nil for I/O errors.
            if chunk==false then return true end
            if chunk==nil then return 125 end
            local remaining=131072-size
            chunks[#chunks+1]=chunk:sub(1,remaining)
            size=size+#chunk
            if size>131072 then return 124 end
        end
    end
    while true do
        local drained=drain()
        if drained~=true then code=drained; break end
        local child,state,status=nixio.waitpid(pid,"nohang")
        if child==nil then code=125; reaped=state==10; break end -- ECHILD: no longer owned.
        if child then
            reaped=true
            code=state=="exited" and status or 128
            drained=drain()
            if drained==true then input:close(); return code,table.concat(chunks) end
            code=drained
            break
        end
        -- A pipe at EOF is always readable/HUP. Waiting on it would spin at
        -- full CPU if a service closed stdout but had not exited yet.
        local ready,err=nixio.poll(eof and idle or watch,100)
        if ready==nil and err~=4 then code=125; break end -- EINTR retries under the same deadline.
    end
    -- A reaped/unowned PID may already have been reused; only target its
    -- remaining process group in that case, never the old positive PID.
    nixio.kill(-pid,9)
    if not reaped then nixio.kill(pid,9); nixio.waitpid(pid) end
    input:close()
    return code,table.concat(chunks)
end
return M
