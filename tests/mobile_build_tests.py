#!/usr/bin/env python3
"""Portable regressions for mobile source integrity and iOS signing validation."""
import copy
import datetime
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import struct
import subprocess
import sys
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_common


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


sources = module("mobile-sources")
signing = module("ios-signing")
android_audit = module("audit-android")
android_prepare = module("prepare-mobile-android")
packaging = module("package-mobile")


class AndroidTests(unittest.TestCase):
    def elf(self, bits=2, machine=183, alignment=16384):
        data = bytearray(128)
        data[:6] = b"\x7fELF" + bytes([bits, 1])
        struct.pack_into("<H", data, 18, machine)
        if bits == 2:
            struct.pack_into("<Q", data, 32, 64)
            struct.pack_into("<HH", data, 54, 56, 1)
            struct.pack_into("<I", data, 64, 1)
            struct.pack_into("<Q", data, 112, alignment)
        else:
            struct.pack_into("<I", data, 28, 64)
            struct.pack_into("<HH", data, 42, 32, 1)
            struct.pack_into("<I", data, 64, 1)
            struct.pack_into("<I", data, 92, alignment)
        return data

    def test_accepts_three_aligned_architectures(self):
        for abi, (bits, machine) in android_audit.MACHINES.items():
            android_audit.audit_elf(self.elf(bits, machine), abi)

    def test_rejects_wrong_architecture_and_old_page_alignment(self):
        for data in (self.elf(alignment=4096), self.elf(machine=62), self.elf()[:70], b"not an ELF"):
            with self.assertRaises(ValueError):
                android_audit.audit_elf(data, "arm64-v8a")

    def test_adaptations_fail_on_ambiguous_upstream(self):
        with self.assertRaises(ValueError):
            android_prepare.once("old old", "old", "new")
        with self.assertRaises(ValueError):
            android_prepare.section("end before start", "start", "end", "new")


