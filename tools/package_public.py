"""Create a source-only ZIP with an explicit allowlist; no Git history or local files."""
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT.parent / 'codex-gauge-public.zip'
TOP = {'LICENSE', '.env.example', '.gitignore', 'README.md', 'HARDWARE.md', 'DOCKER.md',
       'DATENQUELLE.md', 'JSON.md', 'REMOTE.md', 'TESTS.md', 'WLAN-EINRICHTUNG.md',
       'WEITERGABE.md', 'ASSETS.md', 'compose.yaml', 'compose.remote.yaml',
       'display-dual-preview.svg', 'dashboard-preview.png'}
FILES = TOP | {'firmware/README.md', 'firmware/platformio.ini', 'service/package.json',
               'service/Dockerfile', 'service/.dockerignore', '.github/workflows/check.yml',
               'tools/package_public.py'}
for directory, suffixes in [('firmware/src', {'.cpp'}), ('firmware/include', {'.h'}),
                             ('firmware/test', {'.cpp', '.mjs'}), ('service/test', {'.mjs'}),
                             ('service/web', {'.html', '.js', '.css'}), ('deploy', {'.sh', '.conf'})]:
    FILES.update(str(p.relative_to(ROOT)) for p in (ROOT / directory).rglob('*')
                 if p.is_file() and p.suffix in suffixes)
FILES.update(str(p.relative_to(ROOT)) for p in (ROOT / 'service').glob('*.mjs'))
FILES.discard('firmware/include/rocky_typing.h')
FILES.discard('firmware/include/config.h')
# Fail closed for known private deployment identifiers; report names, not contents.
private_markers = [b'/Users/', b'BEGIN PRIVATE KEY', b'BEGIN OPENSSH PRIVATE KEY']
# This script contains the scanner's markers by design, never include its source in matches.
for name in sorted(FILES - {'tools/package_public.py'}):
    data = (ROOT / name).read_bytes()
    if any(marker in data for marker in private_markers):
        raise SystemExit('Private deployment marker found in: ' + name)
with zipfile.ZipFile(DEST, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(FILES):
        archive.write(ROOT / name, 'codex-gauge/' + name)
print(f'Created {DEST.name}: {len(FILES)} files, no history, credentials or third-party sprite')
