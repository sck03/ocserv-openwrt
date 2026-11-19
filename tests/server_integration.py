"""Run portable LuCI regressions using Lua 5.1, and write a machine-readable report."""
from pathlib import Path
import base64
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
from lupa.lua51 import LuaRuntime
from lua_json import install
from lua_modules import install_modules

lua = LuaRuntime(unpack_returned_tuples=True)
install(lua)
source = ROOT / 'server/openwrt/luci-app-ocserv-easy/luasrc'
for path in source.rglob('*.lua'):
    lua.execute('assert(loadstring(...))', path.read_text(encoding='utf-8'))
install_modules(lua)
lua.globals().TEST_TEMPLATE = (ROOT / 'server/openwrt/ocserv/files/ocserv.conf.template').read_text(encoding='utf-8')
lua.globals().TEST_BASE64 = lambda value: base64.b64encode(value.encode()).decode()
lua.globals().TEST_KEEP_SANDBOX = True
try:
    result = lua.execute((ROOT / 'tests/server_logic_tests.lua').read_text(encoding='utf-8'))
    names = [result[i] for i in range(1, len(result)+1)]
    fixture = lua.globals().TEST_PREVIEW
    fixture.reset()
    fixture.state().certtool_output = 'pin-sha256:' + base64.b64encode(bytes(32)).decode()
    corpus = ROOT / 'client/tests/public-profiles'
    for mode, name in [('pin', 'pinned'), ('address', 'prompt')]:
        profile, _ = fixture.backend.export(mode, 'vpn.example.test:4443', 'windows')
        assert profile.encode() == (corpus / f'{name}.vpn.fixture').read_bytes(), name
        names.append(f'{name} export matches shared public profile bytes')
except Exception as error:
    print(f'LuCI regression failure: {error}', file=sys.stderr)
    raise
report = {'runtime': 'Lua 5.1', 'passed': len(names), 'cases': names, 'boundary': 'OpenWrt UCI/nixio/occtl are simulated; no router modified'}
report['cache_reuse'] = dict(lua.globals().TEST_CACHE_REUSE.items())
destination = ROOT / 'test-results/server-regressions.json'
destination.parent.mkdir(exist_ok=True)
destination.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({'passed': len(names), 'report': str(destination)}))
