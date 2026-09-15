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
        f".deps/sources/{source_directory('openssl')}/LICENSE.txt": "OpenSSL-Apache-2.0.txt",
        f".deps/sources/{source_directory('libxml2')}/Copyright": "libxml2-Copyright.txt",
        f".deps/sources/{source_directory('zlib')}/LICENSE": "zlib-LICENSE.txt",
        ".deps/sources/wintun/LICENSE.txt": "Wintun-Prebuilt-License.txt",
    }
    with tempfile.TemporaryDirectory(prefix=name + "-", dir=dist) as temporary:
        folder = Path(temporary) / name
        folder.mkdir()
        license_dir = folder / "licenses"
        license_dir.mkdir()
        for filename in ("布利杰VPN.exe", "wintun.dll"):
            shutil.copyfile(build / filename, folder / filename)
        for filename in ("config/BridgeVPN.ini", "THIRD-PARTY-NOTICES.md"):
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
            "2. 填写服务器地址；未连接时可以直接修改。也可选用 .bvpn 或公共 CA。\n"
            "3. 填写账号密码并连接。首次遇到未知证书时核对指纹，点击“信息准确，记住并连接”；确认前不发送密码。按需完成管理员授权。\n"
            "4. “记住账号和密码”使用当前 Windows 用户的凭据管理器。\n"
            "5. 连接后点击“断开连接”。最小化进入托盘，单击托盘图标还原；关闭窗口先断开再退出。\n"
            "6. 出现错误时可用“复制诊断”向管理员提供诊断信息。\n\n"
            "“导出配置”可保存地址、连接选项和已确认的指纹或公共 CA；不包含账号密码和私钥。其他客户端可用“导入配置”打开该 .bvpn 文件。\n\n"
            "必须保留同目录的 wintun.dll，不要混用不同架构的文件。\n"
            "默认地址是部署示例，请使用管理员提供的实际地址。\n"
            "Win7 SP1 为兼容目标，仍需实机验证；VPN 驱动需要相应系统更新和管理员权限。\n\n"
            "Enter your server address and connect. Verify an unknown certificate and choose Accurate information to remember its key. Credentials are sent after certificate approval. Profile/CA import is optional.\n"
            "Export profile saves the address, connection options and public trust for import on another client; no credentials or private keys are included.\n"
            "Minimize to the tray; click its icon to restore. Closing the window disconnects first.\n"
            "Obtain the matching corresponding-source artifact before redistributing. See licenses/.\n",
            encoding="utf-8-sig",
        )
        info = {"version": version(), "architecture": args.arch, "commit": os.environ.get("GITHUB_SHA", "local"),
                "components": {name: source_directory(name) for name in ("openconnect", "openssl", "libxml2", "zlib")},
                "wintun_sha256": sha256(source_dll), "validation": "Compile and PE audit; real VPN/Win7 acceptance is separate."}
        (folder / "BUILDINFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        archive = dist / (name + ".zip")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=7) as output:
            for file in sorted(folder.rglob("*")):
                if file.is_file():
                    output.write(file, name + "/" + file.relative_to(folder).as_posix())
    shutil.copyfile(build / "bridge_tests.exe", validation / "bridge_tests.exe")
    (dist / f"SHA256SUMS-windows-{args.arch}.txt").write_text(f"{sha256(archive)}  {archive.name}\n", encoding="ascii")
    print(f"Created {archive} ({archive.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
