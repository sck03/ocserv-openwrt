#!/usr/bin/env python3
"""Install validated profiles on an ephemeral macOS runner and create export options."""
import argparse
import base64
import datetime
import json
import os
from pathlib import Path
import plistlib
import re
import shlex
import subprocess


def validate_profile(values, bundle, team):
    entitlements = values["Entitlements"]
    app_id = entitlements.get("application-identifier", "")
    if app_id not in [prefix + "." + bundle for prefix in values.get("ApplicationIdentifierPrefix", [])]:
        raise ValueError("Bundle identifier does not match provisioning profile")
    if team not in values["TeamIdentifier"]:
        raise ValueError("Wrong Apple team")
    if "packet-tunnel-provider" not in entitlements.get("com.apple.developer.networking.networkextension", []):
        raise ValueError("Profile lacks Network Extension entitlement")
    if values["ExpirationDate"] <= datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None):
        raise ValueError("Expired profile")
    if entitlements.get("get-task-allow") or not values.get("ProvisionedDevices"):
        raise ValueError("Supply an Ad Hoc distribution profile with registered device UDIDs")
    uuid = values["UUID"]
    if not re.fullmatch(r"[A-Fa-f0-9-]{36}", uuid):
        raise ValueError("Invalid profile UUID")
    return uuid


def prepare(work):
    bundle = os.environ["VPN_BUNDLE_ID"]
    team = os.environ["IOS_TEAM_ID"]
    profiles = {}
    installed = []
    cert = work / "certificate.p12"
    cert.write_bytes(base64.b64decode(os.environ["IOS_CERTIFICATE_BASE64"], validate=True))
    cert.chmod(0o600)
    destination = Path.home() / "Library/MobileDevice/Provisioning Profiles"
    destination.mkdir(parents=True, exist_ok=True)
    # Record each created path immediately so EXIT cleanup also handles partial failure.
    journal = work / "installed.json"
    journal.write_text("[]")
    for target, suffix in (("app", ""), ("tunnel", ".tunnel")):
        profile = work / f"{target}.mobileprovision"
        profile.write_bytes(base64.b64decode(os.environ[f"IOS_{target.upper()}_PROFILE_BASE64"], validate=True))
        values = plistlib.loads(subprocess.check_output(["security", "cms", "-D", "-i", str(profile)]))
        uuid = validate_profile(values, bundle + suffix, team)
        installed_path = destination / (uuid + ".mobileprovision")
        with installed_path.open("xb") as output:
            output.write(profile.read_bytes())
        installed.append(str(installed_path))
        journal.write_text(json.dumps(installed))
        profiles[target] = uuid
    (work / "profiles.json").write_text(json.dumps(profiles))
    with (work / "ExportOptions.plist").open("wb") as output:
        plistlib.dump({"method": "release-testing", "teamID": team, "signingStyle": "manual",
                      "signingCertificate": "Apple Distribution", "stripSwiftSymbols": True,
                      "provisioningProfiles": {bundle: profiles["app"], bundle + ".tunnel": profiles["tunnel"]}}, output)


def clean(work):
    search = work / "keychains.json"
    if search.exists():
        subprocess.run(["security", "list-keychains", "-d", "user", "-s", *json.loads(search.read_text())], check=True)
    journal = work / "installed.json"
    if journal.exists():
        for name in json.loads(journal.read_text()):
            Path(name).unlink(missing_ok=True)


def keychain(work):
    previous = shlex.split(subprocess.check_output(["security", "list-keychains", "-d", "user"], text=True))
    (work / "keychains.json").write_text(json.dumps(previous))
    subprocess.run(["security", "list-keychains", "-d", "user", "-s", str(work / "build.keychain-db"), *previous], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["prepare", "clean", "keychain"])
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    {"prepare": prepare, "clean": clean, "keychain": keychain}[args.action](args.directory)
