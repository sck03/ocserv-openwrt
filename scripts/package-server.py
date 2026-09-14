"""Collect only the ocserv and LuCI packages for one SDK, plus their source and build metadata."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
from build_common import ROOT, sha256

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("sdk", type=Path)
args = parser.parse_args()
sdk = args.sdk.resolve()
entry = json.loads((sdk / "sdk-info.json").read_text(encoding="utf-8"))
recipe = (ROOT / "server/openwrt/ocserv/Makefile").read_text(encoding="utf-8")
ocserv_version = re.search(r"(?m)^PKG_VERSION:=(\d+\.\d+\.\d+)$", recipe).group(1)
source_hash = re.search(r"(?m)^PKG_HASH:=([a-f0-9]{64})$", recipe).group(1)
output = ROOT / "dist" / f"ocserv-{ocserv_version}-openwrt-{entry['version']}-aarch64_generic"
output.mkdir(parents=True, exist_ok=True)
fmt = "apk" if re.search(r"(?m)^CONFIG_USE_APK=y$", (sdk / ".config").read_text()) else "ipk"
entry["format"] = fmt
patterns = (f"ocserv*{ocserv_version}*.{fmt}", f"luci-app-ocserv[._-]*.{fmt}", f"luci-i18n-ocserv-zh-cn*.{fmt}")
packages = sorted({path for pattern in patterns for path in (sdk / "bin/packages").rglob(pattern)})
for prefix in ("ocserv", "luci-app-ocserv-easy", "luci-i18n-ocserv-zh-cn"):
    if not any(path.name.startswith(prefix) for path in packages):
        raise RuntimeError(f"Missing {prefix} {fmt} package")
if not any(path.name.startswith("luci-app-ocserv") and not path.name.startswith("luci-app-ocserv-easy") for path in packages):
    raise RuntimeError("Missing the upstream LuCI application package")
for package in packages:
    shutil.copyfile(package, output / package.name)

# SDK CONFIG_AUTOREMOVE deletes ipkg-install after packaging but retains .pkgdir.
# This staged package tree contains the installed binary for the ELF audit.
executables = list((sdk / "build_dir").glob(f"target-*/ocserv-{ocserv_version}/.pkgdir/ocserv/usr/sbin/ocserv"))
if len(executables) != 1:
    raise RuntimeError("Cannot locate the staged ocserv ELF executable in the SDK package cache")
header = executables[0].read_bytes()[:64]
if header[:5] != b"\x7fELF\x02" or header[5] != 1 or struct.unpack_from("<H", header, 18)[0] != 183:
    raise RuntimeError("ocserv must be a 64-bit little-endian AArch64 ELF executable")
dynamic = subprocess.check_output(["readelf", "-d", str(executables[0])], text=True)
(output / "ocserv-elf-dependencies.txt").write_text(dynamic, encoding="utf-8")
ui_validation = output / "validation/ui"
ui_validation.mkdir(parents=True, exist_ok=True)
for filename in ("app.js", "style.css"):
    assets = list((sdk / "build_dir").glob(
        "target-*/luci-app-ocserv-easy/.pkgdir/luci-app-ocserv-easy/www/luci-static/resources/ocserv-easy/" + filename))
    if len(assets) != 1:
        raise RuntimeError(f"Cannot locate the staged management page asset: {filename}")
    if filename == "style.css":
        original = ROOT / "server/openwrt/luci-app-ocserv-easy/htdocs/luci-static/resources/ocserv-easy/style.css"
        if sha256(assets[0]) != sha256(original):
            raise RuntimeError("The SDK modified the management CSS; keep LUCI_MINIFY_CSS disabled")
    shutil.copyfile(assets[0], ui_validation / filename)
revisions = {}
for feed in ("packages", "luci"):
    revisions[feed] = subprocess.check_output(["git", "-C", str(sdk / "feeds" / feed), "rev-parse", "HEAD"], text=True).strip()
info = {"ocserv": ocserv_version, "sdk": entry, "architecture": "aarch64_generic",
        "commit": os.environ.get("GITHUB_SHA", "local"), "feeds": revisions,
        "boundary": "SDK compile/ELF validation; OPL/Flippy device ABI and real VPN traffic require device testing."}
(output / "BUILDINFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
shutil.copyfile(sdk / "feeds.conf", output / "feeds.conf.build")
for filename in ("preflight-n1.sh", "upgrade-ocserv-24.10.sh", "diagnose-n1.sh", "export-profile.sh", "add-user.sh"):
    shutil.copyfile(ROOT / "server/tools" / filename, output / filename)
shutil.copyfile(ROOT / "docs/OPENWRT-N1.md", output / "INSTALL-zh-CN.md")
shutil.copyfile(ROOT / "docs/SERVER-UI.md", output / "SERVER-UI.md")
source = output / "source"
source.mkdir(exist_ok=True)
archive = sdk / "dl" / f"ocserv-{ocserv_version}.tar.xz"
if sha256(archive) != source_hash:
    raise RuntimeError("ocserv corresponding source checksum mismatch")
shutil.copyfile(archive, source / archive.name)
with tarfile.open(source / "openwrt-recipes.tar.gz", "w:gz") as tar:
    tar.add(ROOT / "server/openwrt", arcname="openwrt")
subprocess.run(["git", "-C", str(sdk / "feeds/luci"), "archive", "--format=tar.gz",
                "--output=" + str(source / "luci-upstream-source.tar.gz"), "HEAD"], check=True)
files = sorted(path for path in output.rglob("*") if path.is_file() and path.name != "SHA256SUMS")
(output / "SHA256SUMS").write_text("".join(f"{sha256(path)}  {path.relative_to(output).as_posix()}\n" for path in files), encoding="utf-8")
bundle = Path(shutil.make_archive(str(output), "zip", root_dir=output.parent, base_dir=output.name))
(output.parent / f"SHA256SUMS-openwrt-{entry['version']}.txt").write_text(
    f"{sha256(bundle)}  {bundle.name}\n", encoding="ascii")
print(f"Collected {len(packages)} {fmt} packages in {output}; kernel modules are not included.")
print(f"Created {bundle} ({bundle.stat().st_size} bytes)")
