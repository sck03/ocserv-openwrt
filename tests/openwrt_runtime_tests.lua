-- Run only in a disposable OpenWrt rootfs with the explicit fixture marker.
-- Exercises the actual OpenWrt Lua integer ABI, nixio, UCI/rpcd and crypt.
local fs=require "nixio.fs"
assert(fs.access("/tmp/ocserv-easy-test-root"),"Disposable OpenWrt fixture marker is required")
local nixio=require "nixio"
local uci=require "luci.model.uci"
local json=require "luci.jsonc"
local logic=require "luci.model.ocserv_easy.logic"
local backend=require "luci.model.ocserv_easy.backend"
local results={}
local function check(value,label) assert(value,label);results[#results+1]=label end
local function read(path)
    local f=assert(io.open(path,"rb"));local value=f:read("*a");f:close();return value
end
local function action(payload)
    payload.revision=backend.data("192.168.19.2").revision
    return backend.action(payload,"192.168.19.2")
end
local function account(name)
    for _,u in ipairs(backend.data().users)do if u.name==name then return u end end
    error("Missing fixture account")
end
local function hash(name)
    local value
    uci.cursor():foreach("ocserv","ocservusers",function(s)if s.name==name then value=s.password end end)
    return value
end
local fixture_password="Fixture:pass' $6$ 2026"
check(#logic.revision(string.rep("overflow-fixture",100))==24,"revision supports the real OpenWrt integer ABI")
local initial=backend.data()
check(initial.supported and initial.version=="1.5.0","real ocserv version output enables management")
action({action="save_user",name="runtime-fixture",group="*",password=fixture_password,enabled=true})
local first_hash=hash("runtime-fixture")
check(logic.password_hash(first_hash) and nixio.crypt(fixture_password,first_hash)==first_hash,"account creation writes a correct SHA-512 crypt hash")
check(read("/var/etc/ocpasswd"):find("runtime-fixture:*:"..first_hash,1,true)~=nil,"account creation synchronizes the actual ocpasswd file")
check(not read("/etc/config/ocserv"):find(fixture_password,1,true),"plaintext is absent from persistent account configuration")
local public=json.stringify(backend.data())
check(not public:find(first_hash,1,true) and not public:find(fixture_password,1,true),"public API exposes neither password nor hash")
local user=account("runtime-fixture")
action({action="save_user",id=user.id,name=user.name,group="*",password="",enabled=true})
check(hash(user.name)==first_hash,"blank password edits retain the exact hash")
action({action="save_user",id=user.id,name=user.name,group="*",password="",enabled=false})
check(hash(user.name)=="!"..first_hash and not account(user.name).enabled,"disabled account retains its hash with a login lock")
action({action="save_user",id=user.id,name=user.name,group="*",password="",enabled=true})
check(hash(user.name)==first_hash and account(user.name).enabled,"reenabling restores the same password")
local c=uci.cursor()
local old=c:add("ocserv","ocservusers")
assert(c:set("ocserv",old,"name","legacy-fixture"))
assert(c:set("ocserv",old,"password","test"))
assert(c:commit("ocserv"))
local fixed=backend.repair_users()
local legacy_hash=hash("legacy-fixture")
check(fixed.converted==1 and nixio.crypt("test",legacy_hash)==legacy_hash,"legacy plaintext is migrated without changing the password")
check(account("legacy-fixture").group=="*","missing legacy group becomes the default group")
check(hash(user.name)==first_hash,"legacy migration preserves existing hashed accounts")
check(backend.repair_users().converted==0 and hash("legacy-fixture")==legacy_hash,"repeating account migration is idempotent")
local config_mode=fs.stat("/etc/config/ocserv","modestr")
local passwd_mode=fs.stat("/var/etc/ocpasswd","modestr")
check(config_mode=="rw-------" and passwd_mode=="rw-------","persistent and runtime password files are root-only")
-- Leave only the synthetic runtime account for the real TLS login test.
action({action="delete_user",id=old})
check(#backend.data().users==1,"deletion updates native UCI and the runtime account list")
print(json.stringify({passed=#results,checks=results,boundary="Real OpenWrt Lua/nixio/UCI and crypt in an isolated rootfs"}))
