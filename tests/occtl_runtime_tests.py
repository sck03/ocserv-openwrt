"""Real Lua process/occtl modules with chunked pipes and actual JSON text."""
import json
from pathlib import Path
import unittest

from lupa.lua51 import LuaRuntime
from lua_json import install

ROOT = Path(__file__).resolve().parents[1]
MODULES = ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy"


class OcctlRuntimeTests(unittest.TestCase):
    def runtime(self, output, chunk=4096, exit_code=0, delay=0, pause_read=0):
        lua = LuaRuntime(unpack_returned_tuples=True)
        install(lua)
        lua.globals().output = output
        lua.globals().chunk_size = chunk
        lua.globals().exit_code = exit_code
        lua.globals().read_delay = delay
        lua.globals().pause_read = pause_read
        lua.execute('''
            reads, closed, reaped, kills, now = 0, 0, false, 0, 0
            decoded_rows, max_decode_bytes = 0, 0
            local offset = 1
            package.preload.nixio = function() return {
                pipe = function()
                    offset = 1
                    reaped = false
                    return {
                    setblocking = function() return true end,
                    close = function() closed = closed + 1 end,
                    read = function(_, count)
                        reads = reads + 1
                        now = now + read_delay
                        if reads == pause_read or reads == pause_read + 1 and pause_read > 0 then
                            return false, 11, "Try again"
                        end
                        local data = output:sub(offset, offset + math.min(count, chunk_size) - 1)
                        offset = offset + #data
                        return data
                    end
                }, {close = function() closed = closed + 1 end} end,
                fork = function() return 42 end,
                sysinfo = function() return {uptime = now} end,
                waitpid = function()
                    assert(not reaped, "child already reaped")
                    reaped = true; return 42, "exited", exit_code
                end,
                kill = function() kills = kills + 1 end,
                poll_flags = function() return 1 end,
                poll = function() now = now + 1; return 0 end
            } end
            package.preload["luci.jsonc"] = function() return {
                parse = function(raw)
                    decoded_rows = decoded_rows + 1
                    max_decode_bytes = math.max(max_decode_bytes, #raw)
                    local ok, value = pcall(TEST_JSON_DECODE, raw)
                    return ok and value or nil
                end
            } end
        ''')
        for path in MODULES.glob("*.lua"):
            lua.execute('package.preload[...] = assert(loadstring(select(2, ...)))',
                        "luci.model.ocserv_easy." + path.stem, path.read_text(encoding="utf-8"))
        return lua, lua.eval('require("luci.model.ocserv_easy.occtl")')

    def test_online_list_larger_than_generic_command_budget(self):
        rows = [{"ID": i + 1, "Username": f"user{i}", "IPv4": f"10.77.{i // 250}.{i % 250 + 2}",
                 "Routes": [f"10.{n}.0.0/16" for n in range(128)]} for i in range(96)]
        raw = json.dumps(rows)
        self.assertGreater(len(raw), 131072)
        lua, occtl = self.runtime(raw)
        result = occtl.online()
        self.assertFalse(result.online_error)
        self.assertEqual(len(result.online), 96)
        self.assertEqual(result.online[96].name, "user95")
        self.assertIsNone(result.online[1].Routes)
        self.assertEqual(lua.globals().closed, 2)
        self.assertTrue(lua.globals().reaped)
        self.assertEqual(lua.globals().decoded_rows, 96)
        self.assertLess(lua.globals().max_decode_bytes, 4096)

    def test_large_cached_session_list_can_confirm_absence(self):
        raw = json.dumps([{"Username": f"other{i}", "User-Agent": "x" * 256} for i in range(512)])
        self.assertGreater(len(raw), 131072)
        _, occtl = self.runtime(raw)
        self.assertTrue(occtl.absent("revoked"))

    def test_streaming_waits_for_delayed_tail_after_parent_exit(self):
        raw = json.dumps([{"ID": 1, "Username": "user"}, {"ID": 2, "Username": "other"}])
        lua, occtl = self.runtime(raw, chunk=7, pause_read=3)
        result = occtl.online()
        self.assertFalse(result.online_error)
        self.assertEqual(len(result.online), 2)
        self.assertEqual(lua.globals().closed, 2)
        self.assertTrue(lua.globals().reaped)
        self.assertEqual(lua.globals().kills, 0)

    def test_chunk_boundaries_preserve_nested_escaped_and_unicode_fields(self):
        row = {"ID": 7, "Username": 'escape"\\null用户', "Groupname": "staff",
               "Routes": ["10.0.0.0/8", {"nested": [True, False, '}{[]\\"']}],
               "Last connected at": "now"}
        raw = json.dumps([row, dict(row, ID=8)], ensure_ascii=False)
        for chunk in (1, 2, 3, 7, 31, 4096):
            with self.subTest(chunk=chunk):
                _, occtl = self.runtime(raw, chunk)
                result = occtl.online()
                self.assertFalse(result.online_error)
                self.assertEqual(len(result.online), 2)
                self.assertEqual(result.online[1].name, row["Username"])

    def test_incomplete_invalid_or_trailing_data_never_publish_partial_rows(self):
        row = '{"ID":1,"Username":"valid"}'
        for raw in ("", "{}", "[", "[" + row, "[" + row + ",]", "[" + row + "]x",
                    "[" + row + "][{}]", "[" + row + ",null]", "[" + row + ",false]",
                    "[" + row + ",{broken}]", "[" + row + ',{"Username":"unfinished\\',
                    "[" + row + ',{"ID":2,"Username":"bad","extra":null}]',
                    "[" + row + ',{"ID":2,"Username":"bad","extra":[null]}]',
                    "[" + row + '\v]', "[" + row + ',{"ID":2,"Username":"bad","extra":]}]'):
            with self.subTest(raw=raw):
                lua, occtl = self.runtime(raw, 7)
                result = occtl.online()
                self.assertTrue(result.online_error)
                self.assertEqual(len(result.online), 0)
                self.assertFalse(occtl.absent("absent"))
                self.assertEqual(lua.globals().closed, 4)

    def test_display_fields_truncate_only_at_utf8_boundaries(self):
        fields = {"Username": ("name", 64), "Groupname": ("group", 128),
                  "Remote IP": ("ip", 128), "IPv4": ("vpn_ip", 128),
                  "Last connected at": ("since", 128), "State": ("state", 128)}
        for character in ("é", "中", "😀"):
            width = len(character.encode("utf-8"))
            for remaining in range(width + 1):
                with self.subTest(character=character, remaining=remaining):
                    row = {"ID": 1}
                    expected = {}
                    for source, (target, limit) in fields.items():
                        value = "a" * (limit - remaining) + character + "tail"
                        row[source] = value
                        expected[target] = value.encode("utf-8")[:limit].decode("utf-8", errors="ignore")
                    lua, occtl = self.runtime(json.dumps([row], ensure_ascii=False), chunk=7)
                    result = occtl.online()
                    self.assertFalse(result.online_error)
                    # The same JSON bridge used by cache/publication must accept
                    # the projected row, not merely the original command text.
                    encoded = lua.globals().TEST_JSON_ENCODE(result.online[1])
                    displayed = json.loads(encoded)
                    for field, value in expected.items():
                        self.assertEqual(displayed[field], value)

    def test_empty_ipv4_uses_available_ipv6(self):
        for ipv4 in (None, "", False, 0):
            with self.subTest(ipv4=ipv4):
                row = {"ID": 1, "Username": "user", "IPv6": "fd77::2"}
                if ipv4 is not None:
                    row["IPv4"] = ipv4
                _, occtl = self.runtime(json.dumps([row]))
                self.assertEqual(occtl.online().online[1].vpn_ip, "fd77::2")

    def test_empty_array_and_json_whitespace_are_valid(self):
        _, occtl = self.runtime(" \t[\r\n] \n", 1)
        result = occtl.online()
        self.assertFalse(result.online_error)
        self.assertEqual(len(result.online), 0)

    def test_row_budget_has_exact_boundary(self):
        prefix = '{"ID":1,"Username":"user","unused":"'
        for size in (65536, 65537):
            raw = "[" + prefix + "x" * (size - len(prefix) - 2) + '"}]'
            lua, occtl = self.runtime(raw)
            result = occtl.online()
            self.assertEqual(bool(result.online_error), size > 65536)
            self.assertEqual(lua.globals().closed, 2)
            self.assertTrue(lua.globals().reaped)

    def test_online_count_budget_accepts_configured_maximum(self):
        for count in (4096, 4097):
            raw = json.dumps([{"ID": i + 1, "Username": "user"} for i in range(count)])
            _, occtl = self.runtime(raw)
            result = occtl.online()
            self.assertEqual(bool(result.online_error), count > 4096)
            self.assertEqual(len(result.online), count if count == 4096 else 0)

    def test_reconnect_sessions_can_outnumber_online_limit(self):
        raw = json.dumps([{"Username": "other"}] * 4097)
        _, occtl = self.runtime(raw)
        self.assertTrue(occtl.absent("revoked"))

    def test_revocation_never_accepts_a_present_or_unknown_user(self):
        for last in ({"Username": "revoked"}, {"Username": ""}, {}):
            raw = json.dumps([{"Username": "other"}] * 100 + [last])
            lua, occtl = self.runtime(raw, 31)
            self.assertFalse(occtl.absent("revoked"))
            self.assertEqual(lua.globals().closed, 2)
            self.assertTrue(lua.globals().reaped)

    def test_depth_is_bounded_before_decode(self):
        for depth in (31, 32):
            raw = '[{"ID":1,"Username":"user","nested":' + "[" * depth + "0" + "]" * depth + "}]"
            _, occtl = self.runtime(raw)
            self.assertEqual(bool(occtl.online().online_error), depth == 32)

    def test_nonzero_exit_discards_complete_rows(self):
        for code in (1, 124, 125):
            _, occtl = self.runtime('[{"ID":1,"Username":"user"}]', exit_code=code)
            self.assertTrue(occtl.online().online_error)

    def test_streaming_obeys_deadline_and_reaps(self):
        raw = json.dumps([{"ID": i + 1, "Username": "user"} for i in range(500)])
        lua, occtl = self.runtime(raw, chunk=1024, delay=1)
        self.assertTrue(occtl.online().online_error)
        self.assertTrue(lua.globals().reaped)
        self.assertEqual(lua.globals().closed, 2)
        self.assertEqual(lua.globals().kills, 2)
        self.assertEqual(lua.globals().reads, 4)

    def test_streaming_quota_and_consumer_errors_reap_without_retaining_output(self):
        for mode in ("success", "quota", "reject", "throw"):
            with self.subTest(mode=mode):
                raw = "x" * (262145 if mode == "quota" else 262144)
                lua, _ = self.runtime(raw)
                lua.globals().mode = mode
                code, captured = lua.execute('''
                    consumed = 0
                    return require("luci.model.ocserv_easy.process").run({"fixture"}, 5, function(chunk)
                        consumed = consumed + #chunk
                        if mode == "reject" then return false end
                        if mode == "throw" then error("consumer failure") end
                    end, 262144)
                ''')
                self.assertEqual(code, {"success": 0, "quota": 124, "reject": 125, "throw": 125}[mode])
                self.assertEqual(captured, "")
                self.assertLessEqual(lua.globals().consumed, 262144)
                self.assertTrue(lua.globals().reaped)
                self.assertEqual(lua.globals().closed, 2)

    def test_repeated_queries_release_records_and_pipe_handles(self):
        raw = json.dumps([{"ID": i + 1, "Username": "user", "Routes": ["10.0.0.0/8"] * 128}
                          for i in range(96)])
        lua, _ = self.runtime(raw)
        before, after = lua.execute('''
            local occtl = require "luci.model.ocserv_easy.occtl"
            assert(#occtl.online().online == 96)
            collectgarbage("collect")
            local before = collectgarbage("count")
            for i = 1, 100 do assert(#occtl.online().online == 96) end
            collectgarbage("collect")
            return before, collectgarbage("count")
        ''')
        self.assertEqual(lua.globals().closed, 202)
        self.assertEqual(lua.globals().kills, 0)
        self.assertTrue(lua.globals().reaped)
        self.assertLess(after - before, 32, "query state accumulates after collection (KiB)")


if __name__ == "__main__":
    unittest.main()
