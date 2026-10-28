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
    def worker(self, command, failures=0, permanent=False):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().failures = failures
        lua.globals().permanent = permanent
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
                    return calls>failures and {effect="unchanged"} or false
                end
            }
            package.loaded["luci.model.ocserv_easy.guard"]={work=function() sequence[#sequence+1]="guard" end}
            package.loaded["luci.model.ocserv_easy.guard_log"]={add=function(step,code) logs[#logs+1]=code end}
        ''')
        source = (RUNTIME / "usr/libexec/ocserv-easy-guard").read_text(encoding="utf-8")
        lua.execute(source.partition("\n")[2])
        return lua.globals()

    def test_service_events_are_filtered_and_use_one_shot_worker(self):
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
        '''
        script += "\n. " + shlex.quote((RUNTIME / "etc/init.d/ocserv-easy-guard").as_posix()) + "\nservice_triggers\n"
        output = subprocess.check_output([bash, "-s"], input=script, text=True)
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
        self.assertEqual(len(triggers), 3)
        for service, trigger in zip(("ocserv", "nikki", "momo"), triggers):
            event, expression, delay = trigger
            self.assertEqual(event, "instance.start")
            self.assertEqual(expression[:2], ["if", ["eq", "service", service]])
            self.assertEqual(expression[2], ["run_script", "/usr/bin/lua", "/usr/libexec/ocserv-easy-guard", "proxy"])
            self.assertEqual(delay, 3000)

    def test_startup_waits_for_busy_lease_then_exits(self):
        state = self.worker("proxy", failures=2)
        self.assertEqual((state.calls, state.polls, state.exitcode), (3, 2, 0))

    def test_wait_is_bounded_and_failure_is_logged(self):
        state = self.worker("proxy", failures=100)
        self.assertEqual((state.calls, state.polls, state.exitcode), (30, 30, 1))
        self.assertEqual(state.logs[1], "guard_proxy_failed")
        state = self.worker("proxy", permanent=True)
        self.assertEqual((state.calls, state.exitcode, state.logs[1]), (1, 1, "guard_proxy_template"))

    def test_restore_finishes_before_normal_proxy_sync_and_boot_stays_dormant(self):
        state = self.worker("worker")
        self.assertEqual(list(state.sequence.values()), ["recover", "guard", "proxy"])
        state = self.worker("boot")
        self.assertEqual((state.calls, state.exitcode), (0, 0))


if __name__ == "__main__":
    unittest.main()
