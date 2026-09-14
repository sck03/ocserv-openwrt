"""Run portable LuCI regressions using Lua 5.1, and write a machine-readable report."""
from pathlib import Path
import base64
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.tools/python-test'))
from lupa.lua51 import LuaRuntime

lua = LuaRuntime(unpack_returned_tuples=True)
source = ROOT / 'server/openwrt/luci-app-ocserv-easy/luasrc'
for path in source.rglob('*.lua'):
    lua.execute('assert(loadstring(...))', path.read_text(encoding='utf-8'))
for path in (source / 'model/ocserv_easy').glob('*.lua'):
    module = 'luci.model.ocserv_easy.' + path.stem
    lua.execute('package.preload[...] = assert(loadstring(select(2,...)))', module, path.read_text(encoding='utf-8'))
lua.globals().TEST_TEMPLATE = (ROOT / 'server/openwrt/ocserv/files/ocserv.conf.template').read_text(encoding='utf-8')
lua.globals().TEST_BASE64 = lambda value: base64.b64encode(value.encode()).decode()
try:
    result = lua.execute((ROOT / 'tests/server_logic_tests.lua').read_text(encoding='utf-8'))
    names = [result[i] for i in range(1, len(result)+1)]
except Exception as error:
    print(f'LuCI regression failure: {error}', file=sys.stderr)
    raise
report = {'runtime': 'Lua 5.1', 'passed': len(names), 'cases': names, 'boundary': 'OpenWrt UCI/nixio/occtl are simulated; no router modified'}
destination = ROOT / 'test-results/server-regressions.json'
destination.parent.mkdir(exist_ok=True)
destination.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({'passed': len(names), 'report': str(destination)}))
