"""Package a verified local alpha with its exact source and dependency notices."""
import gzip
import hashlib
import io
import json
import shutil
import subprocess
import tarfile
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
def git(*args, cwd=root):
    return subprocess.check_output(["git", "-C", str(cwd), *args])
if git("status", "--porcelain").strip():
    raise SystemExit("Commit all source changes before packaging")
subprocess.run(["python3", str(root / "scripts/fetch-dependencies.py")], check=True)
stage = root / "dist" / "Colony Camera 0.1.3 diagnostic"
if stage.exists():
    raise SystemExit(f"Output already exists: {stage}; preserve or rename it before repackaging")
plugins = stage / "SKSE/Plugins"
plugins.mkdir(parents=True)
for source in [root / "build/ColonyCamera.dll", root / "assets/SKSE/Plugins/ColonyCamera.ini"]:
    shutil.copy2(source, plugins / source.name)
for source, destination in [("README.md", "README.txt"), ("LICENSE", "LICENSE.txt"), ("THIRD-PARTY-NOTICES.md", "THIRD-PARTY-NOTICES.txt")]:
    shutil.copy2(root / source, stage / destination)
sources = stage / "Sources"
sources.mkdir()
revision = git("rev-parse", "HEAD").decode().strip()
(stage / "SOURCE-REVISION.txt").write_text(revision + "\n")
with gzip.open(sources / "colony-camera.tar.gz", "wb") as stream:
    stream.write(git("archive", "--format=tar", "--prefix=Colony-Camera/", "HEAD"))
notices = stage / "Licenses"
notices.mkdir()
with tarfile.open(sources / "dependencies.tar.gz", "w:gz") as bundle:
    for name, spec in json.loads((root / "dependencies.json").read_text()).items():
        path = root / "deps" / name
        archive = git("archive", "--format=tar", f"--prefix=deps/{name}/", spec["revision"], cwd=path)
        with tarfile.open(fileobj=io.BytesIO(archive)) as src:
            for entry in src:
                bundle.addfile(entry, src.extractfile(entry) if entry.isfile() else None)
        dest = notices / name
        dest.mkdir()
        for file in path.iterdir():
            if file.is_file() and file.name.lower().startswith(("license", "copying", "exceptions")):
                shutil.copy2(file, dest / file.name)
        if (path / "licenses").is_dir():
            shutil.copytree(path / "licenses", dest / "licenses")
# Runtime licenses supplied by the installed Rust toolchain.
sysroot = Path(subprocess.check_output(["rustc", "--print", "sysroot"], text=True).strip())
rust_notice = sysroot / "share/doc/rust/COPYRIGHT-library.html"
if not rust_notice.is_file():
    raise SystemExit("Install the matching rust-docs component for Rust library notices")
shutil.copy2(rust_notice, notices / "rust-COPYRIGHT-library.html")
files = sorted(p for p in stage.rglob("*") if p.is_file())
manifest = {str(p.relative_to(stage)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(stage / "SHA256.json").write_text(json.dumps(manifest, indent=2) + "\n")
archive = root / "dist" / (stage.name + ".zip")
with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as out:
    for file in sorted(stage.rglob("*")):
        if file.is_file():
            out.write(file, file.relative_to(stage.parent))
with zipfile.ZipFile(archive) as check:
    assert check.testzip() is None
    for relative, digest in manifest.items():
        assert hashlib.sha256(check.read(stage.name + "/" + relative)).hexdigest() == digest
print(archive)
print("sha256", hashlib.sha256(archive.read_bytes()).hexdigest())
