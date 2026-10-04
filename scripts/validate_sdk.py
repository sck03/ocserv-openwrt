"""Reject an SDK whose configured target or package architecture differs from its metadata."""
import json
from pathlib import Path
import sys
from sdk_config import target_config


def validate_sdk(sdk):
    entry = json.loads((sdk / "sdk-info.json").read_text(encoding="utf-8"))
    expected = target_config(entry["target"])
    config = (sdk / ".config").read_text(encoding="utf-8").splitlines()
    target = expected["target"].replace("/", "_")
    if f"CONFIG_TARGET_{target}=y" not in config or f'CONFIG_TARGET_ARCH_PACKAGES="{expected["architecture"]}"' not in config:
        raise RuntimeError("SDK target/package architecture does not match sdk-info.json")
    return expected


if __name__ == "__main__":
    validate_sdk(Path(sys.argv[1]))
