"""One release identity and UTC build timestamp shared by all jobs and packages."""
import argparse
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import re
import sys

BEIJING = timezone(timedelta(hours=8))


def build_metadata(environment=None):
    env = os.environ if environment is None else environment
    release = env.get("RELEASE_VERSION", "").strip()
    if release and (len(release) > 64 or not re.fullmatch(
            r"v?[0-9]+(?:\.[0-9]+){1,3}(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", release)):
        raise ValueError("Release version must be numeric, e.g. 0.5.1, v0.5.1 or 0.5.1-rc.1 (max 64 characters)")
    timestamp = env.get("BUILD_TIMESTAMP", "")
    if timestamp:
        if not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", timestamp):
            raise ValueError("BUILD_TIMESTAMP must use UTC YYYY-MM-DDTHH:MM:SSZ")
        instant = datetime.strptime(timestamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    else:
        instant = datetime.now(timezone.utc).replace(microsecond=0)
    local = instant.astimezone(BEIJING)
    return {"release_version": release, "release_label": release or local.strftime("%Y%m%d-%H%M%S"),
            "build_timestamp": instant.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "build_time": local.strftime("%Y-%m-%d %H:%M:%S +08:00")}


def publication(component, metadata, environment=None):
    env = os.environ if environment is None else environment
    name = {"client": "布利杰VPN Windows 客户端", "server": "ocserv OpenWrt 服务端与中文管理页"}[component]
    for key in ("GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT"):
        if not re.fullmatch(r"[1-9][0-9]*", env.get(key, "")):
            raise ValueError(f"{key} must be a positive integer")
    # Preserve distinct immutable releases even when a version or job is rerun.
    tag = f"{component}-{metadata['release_label']}-{env['GITHUB_RUN_ID']}-{env['GITHUB_RUN_ATTEMPT']}"
    version = f" {metadata['release_version']}" if metadata["release_version"] else ""
    title = f"{name}{version}（构建 {metadata['build_time']}）"
    return tag, title, metadata["build_time"]


def main():
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--github-output", action="store_true")
    parser.add_argument("--publication", choices=("client", "server"))
    args = parser.parse_args()
    try:
        metadata = build_metadata()
        if args.github_output:
            with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8", newline="\n") as output:
                for key, value in metadata.items():
                    output.write(f"{key}={value}\n")
        if args.publication:
            print("\n".join(publication(args.publication, metadata)))
        else:
            print(json.dumps(metadata, ensure_ascii=False))
    except (KeyError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
