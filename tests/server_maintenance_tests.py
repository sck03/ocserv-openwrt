"""Exercise the actual Lua entry points used by installation and removal."""
from pathlib import Path
import unittest
from lupa.lua51 import LuaRuntime, LuaError

ROOT = Path(__file__).resolve().parents[1]
HELPERS = ROOT / 'server/openwrt/luci-app-ocserv-easy/root/usr/share/ocserv-easy'
FIREWALL = ROOT / 'server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy/firewall.lua'
LOGIC = ROOT / 'server/openwrt/ocserv/files/lua/logic.lua'


class CommandExit(Exception):
    def __init__(self, code): self.code = code


class MaintenanceTests(unittest.TestCase):
    def runtime(self, script, args):
        runtime = LuaRuntime(unpack_returned_tuples=True)
        runtime.globals().arg = runtime.table_from(args)
        runtime.globals().arg[0] = str(HELPERS / script).replace('\\', '/')
        output = []
        runtime.globals().print = lambda value: output.append(str(value))
        return runtime, output

    def network(self, args, with_logic=True):
        runtime, output = self.runtime('network.lua', args)
        runtime.execute('local path=...; package.preload["luci.model.ocserv_easy.firewall"]=function() return assert(loadfile(path))() end', str(FIREWALL))
        if with_logic:
            runtime.execute('local path=...; package.preload["ocserv.logic"]=function() return assert(loadfile(path))() end', str(LOGIC))
        runtime.execute((HELPERS / 'network.lua').read_text(encoding='utf-8'))
        return output

    def test_managed_names_work_before_installation_and_after_package_removal(self):
        self.assertEqual(set(self.network(['names'], with_logic=False)),
                         {'ocserv_vpn', 'ocserv_vpn_to_lan', 'ocserv_vpn_nat', 'ocserv_vpn_entry'})

    def test_candidate_contains_correct_pool_ports_and_device_list(self):
        commands = self.network(['create', '10.77.0.0', '255.255.255.0', '4443', '8443'])
        self.assertIn("set firewall.ocserv_vpn_nat.src_ip='10.77.0.0/24'", commands)
        self.assertIn("set firewall.ocserv_vpn_entry.dest_port='4443 8443'", commands)
        self.assertIn("add_list firewall.ocserv_vpn.device='vpns+'", commands)
        self.assertEqual(commands[-1], 'commit firewall')
        self.assertEqual(sum(line.startswith('set firewall.') and line.endswith('=zone') for line in commands), 1)
        same_port = self.network(['create', '10.77.0.0', '255.255.255.0', '4443', '4443'])
        self.assertIn("set firewall.ocserv_vpn_entry.dest_port='4443'", same_port)

    def test_invalid_network_and_ports_never_get_default_rules(self):
        for args in (['create', '10.77.0.0', '255.0.255.0', '4443', '4443'],
                     ['create', '10.77.0.999', '255.255.255.0', '4443', '4443'],
                     ['create', '10.77.0.0', '255.255.255.0', '0', '4443'],
                     ['create', '10.77.0.0', '255.255.255.0', '4443', "4443';bad"], ['unknown']):
            with self.subTest(args=args), self.assertRaises(LuaError): self.network(args)

    def test_bounded_entry_preserves_arguments_output_and_exit_status(self):
        runtime, output = self.runtime('run.lua', ['30', '/sbin/fixture', 'literal;not-a-shell', 'two words'])
        received = []
        def run(argv, seconds):
            received.append(([argv[index] for index in range(1, len(argv) + 1)], seconds))
            return 124, 'deadline reached\n'
        def finish(code): raise CommandExit(code)
        runtime.globals().run_command = run
        runtime.globals().capture = output.append
        runtime.globals().os.exit = finish
        runtime.execute('package.preload["ocserv.process"]=function() return {run=run_command} end; io.stdout={write=function(_,value) capture(value) end}')
        with self.assertRaises(CommandExit) as result:
            runtime.execute((HELPERS / 'run.lua').read_text(encoding='utf-8'))
        self.assertEqual(result.exception.code, 124)
        self.assertEqual(received, [(['/sbin/fixture', 'literal;not-a-shell', 'two words'], 30)])
        self.assertEqual(output, ['deadline reached\n'])


if __name__ == '__main__': unittest.main()
