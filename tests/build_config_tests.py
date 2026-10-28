"""Check upstream overrides and SDK selection without downloads or changing the checkout."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from sdk_config import BASE, TARGETS, resolve_sdk

spec = importlib.util.spec_from_file_location("prepare_build", ROOT / "scripts/prepare-build.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class BuildSelectionTests(unittest.TestCase):
    def test_default_source_keeps_known_checksum(self):
        with patch.object(prepare, "official_checksum", side_effect=AssertionError("Unexpected download")):
            self.assertEqual(prepare.selected("1.6.0", "a" * 64, "", "", (1, 6, 0), "server"), ("1.6.0", "a" * 64))

    def test_new_source_resolves_official_checksum_once(self):
        for component, current, version, minimum in (("server", "1.6.0", "1.6.1", (1, 6, 0)), ("client", "9.21", "9.22", (9, 21, 0))):
            with self.subTest(component=component), patch.object(prepare, "official_checksum", return_value="b" * 64) as resolve:
                self.assertEqual(prepare.selected(current, "a" * 64, version, "", minimum, component), (version, "b" * 64))
                resolve.assert_called_once_with(component, version)

    def test_new_source_and_checksum(self):
        with patch.object(prepare, "official_checksum", side_effect=AssertionError("Unexpected download")):
            self.assertEqual(prepare.selected("9.21", "a" * 64, "9.22", "B" * 64, (9, 21, 0), "client"), ("9.22", "b" * 64))

    def test_source_rejects_unsafe_version_and_old_api(self):
        for version in ("1.5.0", "../1.6.0", "1.6.0;id", "1.6", "1.6.0-rc1", "1." + "0" * 33 + ".0"):
            with self.assertRaises(ValueError):
                prepare.selected("1.6.0", "a" * 64, version, "b" * 64, (1, 6, 0), "server")

    def test_server_checksum_requires_exact_archive(self):
        digest = "A" * 64
        for separator in (" *", "  "):
            with patch.object(prepare, "fetch", return_value=(digest + separator + "ocserv-1.6.1.tar.xz\n").encode()):
                self.assertEqual(prepare.official_checksum("server", "1.6.1"), digest.lower())
        for body in (digest + " *ocserv-1.6.0.tar.xz", digest + " *ocserv-1.6.1.tar.xz\nextra", "invalid"):
            with patch.object(prepare, "fetch", return_value=body.encode()), self.assertRaises(ValueError):
                prepare.official_checksum("server", "1.6.1")

    def test_openconnect_requires_successful_signature_from_pinned_signer(self):
        from subprocess import CompletedProcess
        from hashlib import sha256
        fingerprint = prepare.OPENCONNECT_SIGNER
        for code, signer, valid in ((0, fingerprint, True), (1, fingerprint, False), (0, "B" * 40, False), (0, "", False)):
            status = f"[GNUPG:] VALIDSIG {fingerprint} 2026-01-01 1 0 4 0 1 8 00 {signer}\n" if signer else ""
            with self.subTest(code=code, signer=signer), patch.object(prepare.shutil, "which", return_value="gpg"), \
                    patch.object(prepare, "fetch", return_value=b"fixture"), \
                    patch.object(prepare.subprocess, "run", side_effect=[CompletedProcess([], 0), CompletedProcess([], code, status)]):
                if valid:
                    self.assertEqual(prepare.official_checksum("client", "9.22"), sha256(b"fixture").hexdigest())
                else:
                    with self.assertRaisesRegex(ValueError, "signature"):
                        prepare.official_checksum("client", "9.22")

    def test_wrong_series_and_paths_are_rejected(self):
        for version in ("24.10.8", "26.01.1", "../25.12.5", "25.12-SNAPSHOT", "25.12.0-rc1", "25.11.9"):
            with self.assertRaises(ValueError):
                resolve_sdk(version, fetch=lambda _: self.fail("Invalid version should not fetch"))

    def test_auto_uses_numeric_stable_version_and_exact_hash(self):
        name = "openwrt-sdk-25.12.10-armsr-armv8_gcc-14.3.0_musl.Linux-x86_64.tar.zst"
        base = BASE + "25.12.10/targets/armsr/armv8/"
        pages = {BASE: '<a href="25.12.9/"><a href="25.12.10/"><a href="26.01.1/"><a href="25.12.11-rc1/">',
                 base: f'<a href="{name}">', base + "sha256sums": "c" * 64 + " *" + name + "\n"}
        for requested in ("auto", "", "  "):
            with self.subTest(requested=requested):
                entry = resolve_sdk(requested, fetch=pages.__getitem__)
                self.assertEqual(entry["version"], "25.12.10")
                self.assertEqual(entry["sha256"], "c" * 64)
                self.assertEqual(entry["checksum_origin"], base + "sha256sums")

    def test_auto_rejects_index_without_stable_releases(self):
        with self.assertRaisesRegex(ValueError, "No released"):
            resolve_sdk(fetch=lambda _: '<a href="25.12.11-rc1/"><a href="snapshots/">')

    def test_explicit_sdk_does_not_query_release_index(self):
        name = "openwrt-sdk-25.12.5-armsr-armv8_gcc-14.3.0_musl.Linux-x86_64.tar.zst"
        base = BASE + "25.12.5/targets/armsr/armv8/"
        pages = {base: f'<a href="{name}">', base + "sha256sums": "a" * 64 + "  " + name}
        entry = resolve_sdk("25.12.5", fetch=pages.__getitem__)
        self.assertEqual(entry["version"], "25.12.5")
        self.assertEqual(entry["sha256"], "a" * 64)

    def test_missing_sdk_checksum_fails(self):
        name = "openwrt-sdk-25.12.6-armsr-armv8_gcc-14.3.0_musl.Linux-x86_64.tar.zst"
        with self.assertRaisesRegex(ValueError, "SHA256SUMS"):
            resolve_sdk("25.12.6", fetch=lambda url: "" if url.endswith("sha256sums") else f'<a href="{name}">')

    def test_all_target_urls_and_metadata(self):
        for target, config in TARGETS.items():
            with self.subTest(target=target):
                name = 'openwrt-sdk-25.12.6-' + target.replace('/', '-') + '_gcc-14.3.0_musl.Linux-x86_64.tar.zst'
                base = BASE + '25.12.6/targets/' + target + '/'
                pages = {base: f'<a href="{name}">', base + 'sha256sums': 'd' * 64 + '  ' + name}
                entry = resolve_sdk('25.12.6', fetch=pages.__getitem__, target=target)
                self.assertEqual(entry['url'], base + name)
                self.assertEqual(entry['architecture'], config['architecture'])
                self.assertEqual(entry['target'], target)

    def test_unsupported_target_fails_before_download(self):
        for target in ('../x86/64', 'x86/invalid', 'unknown'):
            with self.assertRaises(ValueError):
                resolve_sdk(target=target, fetch=lambda _: self.fail('unexpected download'))


if __name__ == "__main__":
    unittest.main()
