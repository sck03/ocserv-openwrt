#!/usr/bin/env python3
"""Package corresponding sources with mobile outputs, never signing secrets."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile
from build_common import ROOT, sha256


def package(platform):
    out = ROOT / "dist" / platform
    info = ROOT / "build/mobile" / platform / "BUILDINFO.json"
    entries = json.loads(info.read_text(encoding="utf-8"))
    # Read current contents of tracked files, excluding local backups and secrets.
    paths = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached"], cwd=ROOT
    ).decode("utf-8").split("\0")
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="source-", dir=out) as work:
        staged = Path(work) / "corresponding-source.zip"
        with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED) as bundle:
            for name in sorted(set(paths)):
                if name and (name.startswith(("mobile/", "scripts/", ".github/workflows/"))
                             or name in ("LICENSE", "README.md", "THIRD-PARTY-NOTICES.md", "docs/MOBILE.md", "tests/mobile_build_tests.py", "tests/mobile_protocol_tests.c")):
                    path = ROOT / name
                    if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
                        raise ValueError(f"Source file must stay in the workspace: {name}")
                    bundle.write(path, "project/" + name)
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
    parser.add_argument("platform", choices=["android", "ios"])
    package(parser.parse_args().platform)
