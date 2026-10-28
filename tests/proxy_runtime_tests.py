"""Check proxy diagnostics against generated nft JSON and kernel socket formats."""
import json
from pathlib import Path
import unittest
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy"


def socket_row(port, state="0A", ipv6=False):
    address = "0" * (32 if ipv6 else 8)
    return f"  0: {address}:{port:04X} {address}:0000 {state} 00000000:00000000 00:00000000 00000000 0 0 1\n"


def chain(port, kind="redirect"):
    return {"nftables": [{"rule": {"expr": [{kind: {"port": port}}]}}]}


class ProxyRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.responses = {}
        self.commands = []
        self.files = {"/proc/net/" + p: "header\n" for p in ("tcp", "tcp6", "udp", "udp6")}
        self.settings = {("proxy", "tcp_mode"): "redirect", ("proxy", "udp_mode"): "tun",
                         ("routing", "tun_route_table"): "181"}

        def convert(value):
            if isinstance(value, dict):
                return self.lua.table_from({key: convert(item) for key, item in value.items()})
            if isinstance(value, list):
                return self.lua.table_from([convert(item) for item in value])
            return value

        self.convert = convert

        def run(argv, _timeout):
            args = tuple(argv.values())
            self.commands.append(args)
            if args[:4] == ("/bin/ubus", "call", "service", "list"):
                name = json.loads(args[4])["name"]
                return 0, json.dumps({name: {"instances": {name: {"running": True}}}})
            if args[:3] == ("/usr/sbin/nft", "list", "set"):
                return 0, 'elements = { "vpns*" }'
            return self.responses.get(args, (1, "unavailable"))

        globals_ = self.lua.globals()
        globals_.run_fixture = run
        globals_.parse_fixture = lambda raw: convert(json.loads(raw))
        globals_.read_fixture = lambda path: self.files.get(path)
        globals_.get_fixture = lambda _cursor, _name, section, key: self.settings.get((section, key))
        self.lua.execute('''
            package.loaded["nixio.fs"]={}
            package.loaded["luci.jsonc"]={parse=parse_fixture,stringify=function(x)return '{"name":"'..x.name..'"}' end}
            package.loaded["luci.model.ocserv_easy.process"]={run=run_fixture}
            package.loaded["luci.model.ocserv_easy.storage"]={read=read_fixture}
            cursor={get=get_fixture}
        ''')
        self.lua.execute('package.loaded["luci.model.ocserv_easy.logic"]=assert(loadstring(...))()',
                         (MODEL / "logic.lua").read_text(encoding="utf-8"))
        self.proxy = self.lua.execute((MODEL / "proxy.lua").read_text(encoding="utf-8"))

    def target(self, name, table, port, kind="redirect"):
        self.responses[("/usr/sbin/nft", "-j", "list", "chain", "inet", name, table)] = (0, json.dumps(chain(port, kind)))

    def test_modified_ports_are_taken_from_generated_rules_for_both_providers(self):
        for name in ("nikki", "momo"):
            with self.subTest(provider=name):
                self.target(name, "lan_dns_hijack", 15053)
                self.target(name, "lan_redirect", 28091)
                self.files["/proc/net/tcp6"] = socket_row(28091, ipv6=True) + socket_row(15053, ipv6=True)
                self.files["/proc/net/udp"] = socket_row(15053, "07")
                state = self.proxy.diagnostics(self.lua.globals().cursor, name)
                self.assertTrue(state.running and state.ingress_ready)
                dns = state.targets.lan_dns_hijack.ports[1]
                redir = state.targets.lan_redirect.ports[1]
                self.assertEqual((dns.port, dns.tcp, dns.udp), (15053, True, True))
                self.assertEqual((redir.port, redir.tcp, redir.udp), (28091, True, False))

    def test_mixed_port_and_established_connections_do_not_satisfy_redirect_listener(self):
        self.target("nikki", "lan_redirect", 28091)
        self.files["/proc/net/tcp"] = socket_row(20802) + socket_row(28091, "01")
        state = self.proxy.diagnostics(self.lua.globals().cursor, "nikki")
        self.assertFalse(state.targets.lan_redirect.ports[1].tcp)

    def test_tproxy_uses_its_generated_port_and_udp_listener(self):
        self.settings[("proxy", "udp_mode")] = "tproxy"
        self.target("momo", "lan_tproxy", 32092, "tproxy")
        self.files["/proc/net/udp6"] = socket_row(32092, "07", ipv6=True)
        state = self.proxy.diagnostics(self.lua.globals().cursor, "momo")
        self.assertTrue(state.targets.lan_tproxy.ports[1].udp)
        self.assertIsNone(state.tun)

    def test_unreadable_socket_tables_are_unknown_instead_of_missing_listeners(self):
        self.target("nikki", "lan_redirect", 28091)
        del self.files["/proc/net/tcp"]
        state = self.proxy.diagnostics(self.lua.globals().cursor, "nikki")
        self.assertIsNone(state.targets.lan_redirect.ports[1].tcp)

    def test_missing_chains_report_unavailable_without_guessing_default_ports(self):
        state = self.proxy.diagnostics(self.lua.globals().cursor, "momo")
        self.assertFalse(state.targets.lan_redirect.available)
        self.assertEqual(len(state.targets.lan_redirect.ports), 0)

    def test_duplicate_and_nonliteral_targets_do_not_expose_rule_data(self):
        data = chain(28091)
        data["nftables"] += chain(28091)["nftables"] + chain({"map": "unsupported"})["nftables"]
        data["nftables"][0]["rule"]["comment"] = "subscription-secret"
        args = ("/usr/sbin/nft", "-j", "list", "chain", "inet", "nikki", "lan_redirect")
        self.responses[args] = (0, json.dumps(data))
        state = self.proxy.diagnostics(self.lua.globals().cursor, "nikki")
        self.assertEqual(len(state.targets.lan_redirect.ports), 1)
        self.assertIsNone(state.targets.lan_redirect.comment)

    def test_custom_tun_table_and_device_are_reported_from_live_routes(self):
        self.responses[("/sbin/ip", "-j", "-4", "route", "show", "table", "181")] = (
            0, json.dumps([{"dst": "default", "dev": "custom-tun"}]))
        self.responses[("/sbin/ip", "-j", "-4", "rule", "show")] = (
            0, json.dumps([{"table": 181, "fwmark": "0x91"}]))
        state = self.proxy.diagnostics(self.lua.globals().cursor, "momo")
        self.assertEqual((state.tun.table, state.tun.device, state.tun.rule), (181, "custom-tun", True))
        self.responses[("/sbin/ip", "-j", "-4", "rule", "show")] = (0, "[]")
        self.assertFalse(self.proxy.diagnostics(self.lua.globals().cursor, "momo").tun.rule)

    def test_missing_tun_option_uses_the_upstream_default_table(self):
        del self.settings[("routing", "tun_route_table")]
        self.responses[("/sbin/ip", "-j", "-4", "route", "show", "table", "81")] = (
            0, json.dumps([{"dst": "default", "dev": "momo"}]))
        state = self.proxy.diagnostics(self.lua.globals().cursor, "momo")
        self.assertEqual((state.tun.table, state.tun.device), (81, "momo"))

    def test_invalid_route_table_is_not_used_as_a_command_argument(self):
        self.settings[("routing", "tun_route_table")] = "--help"
        state = self.proxy.diagnostics(self.lua.globals().cursor, "momo")
        self.assertIsNone(state.tun.table)
        self.assertFalse(any(args[0] == "/sbin/ip" for args in self.commands))


if __name__ == "__main__":
    unittest.main()
