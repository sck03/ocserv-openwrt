"""Apply explicit, checksum-pinned upstream version overrides to an isolated CI checkout."""
import argparse
import json
import re
from build_common import ROOT


def selected(current_version, current_hash, version, checksum, minimum):
    version = version.strip() or current_version
    checksum = checksum.strip().lower()
    if not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", version):
        raise ValueError("Use a numeric upstream release version")
    parts = tuple(int(value) for value in version.split("."))
    parts += (0,) * (3 - len(parts))
    if parts < minimum:
        raise ValueError("This project requires a newer upstream version")
    if not checksum:
        if version != current_version:
            raise ValueError("Supply the official source SHA-256 when selecting another upstream version")
        checksum = current_hash
    if not re.fullmatch(r"[0-9a-f]{64}", checksum):
        raise ValueError("SHA-256 must contain exactly 64 hexadecimal characters")
    return version, checksum


def prepare_client(version, checksum):
    path = ROOT / "scripts/sources.json"
    manifest = json.loads(path.read_text(encoding="utf-8"))
    previous = [name for name in manifest if name.startswith("openconnect-")]
    if len(previous) != 1:
        raise ValueError("Expected one OpenConnect source entry")
    previous = previous[0]
    current = previous.removeprefix("openconnect-").removesuffix(".tar.gz")
    version, checksum = selected(current, manifest[previous]["sha256"], version, checksum, (9, 21, 0))
    replacement = f"openconnect-{version}.tar.gz"
    output = {}
    for name, entry in manifest.items():
        output[replacement if name == previous else name] = (
            {"url": f"https://www.infradead.org/openconnect/download/{replacement}", "sha256": checksum}
            if name == previous else entry
        )
    path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"OpenConnect {version}; official source; SHA-256 {checksum}")


def prepare_server(version, checksum):
    path = ROOT / "server/openwrt/ocserv/Makefile"
    recipe = path.read_text(encoding="utf-8")
    current = re.search(r"(?m)^PKG_VERSION:=(\S+)$", recipe).group(1)
    current_hash = re.search(r"(?m)^PKG_HASH:=([0-9a-f]{64})$", recipe).group(1)
    version, checksum = selected(current, current_hash, version, checksum, (1, 5, 0))
    if version.count(".") != 2:
        raise ValueError("ocserv versions use major.minor.patch, for example 1.5.0")
    recipe = re.sub(r"(?m)^PKG_VERSION:=\S+$", "PKG_VERSION:=" + version, recipe)
    recipe = re.sub(r"(?m)^PKG_HASH:=\S+$", "PKG_HASH:=" + checksum, recipe)
    path.write_text(recipe, encoding="utf-8")
    print(f"ocserv {version}; official source; SHA-256 {checksum}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", choices=("client", "server"))
    parser.add_argument("--version", default="")
    parser.add_argument("--sha256", default="")
    args = parser.parse_args()
    try:
        (prepare_client if args.component == "client" else prepare_server)(args.version, args.sha256)
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
