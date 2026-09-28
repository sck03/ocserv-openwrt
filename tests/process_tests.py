"""Exercise the real Lua command runner with deterministic pipe and clock boundaries."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".tools/python-test"))
from lupa.lua51 import LuaRuntime


class ProcessTests(unittest.TestCase):
    def run_case(self, mode):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().mode = mode
        lua.execute('''
            reads, polls, reaped, closed, killed, now = 0, 0, false, false, {}, 0
            local input = {
                close=function() closed=true end,
                setblocking=function() end,
                read=function()
                    reads=reads+1
                    assert(reads<1000, "unbounded pipe drain")
                    if mode=="flood" then return string.rep("x",4096) end
                    if mode=="slow" then now=now+1; return "x" end
                    if mode=="tail_flood" and reaped then return string.rep("x",4096) end
                    if mode=="exact" and reads<=32 then return string.rep("x",4096) end
                    if mode=="normal" and reads==1 then return "hello" end
                    if mode=="normal" and reaped and reads==3 then return " tail" end
                    return nil
                end
            }
            package.preload.nixio=function() return {
                pipe=function() return input,{close=function() end} end,
                fork=function() return 42 end,
                gettimeofday=function() return now end,
                waitpid=function(pid,flag)
                    if flag and (mode=="idle" or mode=="flood" or mode=="slow") then return nil end
                    reaped=true
                    return pid,mode=="signal" and "signaled" or "exited",mode=="failure" and 7 or 0
                end,
                kill=function(pid) killed[#killed+1]=pid end,
                poll=function() polls=polls+1; now=now+1; assert(polls<100,"unbounded wait") end,
                poll_flags=function() return 1 end
            } end
        ''')
        runner = lua.execute((ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy/process.lua").read_text(encoding="utf-8"))
        code, output = runner.run(lua.table_from(["fixture"]), 5)
        self.assertTrue(lua.globals().closed)
        return code, output, lua.globals()

    def test_normal_exit_preserves_tail(self):
        code, output, state = self.run_case("normal")
        self.assertEqual((code, output), (0, "hello tail"))
        self.assertEqual(len(state.killed), 0)

    def test_failure_exit(self):
        self.assertEqual(self.run_case("failure")[0], 7)

    def test_signal_exit(self):
        self.assertEqual(self.run_case("signal")[0], 128)

    def test_exact_output_limit_is_allowed(self):
        code, output, _ = self.run_case("exact")
        self.assertEqual((code, len(output)), (0, 131072))

    def test_continuous_output_is_bounded_and_reaped(self):
        code, output, state = self.run_case("flood")
        self.assertEqual((code, len(output)), (124, 131072))
        self.assertTrue(state.reaped)
        self.assertEqual(list(state.killed.values()), [-42, 42])

    def test_slow_continuous_output_obeys_deadline(self):
        code, output, state = self.run_case("slow")
        self.assertEqual((code, len(output)), (124, 5))
        self.assertTrue(state.reaped)

    def test_silent_child_obeys_deadline(self):
        code, output, state = self.run_case("idle")
        self.assertEqual((code, output), (124, ""))
        self.assertTrue(state.reaped)

    def test_descendant_output_after_parent_exit_is_bounded(self):
        code, output, state = self.run_case("tail_flood")
        self.assertEqual((code, len(output)), (124, 131072))
        self.assertEqual(list(state.killed.values()), [-42])


if __name__ == "__main__":
    unittest.main()
