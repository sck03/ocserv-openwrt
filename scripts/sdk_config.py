"""Resolve released OpenWrt 25.12.x SDKs from the official download index."""
import json
import re
import urllib.request
from build_common import ROOT

BASE = "https://downloads.openwrt.org/releases/"


def read_text(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read().decode("utf-8")


def resolve_sdk(requested="", fetch=read_text):
    series = "25.12"
    pinned = json.loads((ROOT / "server/openwrt/sdks.json").read_text(encoding="utf-8"))[series]
    requested = requested.strip() or pinned["version"]
    if requested == "auto":
        releases = re.findall(r'href="(25\.12\.\d+)/"', fetch(BASE))
        if not releases:
            raise ValueError(f"No released OpenWrt {series}.x version found")
        requested = max(releases, key=lambda value: tuple(map(int, value.split("."))))
    if not re.fullmatch(r"25\.12\.\d+", requested):
        raise ValueError("Use a released 25.12.x version or auto; other series, RCs and snapshots are not selected")
    if requested == pinned["version"]:
        return dict(pinned, series=series, checksum_origin="repository pin")
    base = BASE + requested + "/targets/armsr/armv8/"
    names = set(re.findall(r'href="(openwrt-sdk-' + re.escape(requested) +
                           r'-armsr-armv8_[A-Za-z0-9_.+-]+\.Linux-x86_64\.tar\.(?:zst|xz))"', fetch(base)))
    if len(names) != 1:
        raise ValueError("Expected exactly one Linux x86_64 SDK for armsr/armv8")
    name = names.pop()
    checksums = [match.group(1) for line in fetch(base + "sha256sums").splitlines()
                 if (match := re.fullmatch(r"([0-9a-fA-F]{64})\s+\*?" + re.escape(name), line))]
    if len(checksums) != 1:
        raise ValueError("SDK has no unique entry in the official SHA256SUMS")
    return {"series": series, "version": requested, "url": base + name, "sha256": checksums[0].lower(),
            "checksum_origin": base + "sha256sums"}
