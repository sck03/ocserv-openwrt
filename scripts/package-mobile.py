#!/usr/bin/env python3
"""Package corresponding sources with mobile outputs, never signing secrets."""
import argparse
import json
from pathlib import Path
import subprocess
import zipfile
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mobile_sources", ROOT / "scripts/mobile-sources.py")
sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sources)


def package(platform):
    out = ROOT / "dist" / platform
    info = ROOT / "build/mobile" / platform / "BUILDINFO.json"
    entries = json.loads(info.read_text(encoding="utf-8"))
    # Include current local changes as well as tracked files; no generated build tree.
    paths = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT
    ).decode().split("\0")
    with zipfile.ZipFile(out / "corresponding-source.zip", "w", zipfile.ZIP_DEFLATED) as bundle:
        for name in sorted(set(paths)):
            if name and (name.startswith(("mobile/", "scripts/", ".github/workflows/"))
                         or name in ("LICENSE", "README.md", "THIRD-PARTY-NOTICES.md", "docs/MOBILE.md", "tests/mobile_build_tests.py", "tests/mobile_protocol_tests.c")):
                bundle.write(ROOT / name, "project/" + name)
        for entry in entries.values():
            archive = ROOT / ".tools/mobile-downloads" / entry["filename"]
            if sources.digest(archive) != entry["sha256"]:
                raise ValueError("Source changed before packaging")
            bundle.write(archive, "upstream/" + archive.name)
    (out / "SHA256SUMS.txt").write_text("".join(
        f"{sources.digest(path)}  {path.name}\n" for path in sorted(out.iterdir())
        if path.is_file() and path.name != "SHA256SUMS.txt"), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=["android", "ios"])
    package(parser.parse_args().platform)
