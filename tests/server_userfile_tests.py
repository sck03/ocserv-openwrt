"""Shared account policy and startup failure propagation; no host service changes."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]


class RuntimeTests(unittest.TestCase):
    def test_account_records_use_the_same_strict_policy_at_startup(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        logic = lua.execute((ROOT / "server/openwrt/ocserv/files/lua/logic.lua").read_text(encoding="utf-8"))
        hashed = "$6$fixtureSalt$" + "a" * 86
        for password, expected in ((hashed, hashed), ("!" + hashed, "!" + hashed),
                                   ("plaintext", "!"), (hashed + ":injected", "!"), (hashed + "\n", "!")):
            users = lua.table_from([lua.table_from(dict(name="employee", group="*", password=password))])
            self.assertEqual(logic.passwd(users), "employee:*:" + expected + "\n")
        for name, group in (("bad:name", "*"), ("employee", "bad:group")):
            users = lua.table_from([lua.table_from(dict(name=name, group=group, password=hashed))])
            ok, _ = lua.eval("function(fn, rows) return pcall(fn, rows) end")(logic.passwd, users)
            self.assertFalse(ok)

    @unittest.skipUnless(shutil.which("sh"), "sh is required for the startup boundary")
    def test_renderer_failure_stops_startup_before_runtime_or_procd_changes(self):
        source = (ROOT / "server/openwrt/ocserv/files/ocserv.init").read_text(encoding="utf-8")
        function = re.search(r"(?ms)^start_service\(\) \{\n.*?^\}\n", source).group(0)
        function = function.replace("/usr/libexec/ocserv-render", "render")
        with tempfile.TemporaryDirectory(prefix="ocserv-start-") as directory:
            script = Path(directory) / "check.sh"
            script.write_text("initcerts() { return 0; }\nrender() { return 1; }\n" +
                              "mkdir() { exit 42; }\ntouch() { exit 42; }\nprocd_open_instance() { exit 42; }\n" +
                              function + "\nstart_service\n", encoding="utf-8", newline="\n")
            result = subprocess.run([shutil.which("sh"), "check.sh"], cwd=directory, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1)


if __name__ == "__main__":
    unittest.main()
