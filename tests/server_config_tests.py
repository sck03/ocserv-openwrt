"""Exercise shared startup rendering with isolated UCI and file interfaces."""
from pathlib import Path
import re
import unittest

from lupa.lua51 import LuaRuntime
from lua_modules import install_modules

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "server/openwrt/ocserv/files"


def directives(text):
    return [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]


class ConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lua = LuaRuntime(unpack_returned_tuples=True)
        cls.logic = cls.lua.execute((ROOT / "server/openwrt/ocserv/files/lua/logic.lua").read_text(encoding="utf-8"))
        cls.template = (PACKAGE / "ocserv.conf.template").read_text(encoding="utf-8")
        install_modules(cls.lua)
        cls.lua.execute('''
            package.loaded["nixio.fs"] = {}
            package.loaded["ocserv.process"] = {}
            package.loaded["ocserv.storage"] = {read=function(path)
                if path:match("template$") then return TEMPLATE end
                return ""
            end}
        ''')
        cls.lua.globals().TEMPLATE = cls.template
        cls.runtime = cls.lua.eval('require("ocserv.runtime")')
        cls.cursor = cls.lua.eval('''function(config, dns, routes)
            return {load=function() return true end,
                get_all=function() return config end,
                get_first=function() return "lan" end,
                foreach=function(_, _, kind, visit)
                    for _, row in ipairs(kind=="dns" and dns or kind=="routes" and routes or {}) do visit(row) end
                end}
        end''')
        cls.defaults = dict(re.findall(r"option\s+(\w+)\s+'([^']*)'", (PACKAGE / "config").read_text()))
        cls.defaults.pop("ip", None)  # DNS belongs to a separate UCI section.

    def render(self, config, dns=("1.1.1.1",), routes=()):
        return self.logic.render(self.template, "", self.lua.table_from(config),
                                 self.lua.table_from([self.lua.table_from({"ip": value}) for value in dns]),
                                 self.lua.table_from([self.lua.table_from({"ip": ip, "netmask": mask}) for ip, mask in routes]),
                                 self.lua.table_from({"domain": "lan"}))

    def startup(self, configs, dns=("1.1.1.1",), routes=()):
        dns_rows = self.lua.table_from([self.lua.table_from({"ip": value}) for value in dns])
        route_rows = self.lua.table_from([self.lua.table_from({"ip": ip, "netmask": mask}) for ip, mask in routes])
        return [self.runtime.render(self.cursor(self.lua.table_from(config), dns_rows, route_rows))[0]
                for config in configs]

    def test_shipped_ipv4_defaults_match_luci(self):
        actual, = self.startup([self.defaults])
        self.assertEqual(directives(actual), directives(self.render(self.defaults)))
        for value in ("ipv4-network = 10.77.0.0", "ipv4-netmask = 255.255.255.0", "dns = 1.1.1.1", "tcp-port = 4443", "udp-port = 4443"):
            self.assertIn(value, directives(actual))
        self.assertFalse(any(line.startswith(("ipv6-network", "listen-host =")) for line in directives(actual)))

    def test_public_address_does_not_change_lan_configuration(self):
        baseline = self.render(self.defaults)
        for endpoint in ("https://192.168.19.253:4443", "https://vpn.example.com:4443", "https://[2001:db8::10]:4443"):
            config = dict(self.defaults, easy_public_url=endpoint)
            self.assertEqual(self.render(config), baseline)
            self.assertEqual(directives(self.startup([config])[0]), directives(baseline))

    def test_ipv6_toggle_does_not_inherit_disabled_state(self):
        dual = dict(self.defaults, ip6addr="fd77::/64")
        configs = [self.defaults, dual, self.defaults, dual]
        for config, actual in zip(configs, self.startup(configs)):
            self.assertEqual(directives(actual), directives(self.render(config)))

    def test_dual_stack_preserves_custom_ipv4_dns_and_routes(self):
        config = dict(self.defaults, ipaddr="10.88.0.0", netmask="255.255.254.0", ip6addr="fd77::/64", udp_port="4444")
        dns = ("10.88.0.1", "fd77::1")
        routes = (("192.168.19.0", "255.255.255.0"), ("fd19::", "64"))
        actual, = self.startup([config], dns, routes)
        self.assertEqual(directives(actual), directives(self.render(config, dns, routes)))


if __name__ == "__main__":
    unittest.main()
