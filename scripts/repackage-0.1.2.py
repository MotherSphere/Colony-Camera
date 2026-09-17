"""Restore distribution notices without rebuilding or changing the 0.1.2 binary."""
import hashlib
import io
import json
import tarfile
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
original = root / 'dist/Colony Camera 0.1.2 alpha.zip'
expected = '3608ef9284a095ae50bb005854928e3faeb6a1468493e3d0d68137c030285405'
if hashlib.sha256(original.read_bytes()).hexdigest() != expected:
    raise SystemExit('Unexpected original archive; refusing to repackage')
revision = 'f3a1e9b0eeaeabaa268d1bd16130d2d40df275d5'
release = 'https://github.com/MotherSphere/Colony-Camera/releases/tag/v0.1.2'
output = root / 'dist/nexus-0.1.2-update'
output.mkdir(exist_ok=True)
with zipfile.ZipFile(original) as source:
    prefix = 'Colony Camera 0.1.2 alpha/'
    files = {n[len(prefix):]: source.read(n) for n in source.namelist()
             if n.startswith(prefix) and not n.endswith('/')}
assert files['SOURCE-REVISION.txt'].decode().strip() == revision
files.pop('LISEZ-MOI.txt')
files.pop('SHA256.json')
files['README.txt'] = f'''Camera Colony 0.1.2 alpha - distribution update

This update restores licenses, notices and corresponding source access.
The DLL and INI are unchanged from the original 0.1.2 build.

Install with your mod manager. The archive root contains SKSE/Plugins.
Requires Skyrim Steam 1.7.104.0, SKSE 2.3.1 and matching Address Library.
Disable SmoothCam and other full third-person camera replacements.
Ctrl+F8: toggle. Ctrl+F9: switch shoulder. Ctrl+F10: reload INI.

Original project code: GPL-3.0-or-later. See LICENSE.txt.
Preserve THIRD-PARTY-NOTICES.txt and Licenses/ when redistributing.
CommonLib's additional permissions are in Licenses/commonlib/EXCEPTIONS.md.

Corresponding source revision: {revision}
Source and downloads: {release}
Sources/ contains the exact original project and native dependency archives.
Extract colony-camera.tar.gz, then extract dependencies.tar.gz inside the
resulting Colony-Camera directory. Follow the included build instructions,
skipping fetch-dependencies.py because these sources have no Git metadata.
Historical source documentation is retained unchanged to preserve provenance.
Current English instructions: https://github.com/MotherSphere/Colony-Camera

This is an alpha. No warranty; see the license. Diagnostic 0.1.3 is a separate
build and is not included here.
'''.encode()
# These historical notices describe the bundled dependency sources and licenses.
required = ['LICENSE.txt', 'THIRD-PARTY-NOTICES.txt',
            'Licenses/commonlib/COPYING.txt', 'Licenses/commonlib/EXCEPTIONS.md',
            'Licenses/commonlib/licenses/LICENSE-MIT.txt',
            'Licenses/spdlog/LICENSE', 'Licenses/directxmath/LICENSE',
            'Licenses/directxtk/LICENSE', 'Licenses/rust-COPYRIGHT-library.html',
            'Sources/colony-camera.tar.gz', 'Sources/dependencies.tar.gz']
assert all(files.get(name) for name in required)
for name in ['Sources/colony-camera.tar.gz', 'Sources/dependencies.tar.gz']:
    with tarfile.open(fileobj=io.BytesIO(files[name]), mode='r:gz') as archive:
        assert archive.getmembers()
manifest = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
files['SHA256.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
for name, entries in [
    ('Camera-Colony-0.1.2-alpha-update.zip', files),
    ('Camera-Colony-0.1.2-sources.zip', {k: v for k, v in files.items()
     if not k.startswith('SKSE/') and k != 'SHA256.json'})]:
    path = output / name
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_DEFLATED) as archive:
        for entry, data in sorted(entries.items()):
            archive.writestr(entry, data)
    with zipfile.ZipFile(path) as check:
        assert check.testzip() is None
        assert all(check.read(k) == v for k, v in entries.items())
    print(path.name, hashlib.sha256(path.read_bytes()).hexdigest())
