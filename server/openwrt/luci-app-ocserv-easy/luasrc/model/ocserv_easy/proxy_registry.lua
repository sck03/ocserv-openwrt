-- SPDX-License-Identifier: GPL-3.0-or-later
-- Keep service identity and capabilities in one place. A shared core binary
-- does not imply shared configuration, ingress rules or rollback semantics.
local entries={
    {name="openclash",label="OpenClash",option="enable",adapter="openclash"},
    {name="nikki",label="Nikki",option="enabled",adapter="ucode"},
    {name="momo",label="Momo",option="enabled",adapter="ucode"},
    {name="passwall",label="PassWall"},
    {name="passwall2",label="PassWall 2"},
    {name="homeproxy",label="HomeProxy"},
    {name="shadowsocksr",label="SSR Plus+"}
}
local M={}
function M.get(name)
    for _,entry in ipairs(entries) do if entry.name==name then return entry end end
end
function M.names(managed)
    local names={}
    for _,entry in ipairs(entries) do
        if not managed or entry.adapter then names[#names+1]=entry.name end
    end
    return names
end
return M
