"""Exercise the real VPN-only transaction manager against isolated OpenWrt interfaces."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.tools/python-test'))
from lupa.lua51 import LuaRuntime

lua = LuaRuntime(unpack_returned_tuples=True)
model = ROOT / 'server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy'
for path in model.glob('*.lua'):
    lua.execute('package.preload[...] = assert(loadstring(select(2,...)))',
                'luci.model.ocserv_easy.' + path.stem, path.read_text(encoding='utf-8'))
lua.globals().TEST_TEMPLATE = (ROOT / 'server/openwrt/ocserv/files/ocserv.conf.template').read_text(encoding='utf-8')
lua.globals().TEST_GUARD = (ROOT / 'server/openwrt/luci-app-ocserv-easy/root/usr/share/ocserv-easy/guard.nft.in').read_text(encoding='utf-8')
cases = lua.execute((ROOT / 'tests/guard_transaction_tests.lua').read_text(encoding='utf-8'))
report = {'runtime': 'Lua 5.1', 'passed': len(cases), 'cases': [cases[i] for i in range(1, len(cases)+1)],
          'boundary': 'Real guard manager; simulated UCI, services and filesystem. No N1 modified.'}
output = ROOT / 'test-results/guard-transactions.json'
output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'passed': len(cases), 'report': str(output)}))
