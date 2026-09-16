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
if not re.fullmatch(r"25\.12\.\d+", entry["version"]):
    raise RuntimeError("Only OpenWrt 25.12.x packages are supported")
recipe = (ROOT / "server/openwrt/ocserv/Makefile").read_text(encoding="utf-8")
ocserv_version = re.search(r"(?m)^PKG_VERSION:=(\d+\.\d+\.\d+)$", recipe).group(1)
source_hash = re.search(r"(?m)^PKG_HASH:=([a-f0-9]{64})$", recipe).group(1)
ui_recipe = (ROOT / "server/openwrt/luci-app-ocserv-easy/Makefile").read_text(encoding="utf-8")
ui_version = re.search(r"(?m)^PKG_VERSION:=(\d+\.\d+\.\d+)$", ui_recipe).group(1)
ui_release = re.search(r"(?m)^PKG_RELEASE:=(\d+)$", ui_recipe).group(1)
ocserv_release = re.search(r"(?m)^PKG_RELEASE:=(\d+)$", recipe).group(1)
output = ROOT / "dist" / f"ocserv-{ocserv_version}-openwrt-{entry['version']}-aarch64_generic-ui-{ui_version}"
output.mkdir(parents=True, exist_ok=True)
if not re.search(r"(?m)^CONFIG_USE_APK=y$", (sdk / ".config").read_text()):
    raise RuntimeError("The OpenWrt 25.12 SDK must produce APK packages")
fmt = "apk"
entry["format"] = fmt
patterns = (f"ocserv-{ocserv_version}-r{ocserv_release}.{fmt}", f"luci-app-ocserv-easy-{ui_version}-r{ui_release}.{fmt}")
packages = sorted({path for pattern in patterns for path in (sdk / "bin/packages").rglob(pattern)})
for pattern in patterns:
    if len([path for path in packages if path.name == pattern]) != 1:
        raise RuntimeError(f"Expected exactly one current package: {pattern}")
for old in output.glob("*.apk"):
    if old.name not in patterns:
        raise RuntimeError(f"Stale package in output directory: {old.name}; use a clean output directory")
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
server_root = executables[0].parents[2]
if sha256(server_root / "etc/init.d/ocserv") != sha256(ROOT / "server/openwrt/ocserv/files/ocserv.init"):
    raise RuntimeError("The staged ocserv startup/certificate script is stale")
ui_roots = list((sdk / "build_dir").glob("target-*/luci-app-ocserv-easy/.pkgdir/luci-app-ocserv-easy"))
if len(ui_roots) != 1:
    raise RuntimeError("Cannot locate the staged management package")
for relative in ("etc/init.d/ocserv-easy-guard", "usr/libexec/ocserv-easy-guard", "usr/libexec/ocserv-easy-repair-users", "usr/share/ocserv-easy/guard.nft.in"):
    if sha256(ui_roots[0] / relative) != sha256(ROOT / "server/openwrt/luci-app-ocserv-easy/root" / relative):
        raise RuntimeError("Stale guard runtime file: " + relative)
for relative in ("etc/init.d/ocserv-easy-guard", "usr/libexec/ocserv-easy-guard", "usr/libexec/ocserv-easy-repair-users"):
    if not (ui_roots[0] / relative).stat().st_mode & 0o111:
        raise RuntimeError("Guard runtime is not executable: " + relative)
for name in ("guard.lua", "process.lua", "backend.lua", "logic.lua"):
    relative = "usr/lib/lua/luci/model/ocserv_easy/" + name
    if sha256(ui_roots[0] / relative) != sha256(ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc/model/ocserv_easy" / name):
        raise RuntimeError("Stale management module: " + name)
for relative in ("controller/ocserv_easy.lua", "view/ocserv_easy/index.htm"):
    if sha256(ui_roots[0] / "usr/lib/lua/luci" / relative) != sha256(ROOT / "server/openwrt/luci-app-ocserv-easy/luasrc" / relative):
        raise RuntimeError("Stale management entry point: " + relative)
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
info = {"ocserv": ocserv_version, "management_ui": ui_version, "sdk": entry, "architecture": "aarch64_generic",
        "apk_architectures": ["aarch64", "aarch64_generic"], "packages": [p.name for p in packages],
        "commit": os.environ.get("GITHUB_SHA", "local"), "feeds": revisions,
        "boundary": "SDK compile/ELF validation; OPL/Flippy device ABI and real VPN traffic require device testing."}
(output / "BUILDINFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
shutil.copyfile(sdk / "feeds.conf", output / "feeds.conf.build")
for filename in ("install.sh", "preflight-n1.sh", "diagnose-n1.sh", "export-profile.sh", "add-user.sh"):
    shutil.copyfile(ROOT / "server/tools" / filename, output / filename)
shutil.copyfile(ROOT / "docs/OPENWRT-N1.md", output / "OPENWRT-N1.md")
shutil.copyfile(ROOT / "docs/SERVER-UI.md", output / "SERVER-UI.md")
shutil.copyfile(ROOT / "docs/VPN-ONLY-OPENCLASH.md", output / "VPN-ONLY-OPENCLASH.md")
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
