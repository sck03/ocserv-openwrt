"""Package one Windows architecture, leaving the signed Wintun binary unchanged."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile
from build_common import ROOT, sha256, source_directory, version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x64", "x86"), required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--strip", required=True)
    args = parser.parse_args()
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    name = f"BulijieVPN-{version()}-windows-{args.arch}"
    build = args.build.resolve()
    validation = dist / f"validation-{args.arch}"
    validation.mkdir(exist_ok=True)
    licenses = {
        "LICENSE": "Application-GPL-3.0.txt",
        f".deps/sources/{source_directory('openconnect')}/COPYING.LGPL": "OpenConnect-LGPL-2.1.txt",
        f".deps/sources/{source_directory('gnutls')}/COPYING.LESSERv2": "GnuTLS-LGPL-2.1.txt",
        f".deps/sources/{source_directory('gnutls')}/COPYING": "GnuTLS-GPL-3.0.txt",
        f".deps/sources/{source_directory('gmp')}/COPYING.LESSERv3": "GMP-LGPL-3.0.txt",
        f".deps/sources/{source_directory('nettle')}/COPYING.LESSERv3": "Nettle-LGPL-3.0.txt",
        f".deps/sources/{source_directory('stoken')}/COPYING.LIB": "stoken-LGPL-2.1.txt",
        f".deps/sources/{source_directory('libxml2')}/Copyright": "libxml2-Copyright.txt",
        f".deps/sources/{source_directory('zlib')}/LICENSE": "zlib-LICENSE.txt",
        ".deps/sources/wintun/LICENSE.txt": "Wintun-Prebuilt-License.txt",
        "client/vendor/json-LICENSE.MIT": "nlohmann-json-MIT.txt",
        "client/vendor/openconnect-gui-LICENSE.txt": "OpenConnect-GUI-GPL-2.0.txt",
        "client/vendor/vpnc-scripts-COPYING": "vpnc-scripts-GPL-2.0.txt",
        "client/vendor/GCC-RUNTIME-EXCEPTION.txt": "GCC-Runtime-Library-Exception.txt",
    }
    with tempfile.TemporaryDirectory(prefix=name + "-", dir=dist) as temporary:
        folder = Path(temporary) / name
        folder.mkdir()
        license_dir = folder / "licenses"
        license_dir.mkdir()
        for filename in ("布利杰VPN.exe", "wintun.dll"):
            shutil.copyfile(build / filename, folder / filename)
        for filename in ("client/vendor/vpnc-script-win.js", "THIRD-PARTY-NOTICES.md"):
            shutil.copyfile(ROOT / filename, folder / Path(filename).name)
        for source, destination in licenses.items():
            shutil.copyfile(ROOT / source, license_dir / destination)
        if os.name == "nt":
            runtime = ROOT / f".tools/{args.arch}/w64devkit/COPYING.MinGW-w64-runtime.txt"
            shutil.copyfile(runtime, license_dir / "MinGW-w64-runtime.txt")
        else:
            shutil.copyfile("/usr/share/doc/mingw-w64-common/copyright", license_dir / "MinGW-w64-runtime.txt")
            notices = sorted(Path("/usr/share/doc").glob("gcc*mingw-w64-base/copyright"))
            if not notices:
                raise RuntimeError("Missing GCC runtime license notices")
            shutil.copyfile(notices[-1], license_dir / "GCC-runtime.txt")
        exe = folder / "布利杰VPN.exe"
        subprocess.run([args.strip, "--strip-all", str(exe)], check=True)
        data = exe.read_bytes()
        machine = struct.unpack_from("<H", data, struct.unpack_from("<I", data, 0x3C)[0] + 4)[0]
        if machine != {"x64": 0x8664, "x86": 0x14C}[args.arch]:
            raise RuntimeError("Executable architecture does not match the package")
        subprocess.run([os.sys.executable, str(ROOT / "scripts/audit-pe.py"), str(exe),
                        "--output", str(validation / "imports.json")], check=True)
        source_dll = ROOT / ".deps/sources/wintun/bin" / ("amd64" if args.arch == "x64" else "x86") / "wintun.dll"
        if sha256(folder / "wintun.dll") != sha256(source_dll):
            raise RuntimeError("The official Wintun DLL was changed or has the wrong architecture")
        (folder / "使用说明.txt").write_text(
            f"布利杰VPN {version()}\n\n"
            "1. 完整解压，运行“布利杰VPN.exe”。64 位 Windows 用 x64，32 位用 x86。\n"
            "2. 在“配置”菜单新建连接，或直接填写实际网关地址，支持端口和用户组路径。\n"
            "3. 点击“连接”，按需完成 Windows 管理员授权，随后按服务器提示填写用户名、密码、分组或验证码。\n"
            "4. 未知证书需核对指纹后确认；指定 CA 或固定指纹不匹配时会拒绝连接。\n"
            "5. 高级配置中的“记住密码”使用当前 Windows 用户的 DPAPI 加密。验证码不会作为密码保存。\n"
            "6. “VPN 信息”显示地址和流量，“查看日志”可复制诊断日志；设置菜单可切换中文/English及托盘行为。\n\n"
            "导入/导出 .bvpn 可共享地址、连接选项和公有证书，不包含密码、令牌或私钥。\n"
            "配置保存在程序旁的 data 目录。复制到其他 Windows 用户后需要重新输入加密凭据。\n"
            "请完整保留 wintun.dll、vpnc-script-win.js 和 licenses，不要混用不同架构的文件。\n"
            "Win7 SP1 为兼容目标，仍需实机验证；VPN 驱动需要相应系统更新和管理员权限。\n\n"
            "Create a profile or enter your gateway, connect and answer the server's authentication prompts. Verify unknown certificate fingerprints before approval.\n"
            "Settings selects Chinese/English and tray behavior. VPN Info shows addresses and traffic; View Log provides diagnostics.\n"
            "Profiles live in data/. Saved passwords and tokens use current-user DPAPI. Exports contain public connection details only.\n"
            "Keep wintun.dll and vpnc-script-win.js beside the executable. No Qt, .NET or compiler runtime installation is needed.\n"
            "Obtain the matching corresponding-source artifact before redistributing. See licenses/.\n",
            encoding="utf-8-sig",
        )
        info = {"version": version(), "architecture": args.arch, "commit": os.environ.get("GITHUB_SHA", "local"),
                "components": {name: source_directory(name) for name in ("openconnect", "gnutls", "gmp", "nettle", "stoken", "libxml2", "zlib")},
                "wintun_sha256": sha256(source_dll),
                "vpnc_script_sha256": sha256(folder / "vpnc-script-win.js"),
                "patches": {p.name: sha256(p) for p in sorted((ROOT / "client/patches").glob("*.patch"))},
                "validation": "Compile and PE audit; Windows runtime reports accompany the workflow. Real VPN/Win7 acceptance is separate."}
        (folder / "BUILDINFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        archive = dist / (name + ".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=7) as output:
            for file in sorted(folder.rglob("*")):
                if file.is_file():
                    output.write(file, name + "/" + file.relative_to(folder).as_posix())
    for filename in ("native_model_tests.exe", "native_session_tests.exe", "native_ui_tests.exe", "native_script_tests.exe"):
        shutil.copyfile(build / filename, validation / filename)
    (dist / f"SHA256SUMS-windows-{args.arch}.txt").write_text(f"{sha256(archive)}  {archive.name}\n", encoding="ascii")
    print(f"Created {archive} ({archive.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
