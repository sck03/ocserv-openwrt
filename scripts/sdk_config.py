"""Resolve released OpenWrt 25.12.x SDKs from the official download index."""
import argparse
import json
import os
from pathlib import Path
import re
import urllib.request
from build_common import ROOT

BASE = "https://downloads.openwrt.org/releases/"
TARGETS = json.loads((ROOT / "server/openwrt/targets.json").read_text(encoding="utf-8"))
DEFAULT_TARGET = "armsr/armv8"


def target_config(target):
    if target not in TARGETS:
        raise ValueError("Unsupported OpenWrt target: " + target)
    return dict(TARGETS[target], target=target)


def read_text(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read().decode("utf-8")


def resolve_sdk(requested="", fetch=read_text, target=DEFAULT_TARGET):
    configuration = target_config(target)
    series = "25.12"
    requested = requested.strip() or "auto"
    if requested == "auto":
        releases = re.findall(r'href="(25\.12\.\d+)/"', fetch(BASE))
        if not releases:
            raise ValueError(f"No released OpenWrt {series}.x version found")
        requested = max(releases, key=lambda value: tuple(map(int, value.split("."))))
    if not re.fullmatch(r"25\.12\.\d+", requested):
        raise ValueError("Use a released 25.12.x version or auto; other series, RCs and snapshots are not selected")
    base = BASE + requested + "/targets/" + target + "/"
    names = set(re.findall(r'href="(openwrt-sdk-' + re.escape(requested) +
                           '-' + re.escape(target.replace('/', '-')) + r'_[A-Za-z0-9_.+-]+\.Linux-x86_64\.tar\.(?:zst|xz))"', fetch(base)))
    if len(names) != 1:
        raise ValueError("Expected exactly one Linux x86_64 SDK for " + target)
    name = names.pop()
    checksums = [match.group(1) for line in fetch(base + "sha256sums").splitlines()
                 if (match := re.fullmatch(r"([0-9a-fA-F]{64})\s+\*?" + re.escape(name), line))]
    if len(checksums) != 1:
        raise ValueError("SDK has no unique entry in the official SHA256SUMS")
    return {**configuration, "format": "apk", "series": series, "version": requested, "url": base + name, "sha256": checksums[0].lower(),
            "checksum_origin": base + "sha256sums"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="")
    parser.add_argument("--target", default=DEFAULT_TARGET, choices=TARGETS)
    parser.add_argument("--github-output", action="store_true")
    args = parser.parse_args()
    try:
        entry = resolve_sdk(args.version, target=args.target)
    except ValueError as error:
        parser.error(str(error))
    if args.github_output:
        with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8", newline="\n") as output:
            output.write(f"version={entry['version']}\n")
            output.write("matrix=" + json.dumps({"include": [dict(target=key, **value) for key, value in TARGETS.items()]}) + "\n")
    print(json.dumps(entry, indent=2))
