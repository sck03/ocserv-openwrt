"""Bundle the Linux desktop's corresponding sources without build or user data."""
from pathlib import Path
import tempfile
import zipfile
from build_common import ROOT, source_files, write_checksums


def package():
    out = ROOT / 'dist/linux-source'
    out.mkdir(parents=True, exist_ok=True)
    paths = source_files(ROOT, ('desktop/linux',), (
        'scripts/build-linux.sh', 'scripts/package-linux-source.py', 'scripts/build_common.py',
        'resources/app.svg', 'LICENSE', 'THIRD-PARTY-NOTICES.md',
        'docs/DESKTOP.md', 'tests/linux_desktop_tests.cpp', 'tests/linux_auth_fixture.cpp',
        'tests/linux_agent_tests.cpp', 'tests/linux_package_tests.py', '.github/workflows/build-desktop.yml',
    ))
    with tempfile.TemporaryDirectory(prefix='source-', dir=out) as work:
        staged = Path(work) / 'Linkora-linux-source.zip'
        with zipfile.ZipFile(staged, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in paths:
                archive.write(path, 'Linkora/' + path.relative_to(ROOT).as_posix())
        staged.replace(out / staged.name)
    write_checksums(out, [out / 'Linkora-linux-source.zip'])


if __name__ == '__main__':
    package()
