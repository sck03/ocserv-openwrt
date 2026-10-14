"""Audit the actual DEB payload, linked libraries and headless version command."""
import argparse
import configparser
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile


def audit(package):
    package = package.resolve()

    def field(name):
        return subprocess.check_output(["dpkg-deb", "--field", package, name], text=True).strip()

    architecture = field("Architecture")
    machine = {"amd64": 62, "arm64": 183}[architecture]
    assert field("Package") == "linkora"
    assert architecture == subprocess.check_output(["dpkg", "--print-architecture"], text=True).strip()
    depends = field("Depends")
    for dependency in ("libc6", "libstdc++6", "libgtk-3-0", "libglib2.0-0", "libnm0", "libsecret-1-0",
                       "network-manager (>= 1.42)", "network-manager-openconnect-gnome"):
        assert dependency in depends, f"Missing runtime dependency: {dependency}"

    payload = subprocess.check_output(["dpkg-deb", "--fsys-tarfile", package])
    expected = {
        "usr/bin/linkora",
        "usr/share/applications/io.github.sck03.linkora.desktop",
        "usr/share/icons/hicolor/scalable/apps/io.github.sck03.linkora.svg",
        "usr/share/doc/linkora/copyright",
        "usr/share/doc/linkora/DESKTOP.md",
        "usr/share/doc/linkora/THIRD-PARTY-NOTICES.md",
    }
    with tarfile.open(fileobj=io.BytesIO(payload)) as contents:
        files = {item.name.removeprefix("./"): item for item in contents if not item.isdir()}
        assert set(files) == expected, f"Unexpected package payload: {set(files) ^ expected}"
        assert all(item.isfile() and item.uid == item.gid == 0 for item in files.values())
        desktop = configparser.ConfigParser(interpolation=None)
        desktop.read_string(contents.extractfile(files["usr/share/applications/io.github.sck03.linkora.desktop"]).read().decode("utf-8"))
        entry = desktop["Desktop Entry"]
        assert entry["Name"] == "Linkora" and entry["Exec"] == "linkora"
        assert entry["Icon"] == "io.github.sck03.linkora"
        binary = files["usr/bin/linkora"]
        assert binary.mode & 0o111, "Client is not executable"
        with contents.extractfile(binary) as executable:
            data = executable.read()
            header = data[:64]
        assert b"Name=Linkora\n" in data, "Login startup entry must use the current product name"
        assert len(header) == 64 and header[:6] == b"\x7fELF\x02\x01", "Expected a real 64-bit ELF client"
        assert int.from_bytes(header[18:20], "little") == machine, "Wrong executable architecture"
        installed_bytes = sum(item.size for item in files.values())
        assert int(field("Installed-Size")) * 1024 >= installed_bytes

    with tempfile.TemporaryDirectory(prefix="linux-package-") as work:
        subprocess.run(["dpkg-deb", "--extract", package, work], check=True)
        executable = Path(work) / "usr/bin/linkora"
        version = subprocess.check_output([executable, "--version"], text=True).strip()
        assert version == "Linkora " + field("Version")
        helper = subprocess.check_output([executable, "--check-runtime"], text=True).strip()
        assert Path(helper).is_absolute() and Path(helper).is_file(), "Missing installed authentication helper"
        libraries = subprocess.check_output(["ldd", executable], text=True)
        assert "not found" not in libraries, libraries
        sections = subprocess.check_output(["readelf", "--sections", executable], text=True)
        assert ".debug_" not in sections and ".symtab" not in sections, "Unstripped release client"

    report = {
        "architecture": architecture, "package_bytes": package.stat().st_size,
        "executable_bytes": binary.size, "installed_bytes": installed_bytes,
        "depends": depends, "files": sorted(files), "version": version,
        "authentication_helper": helper, "passed": True,
    }
    output = Path("test-results/linux-package.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    audit(parser.parse_args().package)
