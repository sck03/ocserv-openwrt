"""Exercise the release import audit and corresponding-source packaging boundaries."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pe_audit", ROOT / "scripts/audit-pe.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def fixture_pe(arch="x64", dll="msvcrt.dll", function="malloc", subsystem=(6, 1), delayed=False):
    data = bytearray(2048)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    is64 = arch == "x64"
    optional_size = 240 if is64 else 224
    struct.pack_into("<HH", data, 0x84, 0x8664 if is64 else 0x14C, 1)
    struct.pack_into("<H", data, 0x94, optional_size)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B if is64 else 0x10B)
    struct.pack_into("<HH", data, optional + 48, *subsystem)
    struct.pack_into("<I", data, optional + 60, 0x200)
    directories = optional + (112 if is64 else 96)
    struct.pack_into("<II", data, directories + 8, 0x1000, 40)
    if delayed:
        struct.pack_into("<II", data, directories + 13 * 8, 0x1200, 32)
    struct.pack_into("<IIII", data, optional + optional_size + 8, 0x600, 0x1000, 0x600, 0x200)
    struct.pack_into("<IIIII", data, 0x200, 0x1100, 0, 0, 0x1080, 0x1100)
    data[0x280:0x280 + len(dll)] = dll.encode("ascii")
    struct.pack_into("<Q" if is64 else "<I", data, 0x300, 0x1180)
    data[0x382:0x382 + len(function)] = function.encode("ascii")
    return data


class ImportAuditTests(unittest.TestCase):
    def test_both_supported_architectures(self):
        for arch in ("x64", "x86"):
            report = audit.inspect(fixture_pe(arch))
            self.assertTrue(report["passed"])
            self.assertEqual(report["architecture"], arch)

    def test_framework_or_compiler_runtime_dll_is_rejected(self):
        for dll in ("Qt6Core.dll", "VCRUNTIME140.dll", "libgcc_s_seh-1.dll", "ucrtbase.dll"):
            self.assertFalse(audit.inspect(fixture_pe(dll=dll))["passed"])

    def test_newer_windows_apis_and_subsystems_are_rejected(self):
        self.assertFalse(audit.inspect(fixture_pe(function="GetDpiForWindow"))["passed"])
        self.assertFalse(audit.inspect(fixture_pe(subsystem=(10, 0)))["passed"])

    def test_delay_imports_cannot_bypass_audit(self):
        self.assertFalse(audit.inspect(fixture_pe(delayed=True))["passed"])

    def test_malformed_and_inconsistent_binaries(self):
        for data in (b"", b"MZ" + b"\0" * 64, fixture_pe()[:0x200]):
            with self.assertRaises(ValueError):
                audit.inspect(data)
        data = fixture_pe()
        struct.pack_into("<H", data, 0x84, 0x14C)
        self.assertFalse(audit.inspect(data)["passed"])


class SourceBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="client-source-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for directory in ("scripts", "client", "resources", "tests", "docs", "server", ".github",
                          ".tools/downloads", ".deps", "test-results", "src", "config"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        for name in ("CMakeLists.txt", "README.md", "LICENSE", "THIRD-PARTY-NOTICES.md",
                     ".gitignore", ".gitattributes", ".clang-format"):
            shutil.copyfile(ROOT / name, self.root / name)
        for name in ("package-source.py", "build_common.py"):
            shutil.copyfile(ROOT / "scripts" / name, self.root / "scripts" / name)
        (self.root / "client/main.cpp").write_text("// fixture source\n", encoding="utf-8")
        (self.root / "src/main.cpp").write_text("obsolete code", encoding="utf-8")
        (self.root / "config/BridgeVPN.ini").write_text("obsolete defaults", encoding="utf-8")
        for name in (".tools/private.key", ".deps/private.key", "test-results/private.key"):
            (self.root / name).write_text("synthetic private test data", encoding="utf-8")
        self.archive = self.root / ".tools/downloads/fixture-1.0.tar.gz"
        self.archive.write_bytes(b"synthetic pinned archive")
        self.manifest = {self.archive.name: {
            "url": "https://example.invalid/fixture-1.0.tar.gz",
            "sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest(),
        }}
        (self.root / "scripts/sources.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def bundle(self):
        return subprocess.run([sys.executable, self.root / "scripts/package-source.py"],
                              capture_output=True, text=True, timeout=30)

    def test_source_contains_pinned_archives_without_local_or_retired_data(self):
        result = self.bundle()
        self.assertEqual(result.returncode, 0, result.stderr)
        package = next((self.root / "dist").glob("*-source.zip"))
        with zipfile.ZipFile(package) as source:
            names = source.namelist()
            self.assertIn("BulijieVPN/client/main.cpp", names)
            self.assertIn("BulijieVPN/.tools/downloads/fixture-1.0.tar.gz", names)
            self.assertFalse(any("private.key" in name or "/src/" in name or "/config/" in name for name in names))
        checksum = (self.root / "dist/SHA256SUMS-source.txt").read_text()
        self.assertIn(hashlib.sha256(package.read_bytes()).hexdigest(), checksum)

    def test_corrupt_download_does_not_replace_a_good_source_package(self):
        self.assertEqual(self.bundle().returncode, 0)
        package = next((self.root / "dist").glob("*-source.zip"))
        previous = package.read_bytes()
        self.archive.write_bytes(b"corrupt")
        self.assertNotEqual(self.bundle().returncode, 0)
        self.assertEqual(package.read_bytes(), previous)

    def test_archive_paths_cannot_escape_download_directory(self):
        self.manifest["../private.key"] = self.manifest.pop(self.archive.name)
        (self.root / "scripts/sources.json").write_text(json.dumps(self.manifest), encoding="utf-8")
        self.assertNotEqual(self.bundle().returncode, 0)


if __name__ == "__main__":
    unittest.main()
