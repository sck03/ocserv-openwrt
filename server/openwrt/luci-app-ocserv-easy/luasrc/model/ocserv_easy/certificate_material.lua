-- SPDX-License-Identifier: GPL-3.0-or-later
-- Public trust material must describe the certificate actually served by ocserv.
local logic = require "ocserv.logic"
local storage = require "ocserv.storage"
local run = require("ocserv.process").run
local M = {}
local ca_path = "/etc/ocserv/ca.pem"

function M.certificate(rendered)
    local certificate
    for line in (rendered .. "\n"):gmatch("([^\n]*)\n") do
        logic.require(not line:match("^%s*%[vhost:"), "pin_unavailable")
        local path = line:match("^%s*server%-cert%s*=%s*(.-)%s*$")
        if path then
            logic.require(not certificate, "pin_unavailable")
            certificate = path:match('^"(.*)"$') or path
        end
    end
    logic.require(certificate and certificate:sub(1, 1) == "/", "pin_unavailable")
    return certificate
end

function M.pin(rendered)
    local code, output = run({"/usr/bin/certtool", "--pubkey-info", "--load-certificate", M.certificate(rendered)}, 8)
    local pin = code == 0 and ("\n" .. output .. "\n"):match("\n%s*(pin%-sha256:[A-Za-z0-9+/]+=)%s*\n")
    logic.require(pin and #pin == 55, "pin_unavailable")
    return pin
end

function M.ca(rendered, hostname)
    local certificate = M.certificate(rendered)
    local ca = storage.read(ca_path, 65536)
    local leaf = storage.read(certificate, 65536)
    logic.require(ca and ca:find("-----BEGIN CERTIFICATE-----", 1, true) and
        ca:find("-----END CERTIFICATE-----", 1, true) and not ca:find("PRIVATE KEY", 1, true), "ca_unavailable")
    logic.require(leaf and not leaf:find("PRIVATE KEY", 1, true), "ca_certificate_mismatch")
    -- Explicit trust anchors only; do not accept an unrelated system root.
    local arguments = {"/usr/bin/certtool", "--verify", "--verify-purpose", "1.3.6.1.5.5.7.3.1",
        "--load-ca-certificate", ca_path, "--infile", certificate}
    if hostname then arguments[#arguments+1] = "--verify-hostname"; arguments[#arguments+1] = hostname end
    local code = run(arguments, 8)
    logic.require(code == 0, "ca_certificate_mismatch")
    logic.require(ca == storage.read(ca_path, 65536) and leaf == storage.read(certificate, 65536), "stale_revision")
    return ca
end

return M
