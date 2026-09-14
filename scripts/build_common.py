"""Small shared helpers for reproducible release scripts."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def version():
    source = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(BridgeVPN VERSION (\d+\.\d+\.\d+)", source)
    if not match:
        raise RuntimeError("Cannot determine application version")
    return match.group(1)


def source_directory(component):
    manifest = json.loads((ROOT / "scripts/sources.json").read_text(encoding="utf-8"))
    matches = [name for name in manifest if re.fullmatch(re.escape(component) + r"-[0-9][A-Za-z0-9.+-]*\.tar\.(gz|xz)", name)]
    if len(matches) != 1:
        raise RuntimeError(f"Expected one pinned source for {component}")
    return matches[0].removesuffix(".tar.gz").removesuffix(".tar.xz")


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def download_verified(destination, url, checksum):
    if destination.is_file():
        actual = sha256(destination)
        if actual == checksum:
            return
        raise RuntimeError(f"SHA-256 mismatch in cached download: {destination.name}; expected {checksum}, got {actual}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=90) as response, temporary.open("wb") as output:
                shutil.copyfileobj(response, output)
            actual = sha256(temporary)
            if actual != checksum:
                raise RuntimeError(f"SHA-256 mismatch: {destination.name}; expected {checksum}, got {actual}")
            temporary.replace(destination)
            return
        except (OSError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(2 * (attempt + 1))
