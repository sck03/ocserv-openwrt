"""Fetch checksum-pinned client sources using Python 3.11+ on Linux or Windows."""
import argparse
import json
from pathlib import Path
import tarfile
import tempfile
import zipfile
from build_common import ROOT, download_verified


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download-only", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "scripts/sources.json").read_text(encoding="utf-8"))
    downloads = ROOT / ".tools/downloads"
    sources = ROOT / ".deps/sources"
    downloads.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    for name, entry in manifest.items():
        archive = downloads / name
        download_verified(archive, entry["url"], entry["sha256"])
        print(f"Verified {name}", flush=True)
        if args.download_only:
            continue
        folder = "wintun" if name.endswith(".zip") else name.removesuffix(".tar.gz").removesuffix(".tar.xz")
        destination = sources / folder
        stamp = sources / (folder + ".sha256")
        if destination.is_dir() and stamp.is_file() and stamp.read_text().strip() == entry["sha256"]:
            continue
        if destination.exists():
            raise RuntimeError(f"Existing unverified source directory: {destination}; use a clean build directory")
        with tempfile.TemporaryDirectory(prefix="unpack-", dir=sources) as temporary:
            if name.endswith(".zip"):
                with zipfile.ZipFile(archive) as package:
                    for member in package.infolist():
                        target = (Path(temporary) / member.filename).resolve()
                        if not target.is_relative_to(Path(temporary).resolve()):
                            raise RuntimeError("Unsafe archive member")
                    package.extractall(temporary)
            else:
                with tarfile.open(archive) as package:
                    if hasattr(tarfile, "data_filter"):
                        package.extractall(temporary, filter="data")
                    else:
                        # Older Python 3.11 builds have no extraction filters. These
                        # source releases contain only directories and regular files.
                        # Refuse links/devices rather than relaxing the path boundary.
                        for member in package.getmembers():
                            target = (Path(temporary) / member.name).resolve()
                            if not target.is_relative_to(Path(temporary).resolve()) or not (member.isdir() or member.isfile()):
                                raise RuntimeError("Unsafe source archive member")
                        package.extractall(temporary)
            extracted = Path(temporary) / folder
            if not extracted.is_dir():
                raise RuntimeError(f"Unexpected source archive layout: {name}")
            extracted.replace(destination)
        stamp.write_text(entry["sha256"] + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
