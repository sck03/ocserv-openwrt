-- SPDX-License-Identifier: GPL-3.0-or-later
-- Public .vpn schema and platform capabilities; no filesystem or HTTP state.
local logic = require "ocserv.logic"
local M = {}
M.targets = {
    {id="windows",label="Windows",modes={"pin","profile","address"}},
    {id="android",label="Android",modes={"pin","profile","address"},split_dns=false},
    {id="macos",label="macOS",modes={"pin"}},
    {id="ios",label="iOS",modes={"pin"}},
    {id="linux",label="Linux",modes={"profile","address"}},
}
function M.validate(server, mode, platform)
    local normalized, cert_mode = logic.validate_client_profile(server, mode)
    platform = platform or "windows"
    for _, target in ipairs(M.targets) do
        if target.id == platform then
            for _, supported in ipairs(target.modes) do
                if supported == cert_mode then return normalized, cert_mode, platform end
            end
        end
    end
    logic.fail("unsupported_client_profile")
end
function M.encode(server, mode, value)
    local trust = ({pin="pin",profile="ca",address="prompt"})[mode]
    logic.require(trust,"invalid_cert_mode")
    return "[VPN]\nVersion=1\nServer="..server.."\nProtocol=anyconnect\nTrust="..trust.."\n"..
        (mode=="pin" and "ServerPin="..value.."\n" or mode=="profile" and "CABase64="..value.."\n" or "")
end
return M
