"""Compare the shipped init script with LuCI without touching a router or host routes."""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "server/openwrt/ocserv/files"
SH = shutil.which("sh")


def directives(text):
    return [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]


class ConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lua = LuaRuntime(unpack_returned_tuples=True)
        cls.logic = cls.lua.execute((ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy/logic.lua").read_text(encoding="utf-8"))
        cls.template = (PACKAGE / "ocserv.conf.template").read_text(encoding="utf-8")
        source = (PACKAGE / "ocserv.init").read_text(encoding="utf-8")
        cls.functions = source[source.index("setup_config() {"):source.index("initcerts() (")]
        cls.defaults = dict(re.findall(r"option\s+(\w+)\s+'([^']*)'", (PACKAGE / "config").read_text()))
        cls.defaults.pop("ip", None)  # DNS belongs to a separate UCI section.

    def render(self, config, dns=("1.1.1.1",), routes=()):
        return self.logic.render(self.template, "", self.lua.table_from(config),
                                 self.lua.table_from([self.lua.table_from({"ip": value}) for value in dns]),
                                 self.lua.table_from([self.lua.table_from({"ip": ip, "netmask": mask}) for ip, mask in routes]),
                                 self.lua.table_from({"domain": "lan"}))

    def shell(self, configs, dns=("1.1.1.1",), routes=()):
        if not SH:
            self.skipTest("sh is needed to exercise the packaged init script")
        with tempfile.TemporaryDirectory(prefix="bulijie-config-") as directory:
            root = Path(directory)
            (root / "etc").mkdir()
            (root / "etc/ocserv.conf.template").write_text(self.template, encoding="utf-8")
            source = self.functions.replace("/etc/ocserv/ocserv.conf.", "./etc/ocserv.conf.")
            source = source.replace("/var/etc/ocserv.conf", "./runtime/ocserv.conf").replace("mkdir -p /var/etc", "mkdir -p ./runtime")
            script = 'uci() { printf "%s\\n" lan; }\n' + source
            for index, config in enumerate(configs):
                values = {"config." + key: value for key, value in config.items()}
                values.update({f"dns{i}.ip": value for i, value in enumerate(dns)})
                for i, (ip, mask) in enumerate(routes):
                    values.update({f"route{i}.ip": ip, f"route{i}.netmask": mask})
                script += 'config_get() {\nlocal value="${4-}"\ncase "$2.$3" in\n'
                for key, value in values.items():
                    script += f"{shlex.quote(key)}) value={shlex.quote(value)};;\n"
                script += 'esac\nexport "$1=$value"\n}\nsetup_config config\n'
                for i in range(len(routes)):
                    script += f"setup_routes route{i}\n"
                for i in range(len(dns)):
                    script += f"setup_dns dns{i}\n"
                script += f"cp runtime/ocserv.conf result{index}.conf\n"
            (root / "check.sh").write_text(script, encoding="utf-8", newline="\n")
            env = os.environ.copy()
            # A developer shell must not supply state to the service fixture.
            for key in ("enable_ipv6", "authsuffix"):
                env.pop(key, None)
            result = subprocess.run([SH, "check.sh"], cwd=root, env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            return [(root / f"result{i}.conf").read_text(encoding="utf-8") for i in range(len(configs))]

    def test_shipped_ipv4_defaults_match_luci(self):
        actual, = self.shell([self.defaults])
        self.assertEqual(directives(actual), directives(self.render(self.defaults)))
        for value in ("ipv4-network = 10.77.0.0", "ipv4-netmask = 255.255.255.0", "dns = 1.1.1.1", "tcp-port = 4443", "udp-port = 4443"):
            self.assertIn(value, directives(actual))
        self.assertFalse(any(line.startswith(("ipv6-network", "listen-host =")) for line in directives(actual)))

    def test_public_address_does_not_change_lan_configuration(self):
        baseline = self.render(self.defaults)
        for endpoint in ("https://192.168.19.253:4443", "https://vpn.example.com:4443", "https://[2001:db8::10]:4443"):
            config = dict(self.defaults, easy_public_url=endpoint)
            self.assertEqual(self.render(config), baseline)
            self.assertEqual(directives(self.shell([config])[0]), directives(baseline))

    def test_ipv6_toggle_does_not_inherit_disabled_state(self):
        dual = dict(self.defaults, ip6addr="fd77::/64")
        configs = [self.defaults, dual, self.defaults, dual]
        for config, actual in zip(configs, self.shell(configs)):
            self.assertEqual(directives(actual), directives(self.render(config)))

    def test_dual_stack_preserves_custom_ipv4_dns_and_routes(self):
        config = dict(self.defaults, ipaddr="10.88.0.0", netmask="255.255.254.0", ip6addr="fd77::/64", udp_port="4444")
        dns = ("10.88.0.1", "fd77::1")
        routes = (("192.168.19.0", "255.255.255.0"), ("fd19::", "64"))
        actual, = self.shell([config], dns, routes)
        self.assertEqual(directives(actual), directives(self.render(config, dns, routes)))


if __name__ == "__main__":
    unittest.main()
