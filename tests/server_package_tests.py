"""Package a synthetic SDK to verify clean staging, revisions and failure preservation."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from sdk_config import TARGETS
spec = importlib.util.spec_from_file_location("server_package", ROOT / "scripts/package-server.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class ServerPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="server-package-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / "project"
        self.sdk = Path(temporary.name) / "sdk"
        for directory in ("server", "docs"):
            shutil.copytree(ROOT / directory, self.root / directory)
        self.sdk.mkdir()
        (self.sdk / "sdk-info.json").write_text(json.dumps({"version": "25.12.5", "target": "armsr/armv8"}))
        (self.sdk / ".config").write_text('CONFIG_USE_APK=y\nCONFIG_TARGET_armsr_armv8=y\nCONFIG_TARGET_ARCH_PACKAGES="aarch64_generic"\n')
        (self.sdk / "feeds.conf").write_text("fixture feeds\n")
        self.write("dl/ocserv-1.5.0.tar.xz", b"synthetic upstream archive")
        recipe = self.root / "server/openwrt/ocserv/Makefile"
        recipe.write_text("PKG_VERSION:=1.5.0\nPKG_RELEASE:=3\nPKG_HASH:=" +
                          package.sha256(self.sdk / "dl/ocserv-1.5.0.tar.xz") + "\n")
        (self.root / "server/openwrt/luci-app-ocserv-easy/Makefile").write_text(
            "PKG_VERSION:=0.4.1\nPKG_RELEASE:=2\n")
        for name in ("ocserv-1.5.0-r3.apk", "luci-app-ocserv-easy-0.4.1-r2.apk"):
            self.write("bin/packages/aarch64/fixture/" + name, b"synthetic APK")
        server_root = self.sdk / "build_dir/target-fixture/ocserv-1.5.0/.pkgdir/ocserv"
        self.binary = server_root / "usr/sbin/ocserv"
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", header, 18, 183)
        self.write(self.binary.relative_to(self.sdk), header)
        startup = server_root / "etc/init.d/ocserv"
        startup.parent.mkdir(parents=True)
        shutil.copyfile(self.root / "server/openwrt/ocserv/files/ocserv.init", startup)
        ui = self.root / "server/openwrt/luci-app-ocserv-easy"
        self.ui_stage = self.sdk / "build_dir/target-fixture/luci-app-ocserv-easy/.pkgdir/luci-app-ocserv-easy"
        shutil.copytree(ui / "root", self.ui_stage)
        shutil.copytree(ui / "luasrc", self.ui_stage / "usr/lib/lua/luci")
        shutil.copytree(ui / "htdocs", self.ui_stage / "www")
        self.runtime = {self.ui_stage / relative for relative in (
            "etc/init.d/ocserv-easy-guard", "usr/libexec/ocserv-easy-guard", "usr/libexec/ocserv-easy-repair-users")}
        for path in self.runtime:
            path.chmod(0o755)

    def write(self, relative, data):
        path = self.sdk / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def bundle(self):
        def command(args, **kwargs):
            destination = next(arg.removeprefix("--output=") for arg in args if arg.startswith("--output="))
            Path(destination).write_bytes(b"synthetic LuCI source")

        original_stat = Path.stat
        def sdk_stat(path, *args, **kwargs):
            result = original_stat(path, *args, **kwargs)
            # Windows cannot represent the SDK's POSIX executable bits.
            if os.name == "nt" and path in self.runtime:
                values = list(result)
                values[0] |= 0o111
                return os.stat_result(values)
            return result

        with patch.object(package, "ROOT", self.root), patch.object(sys, "argv", ["package-server", str(self.sdk)]), \
                patch.dict(os.environ, RELEASE_VERSION="1.5.0", BUILD_TIMESTAMP="2026-09-29T01:02:03Z"), \
                patch.object(package.subprocess, "check_output", return_value="synthetic audit or revision\n"), \
                patch.object(package.subprocess, "run", side_effect=command), patch.object(Path, "stat", sdk_stat):
            package.main()
        return next((self.root / "dist").glob("*.zip"))

    def test_package_includes_revisions_metadata_and_verified_file_list(self):
        archive = self.bundle()
        self.assertIn("ocserv-1.5.0-r3-openwrt-", archive.name)
        self.assertIn("ui-0.4.1-r2-1.5.0", archive.name)
        with zipfile.ZipFile(archive) as contents:
            prefix = archive.stem + "/"
            info = json.loads(contents.read(prefix + "BUILDINFO.json"))
            self.assertEqual(info["build_timestamp"], "2026-09-29T01:02:03Z")
            self.assertEqual(info["management_ui_release"], "2")
            for line in contents.read(prefix + "SHA256SUMS").decode().splitlines():
                checksum, filename = line.split("  ", 1)
                self.assertEqual(hashlib.sha256(contents.read(prefix + filename)).hexdigest(), checksum)

    def test_existing_unpacked_output_cannot_contaminate_next_bundle(self):
        archive = self.bundle()
        stale = archive.with_suffix("")
        stale.mkdir()
        (stale / "obsolete.apk").write_bytes(b"stale")
        (stale / "private.key").write_bytes(b"synthetic private data")
        self.bundle()
        with zipfile.ZipFile(archive) as contents:
            self.assertFalse(any(name.endswith(("obsolete.apk", "private.key")) for name in contents.namelist()))

    def test_failed_elf_audit_preserves_previous_bundle_and_cleans_staging(self):
        archive = self.bundle()
        checksum = package.sha256(archive)
        self.binary.write_bytes(b"invalid executable")
        with self.assertRaisesRegex(RuntimeError, "ELF"):
            self.bundle()
        self.assertEqual(package.sha256(archive), checksum)
        self.assertEqual(list((self.root / "dist").glob("server-*")), [])

    def test_failed_source_verification_preserves_previous_bundle(self):
        archive = self.bundle()
        checksum = package.sha256(archive)
        (self.sdk / "dl/ocserv-1.5.0.tar.xz").write_bytes(b"corrupt")
        with self.assertRaisesRegex(RuntimeError, "source checksum"):
            self.bundle()
        self.assertEqual(package.sha256(archive), checksum)

    def test_all_targets_package_and_reject_wrong_elf(self):
        for target, config in TARGETS.items():
            with self.subTest(target=target):
                (self.sdk / "sdk-info.json").write_text(json.dumps({"version": "25.12.5", "target": target}))
                (self.sdk / ".config").write_text('CONFIG_USE_APK=y\nCONFIG_TARGET_' + target.replace('/', '_') +
                    '=y\nCONFIG_TARGET_ARCH_PACKAGES="' + config['architecture'] + '"\n')
                header = bytearray(64)
                header[:6] = b'\x7fELF' + bytes([config['elf_class'], config['elf_data']])
                struct.pack_into('<H' if config['elf_data'] == 1 else '>H', header, 18, config['elf_machine'])
                self.binary.write_bytes(header)
                self.bundle()
                archives = list((self.root / 'dist').glob('*-' + config['architecture'] + '-*.zip'))
                self.assertEqual(len(archives), 1)
                with zipfile.ZipFile(archives[0]) as contents:
                    prefix = archives[0].stem + '/'
                    self.assertEqual(contents.read(prefix + 'APK-ARCHITECTURES').decode().splitlines(), config['apk_architectures'])
                    self.assertEqual(json.loads(contents.read(prefix + 'BUILDINFO.json'))['architecture'], config['architecture'])
                header[4] = 3 - config['elf_class']
                self.binary.write_bytes(header)
                with self.assertRaisesRegex(RuntimeError, 'ELF'):
                    self.bundle()
        self.assertEqual(len(list((self.root / 'dist').glob('SHA256SUMS-openwrt-*.txt'))), len(TARGETS))

    def test_wrong_sdk_target_rejected(self):
        (self.sdk / '.config').write_text('CONFIG_USE_APK=y\nCONFIG_TARGET_x86_64=y\nCONFIG_TARGET_ARCH_PACKAGES="x86_64"\n')
        with self.assertRaisesRegex(RuntimeError, 'SDK target'):
            self.bundle()


if __name__ == "__main__":
    unittest.main()
