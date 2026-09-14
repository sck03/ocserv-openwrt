"""Small shared helpers for reproducible release scripts."""
import hashlib
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


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def download_verified(destination, url, checksum):
    if destination.is_file():
        if sha256(destination) == checksum:
            return
        raise RuntimeError(f"SHA-256 mismatch in cached download: {destination.name}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=90) as response, temporary.open("wb") as output:
                shutil.copyfileobj(response, output)
            if sha256(temporary) != checksum:
                raise RuntimeError(f"SHA-256 mismatch: {destination.name}")
            temporary.replace(destination)
            return
        except (OSError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(2 * (attempt + 1))
