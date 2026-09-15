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
    local start=nixio.gettimeofday()
    local code=124
    while true do
        while true do
            local chunk=input:read(4096)
            if not chunk or chunk=="" then break end
            size=size+#chunk
            if size<=131072 then chunks[#chunks+1]=chunk end
        end
        local child,state,status=nixio.waitpid(pid,"nohang")
        if child then
            code=state=="exited" and status or 128
            while true do
                local tail=input:read(4096)
                if not tail or tail=="" then break end
                size=size+#tail; if size<=131072 then chunks[#chunks+1]=tail end
            end
            break
        end
        if size>131072 or nixio.gettimeofday()-start>=(timeout or 5) then
            nixio.kill(-pid,9); nixio.kill(pid,9); nixio.waitpid(pid); break
        end
        nixio.poll({{fd=input,events=nixio.poll_flags("in","hup","err")}},100)
    end
    input:close()
    return code,table.concat(chunks)
end
return M
