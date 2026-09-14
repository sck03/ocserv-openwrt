"""Exercise release publication with local ZIP fixtures and a fake GitHub CLI."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BASH = os.environ.get("RELEASE_TEST_BASH") or shutil.which("bash")


@unittest.skipUnless(BASH, "Bash is required for release script tests")
class ReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="release-tests-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.assets = self.work / "assets"
        self.assets.mkdir()
        for name in ("client-x64.zip", "client-x86.zip", "source.zip"):
            with zipfile.ZipFile(self.assets / name, "w") as archive:
                archive.writestr("fixture.txt", name)
        self.checksum_file = self.assets / "SHA256SUMS.txt"
        self.checksum_file.write_text("".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
            for path in sorted(self.assets.glob("*.zip"))), encoding="ascii", newline="\n")
        binary = self.work / "bin"
        binary.mkdir()
        gh = binary / "gh"
        gh.write_text('''#!/usr/bin/env bash
set -euo pipefail
printf '%s\\t' "$@" >> gh-calls.log
printf '\\n' >> gh-calls.log
if [[ "$2" == "${FAIL_COMMAND:-}" ]]; then exit 1; fi
if [[ "$2" == create ]]; then
  while [[ $# -gt 0 ]]; do
    if [[ "$1" == --notes-file ]]; then cp "$2" published-notes.md; break; fi
    shift
  done
fi
''', encoding="utf-8", newline="\n")
        gh.chmod(0o755)

    def publish(self, component="client", **overrides):
        environment = dict(os.environ, GH_TOKEN="fixture-token", GITHUB_REPOSITORY="fixture/repository",
                           GITHUB_SHA="a" * 40, GITHUB_RUN_ID="12345", GITHUB_RUN_ATTEMPT="1",
                           GITHUB_RUN_NUMBER="7", GITHUB_SERVER_URL="https://github.com",
                           GITHUB_STEP_SUMMARY="summary.md", FAIL_COMMAND="")
        environment.update(overrides)
        return subprocess.run(
            [BASH, "-c", 'export PATH="$PWD/bin:/usr/bin:/bin:$PATH"; exec bash "$@"', "release-test",
             (ROOT / "scripts/publish-release.sh").as_posix(), component, "assets"],
            cwd=self.work, env=environment, capture_output=True, text=True, encoding="utf-8", timeout=20)

    def calls(self):
        log = self.work / "gh-calls.log"
        return [line.rstrip("\t").split("\t") for line in log.read_text(encoding="utf-8").splitlines()] if log.exists() else []

    def test_uploads_all_assets_before_publishing_draft(self):
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertEqual([call[:2] for call in calls], [["release", action] for action in ("create", "upload", "edit")])
        self.assertIn("--draft", calls[0])
        self.assertIn("--prerelease", calls[0])
        self.assertIn("--latest=false", calls[0])
        self.assertEqual(calls[0][calls[0].index("--target") + 1], "a" * 40)
        for name in ("client-x64.zip", "client-x86.zip", "source.zip", "SHA256SUMS.txt"):
            self.assertIn("assets/" + name, calls[1])
        self.assertIn("--draft=false", calls[2])
        summary = (self.work / "summary.md").read_text(encoding="utf-8")
        self.assertIn("https://github.com/fixture/repository/releases/tag/client-12345-1", summary)
        notes = (self.work / "published-notes.md").read_text(encoding="utf-8")
        self.assertIn("source.zip", notes)
        self.assertIn("/actions/runs/12345/attempts/1", notes)

    def test_server_uses_its_own_tag(self):
        result = self.publish("server")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls()[0][2], "server-12345-1")

    def test_rerun_uses_new_tag_without_overwriting_assets(self):
        result = self.publish(GITHUB_RUN_ATTEMPT="2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls()[0][2], "client-12345-2")
        self.assertNotIn("--clobber", self.calls()[1])

    def test_corrupt_zip_fails_before_creating_release(self):
        (self.assets / "client-x64.zip").write_bytes(b"corrupt")
        result = self.publish()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("do not match", result.stderr)
        self.assertEqual(self.calls(), [])

    def test_unlisted_zip_fails_before_creating_release(self):
        (self.assets / "unlisted.zip").write_bytes(b"unlisted")
        self.assertNotEqual(self.publish().returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_missing_zip_fails_before_creating_release(self):
        (self.assets / "source.zip").unlink()
        self.assertNotEqual(self.publish().returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_missing_checksum_file_fails_before_creating_release(self):
        self.checksum_file.unlink()
        self.assertNotEqual(self.publish().returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_failed_creation_stops_before_upload(self):
        self.assertNotEqual(self.publish(FAIL_COMMAND="create").returncode, 0)
        self.assertEqual([call[1] for call in self.calls()], ["create"])

    def test_failed_upload_keeps_release_unpublished(self):
        self.assertNotEqual(self.publish(FAIL_COMMAND="upload").returncode, 0)
        self.assertEqual([call[1] for call in self.calls()], ["create", "upload"])
        self.assertFalse((self.work / "summary.md").exists())

    def test_failed_publication_does_not_report_success(self):
        self.assertNotEqual(self.publish(FAIL_COMMAND="edit").returncode, 0)
        self.assertFalse((self.work / "summary.md").exists())

    def test_missing_token_stops_before_creating_release(self):
        self.assertNotEqual(self.publish(GH_TOKEN="").returncode, 0)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
