"""Bundle an explicit allowlist of corresponding source, without private build/test data."""
import json
import zipfile
from build_common import ROOT, sha256, version

destination = ROOT / "dist" / f"BulijieVPN-{version()}-source.zip"
destination.parent.mkdir(exist_ok=True)
directories = ("src", "resources", "config", "scripts", "tests", "docs", "server", ".github")
files = [ROOT / name for name in ("CMakeLists.txt", "README.md", "LICENSE", "THIRD-PARTY-NOTICES.md", ".gitignore", ".gitattributes")]
for directory in directories:
    files.extend(path for path in (ROOT / directory).rglob("*")
                 if path.is_file() and "__pycache__" not in path.parts and path.suffix not in (".pyc", ".pyo"))
upstream = json.loads((ROOT / "scripts/sources.json").read_text(encoding="utf-8"))
with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=7) as archive:
    for path in sorted(files):
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT):
            raise RuntimeError(f"Source file must stay in the workspace: {path}")
        archive.write(path, "BulijieVPN/" + path.relative_to(ROOT).as_posix())
    for name, entry in upstream.items():
        path = ROOT / ".tools/downloads" / name
        if sha256(path) != entry["sha256"]:
            raise RuntimeError(f"Source checksum mismatch: {name}")
        archive.write(path, "BulijieVPN/.tools/downloads/" + name, compress_type=zipfile.ZIP_STORED)
(destination.parent / "SHA256SUMS-source.txt").write_text(f"{sha256(destination)}  {destination.name}\n", encoding="ascii")
print(f"Created {destination} ({destination.stat().st_size} bytes)")
