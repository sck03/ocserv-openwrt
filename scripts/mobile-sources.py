#!/usr/bin/env python3
"""Fetch and verify every native input before invoking either mobile toolchain."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import urllib.request
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "mobile/sources.json"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(entry, downloads):
    archive = downloads / entry["filename"]
    if not archive.exists():
        partial = archive.with_suffix(archive.suffix + ".part")
        with urllib.request.urlopen(entry["url"], timeout=120) as response, partial.open("wb") as out:
            shutil.copyfileobj(response, out)
        if digest(partial) != entry["sha256"]:
            partial.unlink()
            raise ValueError(f"Checksum mismatch: {archive.name}")
        partial.replace(archive)
    if digest(archive) != entry["sha256"]:
        raise ValueError(f"Cached source checksum mismatch: {archive}")
    return archive


def unpack(archive, destination, directory):
    # Each run starts in a new work directory. Never trust an extracted cache.
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            if member.name.split("/")[0] != directory:
                raise ValueError(f"Unexpected archive root: {member.name}")
        source.extractall(destination, filter="data")


def prepare(platform):
    entries = json.loads(MANIFEST.read_text(encoding="utf-8"))
    work = ROOT / "build/mobile" / platform
    if work.exists():
        raise FileExistsError(f"Move the previous build directory before rebuilding: {work}")
    downloads = ROOT / ".tools/mobile-downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    selected = ["openconnect", "openssl", "libxml2"]
    if platform == "android":
        selected += ["opentunnel", "lz4"]
    archives = {key: fetch(entries[key], downloads) for key in selected}
    work.mkdir(parents=True, exist_ok=False)
    if platform == "android":
        unpack(archives["opentunnel"], work, entries["opentunnel"]["directory"])
        app = work / entries["opentunnel"]["directory"]
        app.rename(work / "app")
        app = work / "app"
        cache = app / "native/build/downloads"
        cache.mkdir(parents=True)
        for key in selected:
            if key != "opentunnel":
                shutil.copy2(archives[key], cache / entries[key]["filename"])
        spec = importlib.util.spec_from_file_location("android_prepare", ROOT / "scripts/prepare-mobile-android.py")
        android = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(android)
        android.prepare(app, ROOT)
    else:
        for key in selected:
            unpack(archives[key], work, entries[key]["directory"])
    (work / "BUILDINFO.json").write_text(json.dumps(
        {key: entries[key] for key in selected}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=["android", "ios"])
    prepare(parser.parse_args().platform)
