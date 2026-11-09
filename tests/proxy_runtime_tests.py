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
        self.opened, self.closed, self.read_sizes = [], [], []
        self.read_failures = set()
        self.bytes_read = 0
        self.read_chunk = None
        self.settings = {("proxy", "tcp_mode"): "redirect", ("proxy", "udp_mode"): "tun",
                         ("routing", "tun_route_table"): "181"}

        def convert(value):
            if isinstance(value, dict):
                return self.lua.table_from({key: convert(item) for key, item in value.items()})
            if isinstance(value, list):
                return self.lua.table_from([convert(item) for item in value])
            return value

        self.convert = convert

        def open_file(path, _mode):
            if path not in self.files:
                return None, "No such file", 2
            self.opened.append(path)
            position = 0

            def read(_file, count):
                nonlocal position
                self.read_sizes.append(count)
                if path in self.read_failures:
                    return None, "Input/output error", 5
                if self.read_chunk:
                    count = min(count, self.read_chunk)
                chunk = self.files[path][position:position + count]
                position += len(chunk)
                self.bytes_read += len(chunk)
                return chunk or None

            return self.lua.table_from({"read": read, "close": lambda _file: self.closed.append(path)})

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
        globals_.open_fixture = open_file
        globals_.get_fixture = lambda _cursor, _name, section, key: self.settings.get((section, key))
        self.lua.execute('''
            package.loaded["nixio.fs"]={}
            package.loaded["nixio"]={}
            package.loaded["luci.jsonc"]={parse=parse_fixture,stringify=function(x)return '{"name":"'..x.name..'"}' end}
            package.loaded["luci.model.ocserv_easy.process"]={run=run_fixture}
            io.open=open_fixture
            cursor={get=get_fixture}
        ''')
        self.lua.execute('package.loaded["luci.model.ocserv_easy.logic"]=assert(loadstring(...))()',
                         (MODEL / "logic.lua").read_text(encoding="utf-8"))
        self.storage = self.lua.execute('local m=assert(loadstring(...))(); package.loaded["luci.model.ocserv_easy.storage"]=m; return m',
                                        (MODEL / "storage.lua").read_text(encoding="utf-8"))
        self.lua.execute('package.loaded["luci.model.ocserv_easy.proxy_registry"]=assert(loadstring(...))()',
                         (MODEL / "proxy_registry.lua").read_text(encoding="utf-8"))
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
        self.assertEqual(self.opened, [])

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

    def test_openclash_running_requires_its_own_named_procd_instance(self):
        self.lua.globals().run_fixture = lambda argv, timeout: (0, json.dumps({
            "nikki": {"instances": {"nikki": {"running": True}}}}))
        self.lua.execute('package.loaded["luci.model.ocserv_easy.process"].run=run_fixture')
        proxy = self.lua.execute((MODEL / "proxy.lua").read_text(encoding="utf-8"))
        self.assertFalse(proxy.running("openclash"))

    def test_managed_services_wait_for_valid_generated_dns_targets(self):
        for name in ("openclash", "nikki", "momo"):
            table = "fw4" if name == "openclash" else name
            chain_name = "openclash_dns_redirect" if name == "openclash" else "lan_dns_hijack"
            with self.subTest(provider=name):
                self.assertTrue(self.proxy.ready(name, False))
                self.assertFalse(self.proxy.ready(name, True))
                self.target(table, chain_name, {"map": "dns_ports"})
                self.assertFalse(self.proxy.ready(name, True))
                self.target(table, chain_name, 15053)
                self.assertFalse(self.proxy.ready(name, True))
                self.files["/proc/net/tcp"] = socket_row(15053)
                self.assertFalse(self.proxy.ready(name, True))
                self.files["/proc/net/udp"] = socket_row(15053, "07")
                self.assertTrue(self.proxy.ready(name, True))
                self.files["/proc/net/tcp"] = socket_row(15053, "01")
                self.assertFalse(self.proxy.ready(name, True))
                self.files["/proc/net/tcp"] = "header\n"
                self.files["/proc/net/udp"] = "header\n"

    def test_large_connection_tables_do_not_hide_dns_listeners(self):
        for name in ("openclash", "nikki", "momo"):
            with self.subTest(provider=name):
                table = "fw4" if name == "openclash" else name
                chain_name = "openclash_dns_redirect" if name == "openclash" else "lan_dns_hijack"
                self.target(table, chain_name, 15053)
                self.files["/proc/net/tcp"] = socket_row(443, "01") * 5000 + socket_row(15053)
                self.files["/proc/net/udp"] = socket_row(15053, "07")
                self.assertGreater(len(self.files["/proc/net/tcp"]), 262144)
                self.assertTrue(self.proxy.ready(name, True))
                self.assertEqual(self.opened, self.closed)

    def test_known_targets_stop_scanning_before_unrelated_connections(self):
        self.target("nikki", "lan_dns_hijack", 15053)
        self.files["/proc/net/tcp"] = socket_row(15053) + socket_row(443, "01") * 5000
        self.files["/proc/net/udp"] = socket_row(15053, "07") + socket_row(5353, "07") * 5000
        for _ in range(100):
            self.assertTrue(self.proxy.ready("nikki", True))
        self.assertEqual(self.opened, self.closed)
        self.assertEqual(len(self.opened), 200)
        self.assertLessEqual(self.bytes_read, 100 * 8192)
        self.assertLessEqual(max(self.read_sizes), 4096)
        self.assertFalse(any(path.endswith("6") for path in self.opened))

    def test_large_ipv6_tables_and_split_lines_preserve_listener_detection(self):
        self.target("momo", "lan_redirect", 28091)
        self.files["/proc/net/tcp6"] = socket_row(443, "01", True) * 3000 + socket_row(28091, ipv6=True).rstrip("\n")
        self.read_chunk = 37
        state = self.proxy.diagnostics(self.lua.globals().cursor, "momo")
        self.assertTrue(state.targets.lan_redirect.ports[1].tcp)
        self.assertEqual(self.opened, self.closed)

    def test_socket_read_errors_and_scan_budgets_report_unknown(self):
        self.target("nikki", "lan_redirect", 28091)
        for failure in ("read", "bytes", "line"):
            with self.subTest(failure=failure):
                self.read_failures.clear()
                self.files["/proc/net/tcp"] = "header\n"
                if failure == "read":
                    self.read_failures.add("/proc/net/tcp")
                elif failure == "bytes":
                    self.files["/proc/net/tcp"] = socket_row(443, "01") * 60000
                    self.assertGreater(len(self.files["/proc/net/tcp"]), 4194304)
                else:
                    self.files["/proc/net/tcp"] = "x" * 4097 + "\n"
                state = self.proxy.diagnostics(self.lua.globals().cursor, "nikki")
                self.assertIsNone(state.targets.lan_redirect.ports[1].tcp)
                self.assertEqual(self.opened, self.closed)
        self.assertLessEqual(max(self.read_sizes), 4096)

    def test_partial_observations_do_not_turn_unseen_targets_into_absent(self):
        self.target("nikki", "lan_dns_hijack", 15053)
        self.target("nikki", "lan_redirect", 28091)
        self.files["/proc/net/tcp"] = socket_row(15053)
        self.read_failures.add("/proc/net/tcp6")
        state = self.proxy.diagnostics(self.lua.globals().cursor, "nikki")
        self.assertTrue(state.targets.lan_dns_hijack.ports[1].tcp)
        self.assertIsNone(state.targets.lan_redirect.ports[1].tcp)
        self.assertEqual(self.opened, self.closed)

    def test_line_scanner_preserves_boundaries_empty_lines_and_final_tail(self):
        data = "a" * 4096 + "\n\ntail"
        self.files["/scan"] = data
        for count in (None, 1, 37, 4096):
            self.read_chunk = count
            lines = []
            self.assertTrue(self.storage.scan_lines("/scan", lines.append, len(data)))
            self.assertEqual(lines, ["a" * 4096, "", "tail"])
        self.files["/scan"] = ""
        self.assertTrue(self.storage.scan_lines("/scan", lambda line: self.fail(line), 0))
        self.assertIsNone(self.storage.scan_lines("/missing", lambda line: self.fail(line)))
        self.assertEqual(self.opened, self.closed)

    def test_line_scanner_closes_on_limits_callback_failure_and_early_stop(self):
        checked_scan = self.lua.eval('''function(path, visit, limit)
            local ok, err=pcall(require("luci.model.ocserv_easy.storage").scan_lines,path,visit,limit)
            return ok, type(err)=="table" and err.code or err
        end''')
        for data, limit in (("abcd", 3), ("a" * 4097, 8192), ("a" * 4097 + "\n", 8192)):
            self.files["/scan"] = data
            self.assertEqual(checked_scan("/scan", lambda line: None, limit), (False, "file_too_large"))
        self.files["/scan"] = "first\nsecond\n"
        fail = self.lua.eval('function() error({code="visitor_failed"},0) end')
        self.assertEqual(checked_scan("/scan", fail, 4096), (False, "visitor_failed"))
        lines = []
        self.assertTrue(self.storage.scan_lines("/scan", lambda line: (lines.append(line), False)[1]))
        self.assertEqual(lines, ["first"])
        self.assertEqual(self.opened, self.closed)

    def test_stopped_momo_does_not_report_nikkis_shared_tun(self):
        self.lua.globals().run_fixture = lambda argv, timeout: (0, json.dumps({
            "momo": {"instances": {"momo": {"running": False}}}}))
        self.lua.execute('package.loaded["luci.model.ocserv_easy.process"].run=run_fixture')
        proxy = self.lua.execute((MODEL / "proxy.lua").read_text(encoding="utf-8"))
        state = proxy.diagnostics(self.lua.globals().cursor, "momo")
        self.assertFalse(state.running)
        self.assertIsNone(state.tun)
        self.assertEqual(len(state.targets), 0)

    def test_detected_providers_without_adapters_are_explicitly_unsupported(self):
        for name, label in (("passwall", "PassWall"), ("passwall2", "PassWall 2"),
                            ("homeproxy", "HomeProxy"), ("shadowsocksr", "SSR Plus+")):
            state = self.proxy.diagnostics(self.lua.globals().cursor, name)
            self.assertEqual(state.label, label)
            self.assertTrue(state.running)
            self.assertFalse(state.supported)
            self.assertIsNone(state.ingress_ready)
            self.assertEqual(len(state.targets), 0)


if __name__ == "__main__":
    unittest.main()
