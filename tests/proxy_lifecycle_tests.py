"""Exercise installed procd trigger output and the real one-shot Lua worker."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import unittest
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "server/openwrt/luci-app-ocserv-easy/root"


class ProxyLifecycleTests(unittest.TestCase):
    def worker(self, command, failures=0, permanent=False, provider=None):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().failures = failures
        lua.globals().permanent = permanent
        lua.globals().provider = provider
        lua.globals().arg = lua.table_from([command])
        lua.execute('''
            calls, polls, sequence, logs = 0, 0, {}, {}
            os.exit=function(code) exitcode=code end
            package.loaded["nixio"]={poll=function() polls=polls+1 end}
            package.loaded["luci.model.ocserv_easy.backend"]={
                recover=function() sequence[#sequence+1]="recover";return true end,
                sync_proxy=function()
                    calls=calls+1;sequence[#sequence+1]="proxy"
                    if permanent then error({code="guard_proxy_template"},0) end
                    return calls>failures and {effect="unchanged",proxy=provider} or false
                end
            }
            package.loaded["luci.model.ocserv_easy.guard"]={work=function() sequence[#sequence+1]="guard" end}
            package.loaded["luci.model.ocserv_easy.guard_log"]={add=function(step,code) logs[#logs+1]=code end}
        ''')
        source = (RUNTIME / "usr/libexec/ocserv-easy-guard").read_text(encoding="utf-8")
        lua.execute(source.partition("\n")[2])
        return lua.globals()

    def shell(self, body):
        bash = shutil.which("bash")
        if not bash and os.name == "nt":
            bash = str(Path("D:/msys64/usr/bin/bash.exe"))
        self.assertTrue(bash and Path(bash).exists(), "Bash is required to inspect installed procd triggers")
        script = '''
            json_add_array() { printf 'a\\n'; }
            json_close_array() { printf 'c\\n'; }
            json_add_string() { printf 's\\t%s\\n' "$2"; }
            json_add_int() { printf 'i\\t%s\\n' "$2"; }
            procd_open_trigger() { json_add_array; }
            procd_close_trigger() { json_close_array; }
            procd_add_raw_trigger() {
                json_add_array; json_add_string "" "$1"; delay=$2; shift 2
                json_add_array; json_add_array; json_add_string "" run_script
                for value in "$@"; do json_add_string "" "$value"; done
                json_close_array; json_close_array
                json_add_int "" "$delay"; json_close_array
            }
        '''
        script += "\n. " + shlex.quote((RUNTIME / "etc/init.d/ocserv-easy-guard").as_posix()) + "\n" + body
        return subprocess.check_output([bash, "-s"], input=script, text=True)

    def test_internal_instance_update_with_empty_data_dispatches_worker(self):
        # procd 58eb263d (OpenWrt 25.12) service_instance_update calls
        # trigger_event("instance.update", {}) without a service field.
        # instance.start is only ubus_notify, so the old trigger never ran.
        output = self.shell("service_triggers\n")
        stack, roots = [], []
        for line in output.splitlines():
            kind, _, value = line.partition("\t")
            if kind == "a":
                array = []
                (stack[-1] if stack else roots).append(array)
                stack.append(array)
            elif kind == "c":
                stack.pop()
            else:
                stack[-1].append(int(value) if kind == "i" else value)
        self.assertEqual(len(roots), 1)
        self.assertFalse(stack)
        triggers = roots[0]
        def dispatch(expr, data):
            if expr[0] == "if":
                _, (_, key, value), action = expr
                return dispatch(action, data) if data.get(key) == value else []
            if expr[0] == "run_script":
                return [expr[1:]]
            return [cmd for child in expr for cmd in dispatch(child, data)]

        commands = [cmd for event, expr, _ in triggers if event == "instance.update"
                    for cmd in dispatch(expr, {})]
        self.assertEqual(commands, [["/usr/bin/lua", "/usr/libexec/ocserv-easy-guard", "proxy"]])
        self.assertTrue(all(delay >= 3000 for _, _, delay in triggers))

    def test_start_after_existing_services_schedules_one_shot_catchup(self):
        output = self.shell('''
            procd_open_instance() { instance=$1; }
            procd_set_param() { printf '%s:%s\\n' "$instance" "$*"; }
            procd_close_instance() { :; }
            start_service
        ''')
        self.assertIn("proxy:command /usr/bin/lua /usr/libexec/ocserv-easy-guard proxy", output)
        self.assertNotIn("proxy:respawn", output)

    def test_startup_waits_for_busy_lease_then_exits(self):
        state = self.worker("proxy", failures=2)
        self.assertEqual((state.calls, state.polls, state.exitcode), (3, 2, 0))

    def test_wait_is_bounded_and_failure_is_logged(self):
        state = self.worker("proxy", failures=100)
        self.assertEqual((state.calls, state.polls, state.exitcode), (30, 30, 1))
        self.assertEqual(state.logs[1], "guard_proxy_failed")
        state = self.worker("proxy", permanent=True)
        self.assertEqual((state.calls, state.exitcode, state.logs[1]), (1, 1, "guard_proxy_template"))

    def test_already_connected_proxy_reports_success_without_restarting(self):
        state = self.worker("proxy", provider="nikki")
        self.assertEqual((state.calls, state.exitcode, state.logs[1]), (1, 0, "running"))
        state = self.worker("proxy")
        self.assertEqual(len(state.logs), 0)

    def test_restore_finishes_before_normal_proxy_sync_and_boot_stays_dormant(self):
        state = self.worker("worker")
        self.assertEqual(list(state.sequence.values()), ["recover", "guard", "proxy"])
        state = self.worker("boot")
        self.assertEqual((state.calls, state.exitcode), (0, 0))


if __name__ == "__main__":
    unittest.main()
