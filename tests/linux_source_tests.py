"""Check the actual Linux corresponding-source archive against its CMake inputs."""
import importlib.util
from pathlib import Path, PurePosixPath
import posixpath
import re
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("linux_packaging", ROOT / "scripts/package-linux-source.py")
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)


class LinuxSourceTests(unittest.TestCase):
    def test_cmake_sources_are_present_in_the_shipped_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            packaging.package(directory)
            with zipfile.ZipFile(Path(directory) / "Lansyra-linux-source.zip") as archive:
                names = set(archive.namelist())
                cmake = archive.read("Lansyra/desktop/linux/CMakeLists.txt").decode()
                inputs = re.findall(r"(?:[\w.-]+/)*[\w.-]+\.(?:cpp|h)\b", cmake)
                self.assertTrue(inputs)
                for name in inputs:
                    path = posixpath.normpath(str(PurePosixPath("Lansyra/desktop/linux") / name))
                    with self.subTest(source=name):
                        self.assertIn(path, names)


if __name__ == "__main__":
    unittest.main()
