"""Exercise the real Lua command runner with deterministic pipe and clock boundaries."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
from lupa.lua51 import LuaRuntime


class ProcessTests(unittest.TestCase):
    def run_case(self, mode, exit_code=0):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.globals().mode = mode
        lua.globals().exit_code = exit_code
        lua.execute('''
            reads, polls, waits, reaped, closed, killed, now = 0, 0, 0, false, false, {}, 0
            forked, output_closed, tail_sent = false, false, false
            local input = {
                close=function() closed=true end,
                setblocking=function() return mode~="blocking_failure" end,
                read=function()
                    reads=reads+1
                    assert(reads<1000, "unbounded pipe drain")
                    if mode=="flood" then return string.rep("x",4096) end
                    if mode=="slow" then now=now+1; return "x" end
                    if mode=="tail_flood" and reaped then return string.rep("x",4096) end
                    if mode=="exact" and reads<=32 then return string.rep("x",4096) end
                    if mode=="eof" then return "" end
                    if mode=="normal" and reads==1 then return "hello" end
                    if mode=="normal" and reaped and reads==3 then return " tail" end
                    if mode=="read_failure" or (mode=="tail_failure" and reaped) then return nil,5,"I/O error" end
                    if mode=="delayed_tail" then
                        if polls==0 then return false,11,"Try again" end
                        if not tail_sent then tail_sent=true; return "late output" end
                    end
                    if mode=="inherited_idle" then return false,11,"Try again" end
                    if mode=="delayed_flood" then
                        if polls==0 then return false,11,"Try again" end
                        return string.rep("x",4096)
                    end
                    if reaped then return "" end
                    return false,11,"Try again"
                end
            }
            package.preload.nixio=function() return {
                pipe=function() return input,{close=function() output_closed=true end} end,
                fork=function() forked=true; return 42 end,
                gettimeofday=function() error("wall clock must not control deadlines") end,
                sysinfo=function() return {uptime=now} end,
                waitpid=function(pid,flag)
                    waits=waits+1
                    assert(not reaped,"child was waited on after being reaped")
                    if flag and mode=="wait_failure" then return nil,10,"No child processes" end
                    if flag and (mode=="idle" or mode=="flood" or mode=="slow" or mode=="eof" or mode=="poll_failure" or (mode=="interrupted" and polls==0)) then return false end
                    reaped=true
                    return pid,mode=="signal" and "signaled" or "exited",mode=="failure" and 7 or exit_code
                end,
                kill=function(pid) killed[#killed+1]=pid end,
                poll=function(fds)
                    polls=polls+1
                    if mode=="poll_failure" then return nil,5,"I/O error" end
                    if mode=="interrupted" then return nil,4,"Interrupted system call" end
                    if mode=="eof" then assert(#fds==0,"EOF pipe causes a busy loop") end
                    now=now+1; assert(polls<100,"unbounded wait")
                    return 0
                end,
                poll_flags=function() return 1 end
            } end
        ''')
        runner = lua.execute((ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy/process.lua").read_text(encoding="utf-8"))
        code, output = runner.run(lua.table_from(["fixture"]), 5)
        self.assertTrue(lua.globals().closed)
        self.assertTrue(lua.globals().output_closed)
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

    def test_closed_stdout_waits_without_spinning(self):
        code, output, state = self.run_case("eof")
        self.assertEqual((code, output), (124, ""))
        self.assertEqual(state.reads, 1)
        self.assertEqual(state.polls, 5)
        self.assertTrue(state.reaped)

    def test_descendant_output_after_parent_exit_is_bounded(self):
        code, output, state = self.run_case("tail_flood")
        self.assertEqual((code, len(output)), (124, 131072))
        self.assertEqual(list(state.killed.values()), [-42])

    def test_delayed_descendant_output_is_not_lost_after_parent_exit(self):
        code, output, state = self.run_case("delayed_tail")
        self.assertEqual((code, output), (0, "late output"))
        self.assertEqual(state.waits, 1)
        self.assertEqual(state.polls, 1)
        self.assertEqual(len(state.killed), 0)

    def test_inherited_pipe_without_output_obeys_deadline(self):
        code, output, state = self.run_case("inherited_idle")
        self.assertEqual((code, output), (124, ""))
        self.assertEqual(state.waits, 1)
        self.assertEqual(state.polls, 5)
        self.assertEqual(list(state.killed.values()), [-42])

    def test_delayed_output_preserves_parent_failure_status(self):
        code, output, state = self.run_case("delayed_tail", exit_code=7)
        self.assertEqual((code, output), (7, "late output"))
        self.assertEqual(state.waits, 1)
        self.assertEqual(len(state.killed), 0)

    def test_delayed_descendant_flood_still_obeys_output_budget(self):
        code, output, state = self.run_case("delayed_flood")
        self.assertEqual((code, len(output)), (124, 131072))
        self.assertEqual(state.waits, 1)
        self.assertEqual(list(state.killed.values()), [-42])

    def test_nonblocking_failure_closes_pipe_without_starting_child(self):
        code, output, state = self.run_case("blocking_failure")
        self.assertEqual((code, output), (125, ""))
        self.assertFalse(state.forked)
        self.assertEqual(state.reads, 0)

    def test_pipe_error_cannot_report_success(self):
        code, output, state = self.run_case("read_failure")
        self.assertEqual((code, output), (125, ""))
        self.assertTrue(state.reaped)
        self.assertEqual(list(state.killed.values()), [-42, 42])

    def test_tail_error_cannot_report_success(self):
        code, output, state = self.run_case("tail_failure")
        self.assertEqual((code, output), (125, ""))
        self.assertEqual(list(state.killed.values()), [-42])

    def test_wait_error_fails_without_polling(self):
        code, _, state = self.run_case("wait_failure")
        self.assertEqual(code, 125)
        self.assertEqual(state.polls, 0)
        self.assertEqual(list(state.killed.values()), [-42])

    def test_poll_error_fails_without_spinning(self):
        code, _, state = self.run_case("poll_failure")
        self.assertEqual(code, 125)
        self.assertEqual(state.polls, 1)
        self.assertTrue(state.reaped)

    def test_interrupted_poll_retries(self):
        code, _, state = self.run_case("interrupted")
        self.assertEqual(code, 0)
        self.assertEqual(state.polls, 1)
        self.assertEqual(len(state.killed), 0)

    def test_child_setup_failure_never_executes_command(self):
        for failure in ("session", "null", "stdin", "stdout", "stderr"):
            with self.subTest(failure=failure):
                lua = LuaRuntime(unpack_returned_tuples=True)
                lua.globals().failure = failure
                lua.execute('''
                    executed=false
                    os.exit=function(code) error("child exit "..code) end
                    local fd={close=function() end,setblocking=function() return true end}
                    package.preload.nixio=function() return {
                        pipe=function() return fd,fd end,
                        fork=function() return 0 end,
                        setsid=function() return failure~="session" end,
                        open=function() if failure~="null" then return fd end end,
                        stdin="stdin",stdout="stdout",stderr="stderr",
                        dup=function(_,target) return target~=failure end,
                        exec=function() executed=true end
                    } end
                ''')
                runner = lua.execute((ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy/process.lua").read_text(encoding="utf-8"))
                with self.assertRaisesRegex(Exception, "child exit 127"):
                    runner.run(lua.table_from(["fixture"]), 5)
                self.assertFalse(lua.globals().executed)


if __name__ == "__main__":
    unittest.main()
