"""Bundle corresponding source without private build/test data or retired code."""
import json
import os
from pathlib import Path
import tempfile
import zipfile
from build_common import ROOT, sha256, source_files, version
from release_metadata import build_metadata


def main():
    metadata = build_metadata()
    destination = ROOT / "dist" / f"LinkoraVPN-{metadata['release_label']}-source.zip"
    destination.parent.mkdir(exist_ok=True)
    directories = ("client", "resources", "scripts", "tests", "docs", "server", ".github")
    files = source_files(ROOT, directories, (
        "CMakeLists.txt", "README.md", "LICENSE", "THIRD-PARTY-NOTICES.md",
        ".gitignore", ".gitattributes", ".clang-format",
    ))
    upstream = json.loads((ROOT / "scripts/sources.json").read_text(encoding="utf-8"))
    archives = []
    for name, entry in upstream.items():
        if Path(name).name != name or "\\" in name:
            raise RuntimeError(f"Invalid source archive name: {name}")
        path = ROOT / ".tools/downloads" / name
        if path.is_symlink() or sha256(path) != entry["sha256"]:
            raise RuntimeError(f"Source checksum mismatch: {name}")
        archives.append(path)
    with tempfile.TemporaryDirectory(prefix="source-", dir=destination.parent) as temporary:
        staged = Path(temporary) / destination.name
        with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED, compresslevel=7) as archive:
            archive.writestr("LinkoraVPN/BUILDINFO.json", json.dumps(
                {**metadata, "version": version(), "commit": os.environ.get("GITHUB_SHA", "local")}, indent=2) + "\n")
            for path in sorted(files):
                archive.write(path, "LinkoraVPN/" + path.relative_to(ROOT).as_posix())
            for path in archives:
                archive.write(path, "LinkoraVPN/.tools/downloads/" + path.name,
                              compress_type=zipfile.ZIP_STORED)
        staged.replace(destination)
    (destination.parent / "SHA256SUMS-source.txt").write_text(
        f"{sha256(destination)}  {destination.name}\n", encoding="ascii")
    print(f"Created {destination} ({destination.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
