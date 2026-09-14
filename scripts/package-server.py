"""Collect only the ocserv and LuCI packages for one SDK, plus their source and build metadata."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
from build_common import ROOT, sha256

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("branch", choices=("24.10", "25.12"))
parser.add_argument("sdk", type=Path)
args = parser.parse_args()
sdk = args.sdk.resolve()
entry = json.loads((ROOT / "server/openwrt/sdks.json").read_text())[args.branch]
output = ROOT / "dist" / f"ocserv-1.5.0-openwrt-{args.branch}-aarch64_generic"
output.mkdir(parents=True, exist_ok=True)
fmt = entry["format"]
patterns = (f"ocserv*1.5.0*.{fmt}", f"luci-app-ocserv[._-]*.{fmt}", f"luci-i18n-ocserv-zh-cn*.{fmt}")
packages = sorted({path for pattern in patterns for path in (sdk / "bin/packages").rglob(pattern)})
for prefix in ("ocserv", "luci-app-ocserv-easy", "luci-i18n-ocserv-zh-cn"):
    if not any(path.name.startswith(prefix) for path in packages):
        raise RuntimeError(f"Missing {prefix} {fmt} package")
if not any(path.name.startswith("luci-app-ocserv") and not path.name.startswith("luci-app-ocserv-easy") for path in packages):
    raise RuntimeError("Missing the upstream LuCI application package")
for package in packages:
    shutil.copyfile(package, output / package.name)

executables = list((sdk / "build_dir").glob("target-*/ocserv-1.5.0/ipkg-install/usr/sbin/ocserv"))
if len(executables) != 1:
    raise RuntimeError("Cannot locate the built ocserv ELF executable")
header = executables[0].read_bytes()[:64]
if header[:5] != b"\x7fELF\x02" or header[5] != 1 or struct.unpack_from("<H", header, 18)[0] != 183:
    raise RuntimeError("ocserv must be a 64-bit little-endian AArch64 ELF executable")
dynamic = subprocess.check_output(["readelf", "-d", str(executables[0])], text=True)
(output / "ocserv-elf-dependencies.txt").write_text(dynamic, encoding="utf-8")
revisions = {}
for feed in ("packages", "luci"):
    revisions[feed] = subprocess.check_output(["git", "-C", str(sdk / "feeds" / feed), "rev-parse", "HEAD"], text=True).strip()
info = {"ocserv": "1.5.0", "sdk": entry, "architecture": "aarch64_generic",
        "commit": os.environ.get("GITHUB_SHA", "local"), "feeds": revisions,
        "boundary": "SDK compile/ELF validation; OPL/Flippy device ABI and real VPN traffic require device testing."}
(output / "BUILDINFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
shutil.copyfile(sdk / "feeds.conf", output / "feeds.conf.build")
for filename in ("preflight-n1.sh", "upgrade-ocserv-24.10.sh", "diagnose-n1.sh", "export-profile.sh", "add-user.sh"):
    shutil.copyfile(ROOT / "server/tools" / filename, output / filename)
shutil.copyfile(ROOT / "docs/OPENWRT-N1.md", output / "INSTALL-zh-CN.md")
source = output / "source"
source.mkdir(exist_ok=True)
archive = sdk / "dl/ocserv-1.5.0.tar.xz"
if sha256(archive) != "42ced08958b9576ab134fcb7bdc7f8df5e13214fd147855f99021fedcf0eedbe":
    raise RuntimeError("ocserv corresponding source checksum mismatch")
shutil.copyfile(archive, source / archive.name)
with tarfile.open(source / "openwrt-recipes.tar.gz", "w:gz") as tar:
    tar.add(ROOT / "server/openwrt", arcname="openwrt")
subprocess.run(["git", "-C", str(sdk / "feeds/luci"), "archive", "--format=tar.gz",
                "--output=" + str(source / "luci-upstream-source.tar.gz"), "HEAD"], check=True)
files = sorted(path for path in output.rglob("*") if path.is_file() and path.name != "SHA256SUMS")
(output / "SHA256SUMS").write_text("".join(f"{sha256(path)}  {path.relative_to(output).as_posix()}\n" for path in files), encoding="utf-8")
print(f"Collected {len(packages)} {fmt} packages in {output}; kernel modules are not included.")