class SourceTests(unittest.TestCase):
    def test_manifest_has_fixed_https_sources(self):
        manifest = json.loads((ROOT / "mobile/sources.json").read_text())
        for entry in manifest.values():
            self.assertTrue(entry["url"].startswith("https://"))
            self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$")
            self.assertNotIn("/latest/", entry["url"])
        self.assertIn("0535533dfb7d3a656bfdb880d51f731c109135c1", manifest["opentunnel"]["url"])

    def test_download_verifies_before_installing(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)
            entry = {"filename": "source.tar.gz", "url": "https://example.invalid/source",
                     "sha256": hashlib.sha256(b"expected").hexdigest()}
            with patch.object(build_common.urllib.request, "urlopen", return_value=io.BytesIO(b"corrupt")):
                with self.assertRaises(RuntimeError):
                    sources.fetch(entry, destination)
            self.assertFalse((destination / entry["filename"]).exists())
            self.assertEqual(list(destination.iterdir()), [])
            with patch.object(build_common.urllib.request, "urlopen", return_value=io.BytesIO(b"expected")):
                archive = sources.fetch(entry, destination)
            archive.write_bytes(b"tampered")
            with self.assertRaises(RuntimeError):
                sources.fetch(entry, destination)

    def test_download_retries_and_cleans_failed_attempts(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "source.tar.gz"
            checksum = hashlib.sha256(b"expected").hexdigest()
            with patch.object(build_common.time, "sleep"), patch.object(
                build_common.urllib.request, "urlopen",
                side_effect=[OSError("offline"), io.BytesIO(b"expected")],
            ) as request:
                build_common.download_verified(archive, "https://example.invalid", checksum)
                self.assertEqual(request.call_count, 2)
            with patch.object(build_common.urllib.request, "urlopen") as request:
                build_common.download_verified(archive, "https://example.invalid", checksum)
                request.assert_not_called()
            archive.unlink()
            with patch.object(build_common.time, "sleep"), patch.object(
                build_common.urllib.request, "urlopen", side_effect=OSError("offline"),
            ) as request:
                with self.assertRaises(OSError):
                    build_common.download_verified(archive, "https://example.invalid", checksum)
                self.assertEqual(request.call_count, 3)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_concurrent_downloads_use_independent_temporary_files(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "source.tar.gz"
            checksum = hashlib.sha256(b"expected").hexdigest()

            class InterleavedResponse(io.BytesIO):
                def read(self, size=-1):
                    if self.tell() == 0:
                        build_common.download_verified(archive, "https://example.invalid", checksum)
                    return super().read(size)

            with patch.object(build_common.urllib.request, "urlopen", side_effect=[
                InterleavedResponse(b"expected"), io.BytesIO(b"expected"),
            ]):
                build_common.download_verified(archive, "https://example.invalid", checksum)
            self.assertEqual(archive.read_bytes(), b"expected")
            self.assertEqual(list(Path(directory).iterdir()), [archive])

    def test_download_rejects_unsafe_archive_names(self):
        for name in ("../source.tar.gz", "..\\source.tar.gz", ".", "", "/source.tar.gz"):
            with self.subTest(name=name), patch.object(sources, "download_verified") as download:
                with self.assertRaises(ValueError):
                    sources.fetch({"filename": name}, Path("unused"))
                download.assert_not_called()

    def test_archive_rejects_path_escape(self):
        for name in ("../escape", "source/../../escape"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                archive = Path(directory) / "source.tar"
                with tarfile.open(archive, "w") as tar:
                    item = tarfile.TarInfo(name)
                    item.size = 3
                    tar.addfile(item, io.BytesIO(b"bad"))
                with self.assertRaises((ValueError, tarfile.FilterError)):
                    sources.unpack(archive, Path(directory) / "out", "source")
                self.assertFalse((Path(directory) / "escape").exists())


class PackagingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.out = self.root / "dist/ios"
        self.out.mkdir(parents=True)
        self.archive = self.root / ".tools/mobile-downloads/source.tar.gz"
        self.archive.parent.mkdir(parents=True)
        self.archive.write_bytes(b"upstream archive")
        self.entries = {"core": {"filename": self.archive.name, "sha256": build_common.sha256(self.archive)}}
        self.info = self.root / "build/mobile/ios/BUILDINFO.json"
        self.info.parent.mkdir(parents=True)
        self.info.write_text(json.dumps(self.entries))
        (self.root / "mobile").mkdir()
        (self.root / "mobile/app.swift").write_text("tracked source")
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "add", "mobile/app.swift"], check=True)
        self.root_patch = patch.object(packaging, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def test_only_tracked_current_sources_and_uncompressed_upstream(self):
        (self.root / "mobile/app.swift").write_text("current changes")
        (self.root / "mobile/backup.txt").write_text("private backup")
        packaging.package("ios")
        with zipfile.ZipFile(self.out / "corresponding-source.zip") as bundle:
            self.assertEqual(set(bundle.namelist()), {"project/mobile/app.swift", "upstream/source.tar.gz"})
            self.assertEqual(bundle.read("project/mobile/app.swift"), b"current changes")
            self.assertEqual(bundle.getinfo("upstream/source.tar.gz").compress_type, zipfile.ZIP_STORED)
        checksum = build_common.sha256(self.out / "corresponding-source.zip")
        self.assertIn(checksum, (self.out / "SHA256SUMS.txt").read_text())

    def test_failed_package_preserves_previous_outputs(self):
        packaging.package("ios")
        previous = {p.name: p.read_bytes() for p in self.out.iterdir()}
        self.archive.write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            packaging.package("ios")
        self.assertEqual({p.name: p.read_bytes() for p in self.out.iterdir()}, previous)

    def test_rejects_escaping_archive_name(self):
        self.entries["core"]["filename"] = "../source.tar.gz"
        self.info.write_text(json.dumps(self.entries))
        with self.assertRaisesRegex(ValueError, "Invalid source archive name"):
            packaging.package("ios")
        self.assertEqual(list(self.out.iterdir()), [])


class SigningTests(unittest.TestCase):
    def setUp(self):
        self.profile = {
            "ApplicationIdentifierPrefix": ["PREFIX1234"], "TeamIdentifier": ["TEAM123456"],
            "ExpirationDate": datetime.datetime.now() + datetime.timedelta(days=30),
            "UUID": "01234567-89AB-CDEF-0123-456789ABCDEF", "ProvisionedDevices": ["device"],
            "Entitlements": {"application-identifier": "PREFIX1234.io.github.sck03.linkoravpn",
                             "com.apple.developer.networking.networkextension": ["packet-tunnel-provider"],
                             "get-task-allow": False},
        }

    def test_accepts_matching_distribution_profile(self):
        self.assertEqual(signing.validate_profile(self.profile, "io.github.sck03.linkoravpn", "TEAM123456"), self.profile["UUID"])

    def test_rejects_wrong_app_or_team(self):
        for bundle, team in (("io.github.sck03.linkoravpn.tunnel", "TEAM123456"), ("io.github.sck03.linkoravpn", "WRONG")):
            with self.assertRaises(ValueError):
                signing.validate_profile(self.profile, bundle, team)

    def test_rejects_expired_development_and_missing_capability(self):
        invalid = []
        p = copy.deepcopy(self.profile)
        p["ExpirationDate"] = datetime.datetime(2000, 1, 1)
        invalid.append(p)
        p = copy.deepcopy(self.profile)
        p["Entitlements"]["get-task-allow"] = True
        invalid.append(p)
        p = copy.deepcopy(self.profile)
        p["Entitlements"]["com.apple.developer.networking.networkextension"] = []
        invalid.append(p)
        p = copy.deepcopy(self.profile)
        p["ProvisionedDevices"] = []
        invalid.append(p)
        for profile in invalid:
            with self.assertRaises(ValueError):
                signing.validate_profile(profile, "io.github.sck03.linkoravpn", "TEAM123456")


if __name__ == "__main__":
    unittest.main()
