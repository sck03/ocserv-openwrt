"""Check upstream overrides and SDK selection without downloads or changing the checkout."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from sdk_config import BASE, resolve_sdk

spec = importlib.util.spec_from_file_location("prepare_build", ROOT / "scripts/prepare-build.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class BuildSelectionTests(unittest.TestCase):
    def test_default_source_keeps_known_checksum(self):
        self.assertEqual(prepare.selected("1.5.0", "a" * 64, "", "", (1, 5, 0)), ("1.5.0", "a" * 64))

    def test_new_source_requires_checksum(self):
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            prepare.selected("1.5.0", "a" * 64, "1.6.0", "", (1, 5, 0))

    def test_new_source_and_checksum(self):
        self.assertEqual(prepare.selected("9.21", "a" * 64, "9.22", "B" * 64, (9, 21, 0)), ("9.22", "b" * 64))

    def test_source_rejects_unsafe_version_and_old_api(self):
        for version in ("1.3.0", "../1.5.0", "1.5.0;id"):
            with self.assertRaises(ValueError):
                prepare.selected("1.5.0", "a" * 64, version, "b" * 64, (1, 5, 0))

    def test_pinned_sdk_needs_no_index_request(self):
        entry = resolve_sdk("24", fetch=lambda _: self.fail("Pinned SDK should not fetch an index"))
        self.assertEqual(entry["version"], "24.10.8")
        self.assertEqual(len(entry["sha256"]), 64)

    def test_wrong_series_and_paths_are_rejected(self):
        for version in ("25.12.5", "../24.10.8", "24.10-SNAPSHOT", "24.10.0-rc1"):
            with self.assertRaises(ValueError):
                resolve_sdk("24", version, fetch=lambda _: self.fail("Invalid version should not fetch"))

    def test_auto_uses_numeric_stable_version_and_exact_hash(self):
        name = "openwrt-sdk-24.10.10-armsr-armv8_gcc-13.3.0_musl.Linux-x86_64.tar.zst"
        base = BASE + "24.10.10/targets/armsr/armv8/"
        pages = {BASE: '<a href="24.10.9/"><a href="24.10.10/"><a href="25.12.5/"><a href="24.10.11-rc1/">',
                 base: f'<a href="{name}">', base + "sha256sums": "c" * 64 + " *" + name + "\n"}
        entry = resolve_sdk("24", "auto", fetch=pages.__getitem__)
        self.assertEqual(entry["version"], "24.10.10")
        self.assertEqual(entry["sha256"], "c" * 64)

    def test_missing_sdk_checksum_fails(self):
        name = "openwrt-sdk-25.12.6-armsr-armv8_gcc-14.3.0_musl.Linux-x86_64.tar.zst"
        with self.assertRaisesRegex(ValueError, "SHA256SUMS"):
            resolve_sdk("25", "25.12.6", fetch=lambda url: "" if url.endswith("sha256sums") else f'<a href="{name}">')


if __name__ == "__main__":
    unittest.main()
