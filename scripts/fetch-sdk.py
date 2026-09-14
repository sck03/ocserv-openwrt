"""Prepare a checksum-pinned, release-specific armsr/armv8 OpenWrt SDK on Linux."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from urllib.parse import urlsplit
from build_common import ROOT, download_verified

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("branch", choices=("24.10", "25.12"))
args = parser.parse_args()
if os.name == "nt":
    raise SystemExit("The OpenWrt SDK needs a Linux build host.")
entry = json.loads((ROOT / "server/openwrt/sdks.json").read_text())[args.branch]
name = Path(urlsplit(entry["url"]).path).name
archive = ROOT / ".tools/downloads" / name
download_verified(archive, entry["url"], entry["sha256"])
destination = ROOT / ".tools" / ("sdk-" + args.branch)
destination.mkdir(parents=True, exist_ok=True)
if any(destination.iterdir()):
    raise SystemExit("Use a clean SDK destination: " + str(destination))
subprocess.run(["tar", "--zstd", "-xf", str(archive), "--strip-components=1", "-C", str(destination)], check=True)
print(f"Verified and extracted OpenWrt {entry['version']} SDK to {destination}")
