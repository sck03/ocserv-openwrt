"""Bundle the Linux desktop's corresponding sources without build or user data."""
from pathlib import Path
import tempfile
import zipfile
from build_common import ROOT, sha256, source_files


def package():
    out = ROOT / 'dist/linux'
    out.mkdir(parents=True, exist_ok=True)
    paths = source_files(ROOT, ('desktop/linux',), (
        'scripts/build-linux.sh', 'scripts/package-linux-source.py', 'scripts/build_common.py',
        'scripts/release_metadata.py', 'resources/app.svg', 'LICENSE', 'THIRD-PARTY-NOTICES.md',
        'docs/DESKTOP.md', 'tests/linux_desktop_tests.cpp', 'tests/linux_auth_fixture.cpp',
        'tests/linux_agent_tests.cpp', '.github/workflows/build-desktop.yml',
    ))
    with tempfile.TemporaryDirectory(prefix='source-', dir=out) as work:
        staged = Path(work) / 'LinkoraVPN-linux-source.zip'
        with zipfile.ZipFile(staged, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in paths:
                archive.write(path, 'LinkoraVPN/' + path.relative_to(ROOT).as_posix())
        staged.replace(out / staged.name)
    (out / 'SHA256SUMS.txt').write_text(''.join(
        f'{sha256(path)}  {path.name}\n' for path in sorted(out.iterdir())
        if path.is_file() and path.name != 'SHA256SUMS.txt'), encoding='utf-8')


if __name__ == '__main__':
    package()
