"""Prepare a verified, release-specific armsr/armv8 OpenWrt SDK on Linux."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from urllib.parse import urlsplit
from build_common import ROOT, download_verified
from sdk_config import resolve_sdk

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--version", default="", help="Released 25.12.x version; auto selects the newest stable 25.12 release")
parser.add_argument("--output", type=Path, required=True, help="Empty SDK directory outside the source checkout")
args = parser.parse_args()
if os.name == "nt":
    raise SystemExit("The OpenWrt SDK needs a Linux build host.")
entry = resolve_sdk(args.version)
name = Path(urlsplit(entry["url"]).path).name
archive = ROOT / ".tools/downloads" / name
download_verified(archive, entry["url"], entry["sha256"])
destination = args.output.resolve()
if destination.is_relative_to(ROOT) or ROOT.is_relative_to(destination):
    raise SystemExit("Keep the SDK outside the source Git checkout to avoid metadata scanning conflicts.")
destination.mkdir(parents=True, exist_ok=True)
if any(destination.iterdir()):
    raise SystemExit("Use a clean SDK destination: " + str(destination))
command = ["tar"]
if archive.name.endswith(".zst"):
    command.append("--zstd")
subprocess.run(command + ["-xf", str(archive), "--strip-components=1", "-C", str(destination)], check=True)
(destination / "sdk-info.json").write_text(json.dumps(entry, indent=2) + "\n", encoding="utf-8")
print(f"Verified and extracted OpenWrt {entry['version']} SDK to {destination}")
