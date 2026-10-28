"""Exercise actual controller ACL decisions before privileged backend actions."""
import json
from pathlib import Path
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
model = ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy"
lua.execute('package.preload[...] = assert(loadstring(select(2,...)))',
            "luci.model.ocserv_easy.logic", (model / "logic.lua").read_text(encoding="utf-8"))
lua.execute('package.preload[...] = assert(loadstring(select(2,...)))',
            "luci.model.ocserv_easy.proxy_registry", (model / "proxy_registry.lua").read_text(encoding="utf-8"))
lua.execute('''
permissions={ocserv=true}; payload={action="settings"}; calls=0
package.loaded["luci.dispatcher"]={context={authsession="fixture"}}
package.loaded["luci.util"]={ubus=function(_,_,request)return {access=permissions[request.object]==true}end}
package.loaded["luci.http"]={
    header=function()end, prepare_content=function()end,
    formvalue=function()return "fixture"end, getenv=function()return "192.168.19.2"end,
    status=function(code)response_code=code end,write_json=function(value)reply=value end
}
package.loaded["luci.jsonc"]={parse=function()return payload end}
package.loaded["luci.model.ocserv_easy.backend"]={action=function()calls=calls+1;return {effect="checked"}end}
''')
controller = ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/controller/ocserv_easy.lua"
lua.execute(controller.read_text(encoding="utf-8"))
lua.execute('''
local action=package.loaded["luci.controller.ocserv_easy"].action
action();assert(response_code==403 and reply.error=="forbidden" and calls==0)
permissions.firewall=true;action();assert(reply.ok and calls==1)
payload.action="guard";action();assert(reply.error=="forbidden" and calls==1)
permissions.dhcp=true;action();assert(reply.error=="forbidden" and calls==1)
permissions.openclash=true;action();assert(reply.error=="forbidden" and calls==1)
permissions.nikki=true;action();assert(reply.error=="forbidden" and calls==1)
permissions.momo=true;action();assert(reply.ok and calls==2)
permissions.ocserv=false;action();assert(reply.error=="forbidden" and calls==2)
''')
print(json.dumps({"passed": 8, "boundary": "Actual LuCI controller; simulated session ACL and backend"}))
