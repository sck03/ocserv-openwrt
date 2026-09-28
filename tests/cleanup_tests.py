"""Check workspace cleanup against disposable Git repositories on Windows."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell.exe")
if not POWERSHELL and os.name == "nt":
    candidate = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    if candidate.is_file():
        POWERSHELL = str(candidate)


@unittest.skipUnless(os.name == "nt" and POWERSHELL, "Windows PowerShell is required")
class CleanupTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cleanup-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for name in ("client/CMakeLists.txt", "scripts/sources.json", "build/cache.bin",
                     "scripts/__pycache__/cache.pyc", ".deps/library.a", "test-results/report.json",
                     "dist/release.zip", "artifacts/backups/config.zip", ".tools/tool.exe"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture", encoding="ascii")
        shutil.copyfile(ROOT / "scripts/clean-workspace.ps1", self.root / "scripts/clean-workspace.ps1")
        self.git("init", "-q")
        self.git("add", "client", "scripts/clean-workspace.ps1", "scripts/sources.json")

    def git(self, *args):
        subprocess.run(["git", "-C", str(self.root), *args], check=True, capture_output=True)

    def clean(self, *args):
        return subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                               str(self.root / "scripts/clean-workspace.ps1"), *args],
                              capture_output=True, text=True, timeout=30)

    def test_preview_does_not_delete(self):
        result = self.clean()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / "build/cache.bin").is_file())
        self.assertIn("Preview only", result.stdout)

    def test_apply_removes_only_default_caches(self):
        result = self.clean("-Apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / "build").exists())
        self.assertFalse((self.root / "scripts/__pycache__").exists())
        for name in (".deps/library.a", "test-results/report.json", "dist/release.zip",
                     "artifacts/backups/config.zip", ".tools/tool.exe", "client/CMakeLists.txt"):
            self.assertTrue((self.root / name).is_file(), name)

    def test_optional_caches_are_explicit(self):
        result = self.clean("-Apply", "-IncludeDependencies", "-IncludeTestResults")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.root / ".deps").exists())
        self.assertFalse((self.root / "test-results").exists())
        self.assertTrue((self.root / "artifacts/backups/config.zip").exists())

    def test_tracked_file_stops_all_deletions(self):
        self.git("add", "build/cache.bin")
        result = self.clean("-Apply")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tracked source files", result.stderr)
        self.assertTrue((self.root / "build/cache.bin").is_file())
        self.assertTrue((self.root / "scripts/__pycache__/cache.pyc").is_file())


if __name__ == "__main__":
    unittest.main()
