"""Resolve official release checksums and pin a version in an isolated CI checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import urllib.request
from build_common import ROOT

OPENCONNECT_SIGNER = "BE07D9FD54809AB2C4B0FF5F63762CDA67E2F359"


def fetch(url, limit):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=45) as response:
                if not response.geturl().startswith("https://"):
                    raise ValueError("Official source requests must remain on HTTPS")
                data = response.read(limit + 1)
            if len(data) > limit:
                raise ValueError("Official source response exceeds the size limit")
            return data
        except (OSError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def official_checksum(component, version):
    if component == "server":
        name = f"ocserv-{version}.tar.xz"
        url = f"https://www.infradead.org/ocserv/download/{name}.sha256sum"
        value = fetch(url, 4096).decode("ascii").strip()
        match = re.fullmatch(r"([0-9a-fA-F]{64})\s+\*?" + re.escape(name), value)
        if not match:
            raise ValueError("No unique SHA-256 for the selected release in the official checksum file")
        return match[1].lower()
    # OpenConnect publishes detached signatures rather than SHA256SUMS.
    # Verify the official release signer before deriving the archive digest.
    gpg = shutil.which("gpg")
    if not gpg:
        raise ValueError("Install GnuPG (gpg) to verify a different OpenConnect release")
    name = f"openconnect-{version}.tar.gz"
    url = f"https://www.infradead.org/openconnect/download/{name}"
    with tempfile.TemporaryDirectory(prefix="openconnect-verify-") as temporary:
        work = Path(temporary)
        (work / "gnupg").mkdir(mode=0o700)
        archive, signature, key = (work / item for item in (name, name + ".asc", "signer.asc"))
        archive.write_bytes(fetch(url, 64 * 1024 * 1024))
        signature.write_bytes(fetch(url + ".asc", 65536))
        key.write_bytes(fetch("https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x" + OPENCONNECT_SIGNER, 1024 * 1024))
        command = [gpg, "--no-options", "--homedir", "gnupg", "--batch", "--no-auto-key-retrieve"]
        subprocess.run([*command, "--import-options", "import-minimal", "--import", key.name],
                       cwd=work, check=True, capture_output=True, timeout=30)
        result = subprocess.run([*command, "--status-fd", "1", "--verify", signature.name, archive.name],
                                cwd=work, capture_output=True, text=True, timeout=30)
        signatures = [line.split()[2:] for line in result.stdout.splitlines() if line.startswith("[GNUPG:] VALIDSIG ")]
        if result.returncode or len(signatures) != 1 or signatures[0][-1] != OPENCONNECT_SIGNER:
            raise ValueError("OpenConnect signature is invalid or does not match the pinned official signer")
        with archive.open("rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()


def selected(current_version, current_hash, version, checksum, minimum, component):
    version = version.strip() or current_version
    checksum = checksum.strip().lower()
    pattern = r"[0-9]+\.[0-9]+\.[0-9]+" if component == "server" else r"[0-9]+\.[0-9]+(?:\.[0-9]+)?"
    if len(version) > 32 or not re.fullmatch(pattern, version):
        raise ValueError("Use a numeric upstream release version")
    parts = tuple(int(value) for value in version.split("."))
    parts += (0,) * (3 - len(parts))
    if parts < minimum:
        raise ValueError("This project requires a newer upstream version")
    if not checksum:
        checksum = current_hash if version == current_version else official_checksum(component, version)
    if not re.fullmatch(r"[0-9a-f]{64}", checksum):
        raise ValueError("SHA-256 must contain exactly 64 hexadecimal characters")
    return version, checksum


def prepare_client(version, checksum=""):
    path = ROOT / "scripts/sources.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    previous = [name for name in manifest if name.startswith("openconnect-")]
    if len(previous) != 1:
        raise ValueError("Expected one OpenConnect source entry")
    previous = previous[0]
    current = previous.removeprefix("openconnect-").removesuffix(".tar.gz")
    version, checksum = selected(current, manifest[previous]["sha256"], version, checksum, (9, 21, 0), "client")
    replacement = f"openconnect-{version}.tar.gz"
    output = {}
    for name, entry in manifest.items():
        output[replacement if name == previous else name] = (
            {"url": f"https://www.infradead.org/openconnect/download/{replacement}", "sha256": checksum}
            if name == previous else entry
        )
    path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"OpenConnect {version}; official source; SHA-256 {checksum}")
    return version, checksum


def prepare_server(version, checksum=""):
    path = ROOT / "server/openwrt/ocserv/Makefile"
    recipe = path.read_text(encoding="utf-8")
    current = re.search(r"(?m)^PKG_VERSION:=(\S+)$", recipe).group(1)
    current_hash = re.search(r"(?m)^PKG_HASH:=([0-9a-f]{64})$", recipe).group(1)
    version, checksum = selected(current, current_hash, version, checksum, (1, 6, 0), "server")
    recipe = re.sub(r"(?m)^PKG_VERSION:=\S+$", "PKG_VERSION:=" + version, recipe)
    recipe = re.sub(r"(?m)^PKG_HASH:=\S+$", "PKG_HASH:=" + checksum, recipe)
    path.write_text(recipe, encoding="utf-8")
    print(f"ocserv {version}; official source; SHA-256 {checksum}")
    return version, checksum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", choices=("client", "server"))
    parser.add_argument("--version", default="")
    parser.add_argument("--resolved-sha256", default="", help="Checksum already resolved by this workflow's validation job")
    parser.add_argument("--github-output", action="store_true")
    args = parser.parse_args()
    try:
        version, checksum = (prepare_client if args.component == "client" else prepare_server)(args.version, args.resolved_sha256)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    if args.github_output:
        with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8", newline="\n") as output:
            output.write(f"version={version}\nsha256={checksum}\n")


if __name__ == "__main__":
    main()
