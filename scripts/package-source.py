"""Bundle corresponding source without private build/test data or retired code."""
import json
from pathlib import Path
import tempfile
import zipfile
from build_common import ROOT, sha256, version


def main():
    destination = ROOT / "dist" / f"BulijieVPN-{version()}-source.zip"
    destination.parent.mkdir(exist_ok=True)
    directories = ("client", "resources", "scripts", "tests", "docs", "server", ".github")
    files = [ROOT / name for name in (
        "CMakeLists.txt", "README.md", "LICENSE", "THIRD-PARTY-NOTICES.md",
        ".gitignore", ".gitattributes", ".clang-format",
    )]
    for directory in directories:
        files.extend(path for path in (ROOT / directory).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts
                     and path.suffix not in (".pyc", ".pyo"))
    for path in files:
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            raise RuntimeError(f"Source file must stay in the workspace: {path}")
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
            for path in sorted(files):
                archive.write(path, "BulijieVPN/" + path.relative_to(ROOT).as_posix())
            for path in archives:
                archive.write(path, "BulijieVPN/.tools/downloads/" + path.name,
                              compress_type=zipfile.ZIP_STORED)
        staged.replace(destination)
    (destination.parent / "SHA256SUMS-source.txt").write_text(
        f"{sha256(destination)}  {destination.name}\n", encoding="ascii")
    print(f"Created {destination} ({destination.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
