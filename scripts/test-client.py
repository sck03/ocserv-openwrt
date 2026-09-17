"""Run the native client regressions on Windows using only system DLL search paths."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time
import zipfile

from build_common import ROOT, sha256, version


def run(command, cwd, environment, output, name, timeout=120):
    result = subprocess.run([str(value) for value in command], cwd=cwd, env=environment,
                            capture_output=True, timeout=timeout)
    stdout = result.stdout.decode("utf-8", "replace")
    stderr = result.stderr.decode("utf-8", "replace")
    (output / f"{name}.log").write_text(stdout + stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"{name} failed ({result.returncode}):\n{stdout}\n{stderr}")
    print(f"{name}: passed", flush=True)
    return json.loads(stdout.splitlines()[-1]) if stdout.strip().startswith("{") else {}


def smoke_test(executable, directory, environment, language):
    """Inspect only the process created here; never send input to another application's windows."""
    directory.mkdir()
    if language == "en":
        (directory / "profiles.json").write_text(json.dumps({
            "schema": 1, "settings": {"language": "en"}, "profiles": [],
        }), encoding="utf-8")
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    with subprocess.Popen([str(executable), "--data-dir", str(directory)],
                          cwd=executable.parent, env=environment) as process:
        window = None

        @callback_type
        def find_window(handle, _):
            nonlocal window
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(handle, ctypes.byref(pid))
            if pid.value == process.pid:
                name = ctypes.create_unicode_buffer(128)
                user32.GetClassNameW(handle, name, len(name))
                if name.value == "BulijieVPN.Main":
                    window = handle
                    return False
            return True

        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and process.poll() is None:
                user32.EnumWindows(find_window, 0)
                if window:
                    break
                time.sleep(0.05)
            if not window:
                raise RuntimeError(f"Portable executable did not create its {language} window")
            title = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(window, title, len(title))
            expected = "BulijieVPN" if language == "en" else "布利杰VPN"
            if title.value != expected:
                raise RuntimeError(f"Portable executable has incorrect {language} title: {title.value}")
            user32.PostMessageW(window, 0x0010, 0, 0)  # WM_CLOSE, this test process only.
            if process.wait(timeout=10):
                raise RuntimeError("Portable executable did not shut down cleanly")
            return {"language": language, "title": title.value, "passed": True}
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


def inspect_package(package, output, environment):
    with tempfile.TemporaryDirectory(prefix="package-", dir=output) as temporary:
        directory = Path(temporary)
        with zipfile.ZipFile(package) as archive:
            for member in archive.infolist():
                if not (directory / member.filename).resolve().is_relative_to(directory.resolve()):
                    raise RuntimeError("Package member escapes its directory")
            archive.extractall(directory)
        folders = list(directory.iterdir())
        if len(folders) != 1 or not folders[0].is_dir():
            raise RuntimeError("Unexpected portable package layout")
        folder = folders[0]
        info = json.loads((folder / "BUILDINFO.json").read_text(encoding="utf-8"))
        if info["version"] != version():
            raise RuntimeError("Package version differs from the tested source")
        if sha256(folder / "wintun.dll") != info["wintun_sha256"]:
            raise RuntimeError("Wintun checksum mismatch")
        if sha256(folder / "vpnc-script-win.js") != sha256(ROOT / "client/vendor/vpnc-script-win.js"):
            raise RuntimeError("Package has an outdated routing script")
        if (folder / "data").exists() or (folder / "BridgeVPN.ini").exists():
            raise RuntimeError("Package contains local or obsolete configuration")
        subprocess.run([sys.executable, str(ROOT / "scripts/audit-pe.py"),
                        str(folder / "布利杰VPN.exe"), "--output", str(output / "package-imports.json")],
                       env=environment, check=True, capture_output=True)
        report = json.loads((output / "package-imports.json").read_text(encoding="utf-8"))
        if report["architecture"] != info["architecture"]:
            raise RuntimeError("Package architecture mismatch")
        info["startup"] = [smoke_test(folder / "布利杰VPN.exe", directory / language, environment, language)
                           for language in ("zh-CN", "en")]
        return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--package", type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Run native regressions on Windows; Linux is used for cross compilation.")
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, PYTHONUTF8="1")
    system = Path(os.environ["SystemRoot"])
    environment["PATH"] = os.pathsep.join(str(p) for p in (system / "System32", system))
    report = {"version": version(), "platform": platform.platform(), "tests": {}}
    if args.package:
        report["package"] = inspect_package(args.package, output, environment)
    for name, executable in (("profiles", "native_model_tests.exe"), ("ui", "native_ui_tests.exe"),
                             ("script_runner", "native_script_tests.exe")):
        report["tests"][name] = run([build / executable, output / name], build, environment, output, name)
    report["tests"]["authentication"] = run(
        [sys.executable, ROOT / "client/tests/auth_integration.py", "--client", build / "native_session_tests.exe",
         "--output", output / "authentication"], ROOT, environment, output, "authentication", timeout=300)
    report["passed"] = True
    report["boundary"] = "Native Windows UI, DPAPI and loopback TLS; no adapter or routes were created."
    (output / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
