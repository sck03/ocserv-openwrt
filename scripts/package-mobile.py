#!/usr/bin/env python3
"""Package corresponding sources separately from applications and signing data."""
import argparse
import json
from pathlib import Path
import tempfile
import zipfile
from build_common import ROOT, sha256, source_files, write_checksums


def package(platform):
    out = ROOT / "dist" / platform
    sources = ROOT / "dist" / (platform + "-source")
    info = ROOT / ("build/desktop" if platform == "macos" else "build/mobile") / platform / "BUILDINFO.json"
    entries = json.loads(info.read_text(encoding="utf-8"))
    if not (ROOT / ".git").exists():
        raise ValueError("Package mobile sources from a Git checkout to exclude signing files")
    directories = {
        "android": ("mobile/android",),
        "ios": ("mobile/ios", "client/apple"),
        "macos": ("desktop/macos", "client/apple", "resources"),
    }[platform]
    scripts = (
        "build_common.py", "mobile-sources.py", "package-mobile.py", "ios-signing.py",
        "audit-android.py", "prepare-mobile-android.py",
        "build-" + platform + ".sh",
        *(("build-apple-core.sh",) if platform != "android" else ()),
    )
    paths = source_files(ROOT, directories, (
        "mobile/sources.json",
        "LICENSE", "README.md", "THIRD-PARTY-NOTICES.md", "docs/MOBILE.md", "docs/DESKTOP.md",
        "tests/mobile_build_tests.py", "tests/mobile_protocol_tests.c", "tests/ios_connection_tests.m",
        ".github/workflows/build-" + ("desktop" if platform == "macos" else platform) + ".yml",
        *("scripts/" + name for name in scripts),
    ))
    sources.mkdir(parents=True, exist_ok=True)
    destination = sources / f"Linkora-{platform}-source.zip"
    with tempfile.TemporaryDirectory(prefix="source-", dir=sources) as work:
        staged = Path(work) / destination.name
        with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED) as bundle:
            for path in paths:
                bundle.write(path, "project/" + path.relative_to(ROOT).as_posix())
            for entry in entries.values():
                name = entry["filename"]
                if Path(name).name != name or "\\" in name or name in ("", ".", ".."):
                    raise ValueError(f"Invalid source archive name: {name}")
                archive = ROOT / ".tools/mobile-downloads" / name
                if archive.is_symlink() or not archive.resolve().is_relative_to(ROOT) or sha256(archive) != entry["sha256"]:
                    raise ValueError("Source changed before packaging")
                bundle.write(archive, "upstream/" + archive.name, compress_type=zipfile.ZIP_STORED)
        staged.replace(destination)
    write_checksums(sources, [destination])
    patterns = {
        "android": ("Linkora-android-*.apk", "BUILDINFO.json"),
        "ios": ("Linkora-ios-unsigned.xcarchive.zip", "Linkora-ios.ipa", "BUILDINFO.json"),
        "macos": ("Linkora-*-macos-universal-*.zip", "BUILDINFO.json"),
    }[platform]
    out.mkdir(parents=True, exist_ok=True)
    write_checksums(out, [path for pattern in patterns for path in out.glob(pattern) if path.is_file()])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=["android", "ios", "macos"])
    package(parser.parse_args().platform)
