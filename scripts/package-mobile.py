#!/usr/bin/env python3
"""Package corresponding sources with mobile outputs, never signing secrets."""
import argparse
import json
from pathlib import Path
import tempfile
import zipfile
from build_common import ROOT, sha256, source_files


def package(platform):
    out = ROOT / "dist" / platform
    info = ROOT / ("build/desktop" if platform == "macos" else "build/mobile") / platform / "BUILDINFO.json"
    entries = json.loads(info.read_text(encoding="utf-8"))
    if not (ROOT / ".git").exists():
        raise ValueError("Package mobile sources from a Git checkout to exclude signing files")
    paths = source_files(ROOT, ("mobile", "client/apple", "desktop", "scripts", ".github/workflows"), (
        "LICENSE", "README.md", "THIRD-PARTY-NOTICES.md", "docs/MOBILE.md", "docs/DESKTOP.md",
        "tests/mobile_build_tests.py", "tests/mobile_protocol_tests.c", "tests/ios_connection_tests.m",
    ))
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="source-", dir=out) as work:
        staged = Path(work) / "corresponding-source.zip"
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
        staged.replace(out / staged.name)
    (out / "SHA256SUMS.txt").write_text("".join(
        f"{sha256(path)}  {path.name}\n" for path in sorted(out.iterdir())
        if path.is_file() and path.name != "SHA256SUMS.txt"), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=["android", "ios", "macos"])
    package(parser.parse_args().platform)
