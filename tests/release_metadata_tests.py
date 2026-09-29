"""Release identity must survive parallel jobs, reruns and UTC date boundaries."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from release_metadata import build_metadata, publication


class MetadataTests(unittest.TestCase):
    def test_blank_version_uses_beijing_date_including_midnight_rollover(self):
        info = build_metadata({"BUILD_TIMESTAMP": "2026-09-28T18:02:03Z"})
        self.assertEqual(info["release_label"], "20260929-020203")
        self.assertEqual(info["build_time"], "2026-09-29 02:02:03 +08:00")

    def test_explicit_version_keeps_build_timestamp(self):
        info = build_metadata({"RELEASE_VERSION": " v1.5.0-rc.1 ", "BUILD_TIMESTAMP": "2026-09-29T01:02:03Z"})
        self.assertEqual(info["release_label"], "v1.5.0-rc.1")
        self.assertEqual(info["build_timestamp"], "2026-09-29T01:02:03Z")

    def test_invalid_version_and_timestamp_are_rejected(self):
        for version in ("../1.0", "1.0\nattack", "1.0;id", "--1.0", "1.0..0", "1.0/2", "a" * 65):
            with self.subTest(version=version), self.assertRaises(ValueError):
                build_metadata({"RELEASE_VERSION": version})
        for timestamp in ("9.1", "2026-02-30T00:00:00Z", "2026-09-29T01:02:03+08:00"):
            with self.subTest(timestamp=timestamp), self.assertRaises(ValueError):
                build_metadata({"BUILD_TIMESTAMP": timestamp})

    def test_different_runs_and_attempts_cannot_overwrite_same_version(self):
        info = build_metadata({"RELEASE_VERSION": "1.2.3"})
        tags = {publication(component, info, {"GITHUB_RUN_ID": run, "GITHUB_RUN_ATTEMPT": attempt})[0]
                for component in ("client", "server") for run in ("123", "124") for attempt in ("1", "2")}
        self.assertEqual(len(tags), 8)

    def test_generated_time_is_reused_by_downstream_jobs(self):
        initial = build_metadata({})
        downstream = build_metadata({"BUILD_TIMESTAMP": initial["build_timestamp"]})
        self.assertEqual(initial, downstream)

    def test_cli_outputs_propagate_to_another_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "output"
            env = dict(os.environ, RELEASE_VERSION="1.2.3", BUILD_TIMESTAMP="2026-09-29T01:02:03Z",
                       GITHUB_OUTPUT=str(output))
            result = subprocess.run([sys.executable, ROOT / "scripts/release_metadata.py", "--github-output"],
                                    env=env, capture_output=True, text=True, encoding="utf-8", check=True)
            propagated = dict(line.split("=", 1) for line in output.read_text().splitlines())
            self.assertEqual(json.loads(result.stdout), build_metadata({
                "RELEASE_VERSION": propagated["release_version"], "BUILD_TIMESTAMP": propagated["build_timestamp"]}))


if __name__ == "__main__":
    unittest.main()
