"""Build a committed candidate; package runtime, sources and symbols separately.

Python 3.11+. Use the developer terminal used to configure CMake. This does not
install or publish anything. ZIP metadata is deterministic for identical input
bytes; this does not claim toolchain-level binary reproducibility.
"""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import struct
import subprocess
import tarfile
import time
import tomllib
import uuid
import zipfile


ROOT = Path(__file__).resolve().parents[1]
PLAYER_FILES = {
    "SKSE/Plugins/ColonyCamera.dll", "SKSE/Plugins/ColonyCamera.ini",
    "README.txt", "LICENSES.txt",
}


class PackageError(RuntimeError):
    """An input is unsafe, incomplete or not tied to committed sources."""


def command(*args, cwd=ROOT):
    return subprocess.check_output([str(arg) for arg in args], cwd=cwd)


def git(path, *args):
    return command("git", "-C", path, *args, cwd=path)


def clean_revision(path, expected=None):
    revision = git(path, "rev-parse", "HEAD").decode().strip()
    if expected is not None and revision != expected:
        raise PackageError(f"{path}: revision {revision} does not match {expected}")
    if git(path, "status", "--porcelain", "--untracked-files=all").strip():
        raise PackageError(f"{path}: commit or preserve source changes before packaging")
    # This project builds SE/AE, without CommonLib's optional OpenVR submodule.
    # Fail on every other gitlink rather than silently omitting used sources.
    unused = {b"extern/openvr": b"60eb187801956ad277f1cae6680e3a410ee0873b"} if path.name == "commonlib" else {}
    for line in git(path, "ls-files", "--stage").splitlines():
        if line.startswith(b"160000 "):
            info, name = line.split(b"\t", 1)
            if unused.get(name) != info.split()[1]:
                raise PackageError(f"{path}: unknown submodule requires corresponding-source bundling")
    return revision


def read_version(root):
    package = tomllib.loads((root / "Cargo.toml").read_text(encoding="utf-8"))["package"]
    version = package["version"]
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?", version):
        raise PackageError("Unsupported package version syntax")
    if any(value > limit for value, limit in zip(map(int, version.split("-", 1)[0].split(".")), (255, 255, 4095))):
        raise PackageError("Package version exceeds the SKSE version field bounds")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(ColonyCamera\s+VERSION\s+([^\s)]+)", cmake)
    if not match or match[1] != version.split("-", 1)[0]:
        raise PackageError("CMake and Cargo versions disagree")
    packages = tomllib.loads((root / "Cargo.lock").read_text(encoding="utf-8"))["package"]
    if len(packages) != 1 or packages[0].get("name") != package["name"]:
        raise PackageError("New Rust dependencies require corresponding-source and notice bundling")
    if packages[0].get("version") != version:
        raise PackageError("Cargo.lock and Cargo.toml versions disagree")
    return version


def source_repository(root):
    url = git(root, "remote", "get-url", "origin").decode().strip()
    match = re.fullmatch(r"(?:https://github\.com/|git@github\.com:)([\w.-]+/[\w.-]+?)(?:\.git)?", url)
    if not match:
        raise PackageError("origin must identify the public GitHub source repository")
    return "https://github.com/" + match[1]


def safe_path(name):
    path = PurePosixPath(name)
    if not name or "\\" in name or ":" in name or path.is_absolute() or ".." in path.parts:
        raise PackageError(f"Unsafe archive path: {name!r}")
    if path.as_posix() != name.rstrip("/"):
        raise PackageError(f"Noncanonical archive path: {name!r}")
    return path


def archive_source(path, revision, prefix):
    data = git(path, "-c", "core.autocrlf=false", "archive", "--format=tar", f"--prefix={prefix}/", revision)
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        for entry in archive:
            safe_path(entry.name)
            if entry.issym() or entry.islnk():
                target = entry.linkname
                if entry.issym():
                    target = posixpath.normpath(posixpath.join(posixpath.dirname(entry.name), target))
                safe_path(target)
                if not PurePosixPath(target).is_relative_to(prefix):
                    raise PackageError(f"Source link escapes its tree: {entry.name}")
            elif not (entry.isfile() or entry.isdir()):
                raise PackageError(f"Unsupported source archive entry: {entry.name}")
    return data


def is_notice(path):
    lower = path.name.lower()
    return (any(part.lower() in {"licenses", "licences"} for part in path.parts)
            or any(token in lower for token in ("license", "licence", "copying", "copyright", "exceptions", "notice")))


def dependency_sources(root):
    manifest = json.loads((root / "dependencies.json").read_text(encoding="utf-8"))
    notices = {}
    revisions = {}
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as bundle:
        for name, spec in sorted(manifest.items()):
            if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
                raise PackageError(f"Invalid dependency name: {name}")
            if not re.fullmatch(r"[0-9a-f]{40}", spec["revision"]):
                raise PackageError(f"{name}: manifest must pin a full commit")
            path = root / "deps" / name
            revisions[name] = clean_revision(path, spec["revision"])
            archive = archive_source(path, spec["revision"], f"deps/{name}")
            found_license = False
            with tarfile.open(fileobj=io.BytesIO(archive)) as source:
                for entry in source:
                    data = source.extractfile(entry).read() if entry.isfile() else None
                    bundle.addfile(entry, io.BytesIO(data) if data is not None else None)
                    relative = PurePosixPath(entry.name).relative_to(f"deps/{name}")
                    if data is not None and is_notice(relative):
                        if not data.strip():
                            raise PackageError(f"Empty dependency notice: {entry.name}")
                        notices[entry.name] = data
                        found_license = True
            if not found_license:
                raise PackageError(f"{name}: no license notices found at pinned revision")
    required = {"deps/commonlib/COPYING.txt", "deps/commonlib/EXCEPTIONS.md",
                "deps/commonlib/licenses/LICENSE-MIT.txt"}
    if not required.issubset(notices):
        raise PackageError("Pinned CommonLib license or additional permissions are missing")
    return buffer.getvalue(), notices, revisions


def consolidate_notices(root, dependency_notices, rustc):
    sysroot = Path(command(rustc, "--print", "sysroot").decode().strip())
    rust_notice = sysroot / "share/doc/rust/COPYRIGHT-library.html"
    if not rust_notice.is_file():
        raise PackageError("Install rust-docs for the Rust toolchain used by this build")
    notices = {"Project LICENSE": (root / "LICENSE").read_bytes(),
               "Project THIRD-PARTY-NOTICES.md": (root / "THIRD-PARTY-NOTICES.md").read_bytes(),
               "Improved Camera MPL-2.0": (root / "licenses/ImprovedCamera/MPL-2.0.txt").read_bytes(),
               **dependency_notices,
               "Rust COPYRIGHT-library.html (HTML preserved verbatim)": rust_notice.read_bytes()}
    combined = bytearray(b"Camera Colony - complete distribution license notices\n")
    for name, data in sorted(notices.items()):
        if not data.strip():
            raise PackageError(f"Required notice is empty: {name}")
        data.decode("utf-8")
        combined.extend(f"\n{'=' * 72}\n{name}\n{'=' * 72}\n".encode())
        combined.extend(data)
        combined.extend(b"\n")
    return bytes(combined)


def build_cache(root, build_dir, config):
    cache = {}
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        match = re.match(r"([^#/:][^:]*):[^=]+=(.*)", line)
        if match:
            cache[match[1]] = match[2]
    home = cache.get("CMAKE_HOME_DIRECTORY")
    if not home or Path(home).resolve() != root.resolve():
        raise PackageError("Build directory was configured from a different source checkout")
    configurations = cache.get("CMAKE_CONFIGURATION_TYPES", "").split(";")
    if configurations != [""]:
        if config not in configurations:
            raise PackageError(f"Configuration {config} is absent from this CMake build")
        dll = build_dir / config / "ColonyCamera.dll"
    else:
        if config != cache.get("CMAKE_BUILD_TYPE"):
            raise PackageError("--config must match CMAKE_BUILD_TYPE for a single-configuration build")
        dll = build_dir / "ColonyCamera.dll"
    return cache, dll


def validate_dll(data, expected_version=None):
    """Check x64 PE structure and SKSE exports without executing the DLL."""
    try:
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        machine, count = struct.unpack_from("<HH", data, pe + 4)
        optional_size, flags = struct.unpack_from("<HH", data, pe + 20)
        optional = pe + 24
        if (data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0"
                or machine != 0x8664 or not flags & 0x2000
                or struct.unpack_from("<H", data, optional)[0] != 0x20B
                or optional_size < 120 or not 1 <= count <= 96):
            raise PackageError("Expected a Windows x64 PE32+ DLL")
        sections = [struct.unpack_from("<4I", data, optional + optional_size + 40 * i + 8)
                    for i in range(count)]

        def mapped(rva, size):
            for _, address, length, raw in sections:
                offset = raw + rva - address
                if address <= rva and rva + size <= address + length and offset + size <= len(data):
                    return offset
            raise PackageError("DLL export data is outside a mapped section")

        export_rva, export_size = struct.unpack_from("<II", data, optional + 112)
        if export_size < 40:
            raise PackageError("DLL export directory is missing")
        exports = mapped(export_rva, 40)
        names_count = struct.unpack_from("<I", data, exports + 24)[0]
        names_rva = struct.unpack_from("<I", data, exports + 32)[0]
        if not 1 <= names_count <= 65536:
            raise PackageError("DLL export count is invalid")
        table = mapped(names_rva, names_count * 4)
        names = set()
        version_index = None
        for i in range(names_count):
            rva = struct.unpack_from("<I", data, table + 4 * i)[0]
            start = mapped(rva, 1)
            end = data.find(b"\0", start, start + 256)
            if end < 0:
                raise PackageError("DLL export name is unterminated")
            mapped(rva, end - start + 1)
            names.add(data[start:end])
            if data[start:end] == b"SKSEPlugin_Version":
                version_index = i
        if not {b"SKSEPlugin_Load", b"SKSEPlugin_Version"}.issubset(names):
            raise PackageError("DLL lacks the required SKSE exports")
        if expected_version is not None:
            functions_count = struct.unpack_from("<I", data, exports + 20)[0]
            functions_rva = struct.unpack_from("<I", data, exports + 28)[0]
            ordinals_rva = struct.unpack_from("<I", data, exports + 36)[0]
            ordinal = struct.unpack_from("<H", data, mapped(ordinals_rva + 2 * version_index, 2))[0]
            if ordinal >= functions_count:
                raise PackageError("DLL version export ordinal is invalid")
            version_rva = struct.unpack_from("<I", data, mapped(functions_rva + 4 * ordinal, 4))[0]
            if export_rva <= version_rva < export_rva + export_size:
                raise PackageError("DLL version export is forwarded instead of owned by this plugin")
            version_data = mapped(version_rva, 264)
            major, minor, patch = map(int, expected_version.split("-", 1)[0].split("."))
            if struct.unpack_from("<II", data, version_data) != (1, major << 24 | minor << 16 | patch << 4):
                raise PackageError("DLL plugin version does not match the source metadata")
            if data[version_data + 8:version_data + 264].split(b"\0", 1)[0] != b"ColonyCamera":
                raise PackageError("DLL plugin name does not match ColonyCamera")
    except (struct.error, IndexError) as error:
        raise PackageError("Truncated or invalid DLL") from error


def write_zip(path, entries, epoch):
    for name in entries:
        safe_path(name)
    stamp = time.gmtime(max(315532800, min(epoch, 4354819198)))[:6]
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=stamp)
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compresslevel=9)
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(entries):
            raise PackageError(f"Archive verification failed: {path}")
        if any(archive.read(name) != data for name, data in entries.items()):
            raise PackageError(f"Archive contents changed: {path}")


def rebuild(root, build_dir, cache, args, environment):
    """Use a fresh Cargo target without deleting or trusting an existing cache.

    Ninja can create a custom command's output parents before Cargo starts. Such
    directories may lack Cargo's CACHEDIR.TAG and cannot safely be cargo-cleaned.
    A new target avoids both that problem and incremental source ambiguity.
    """
    target = build_dir / f"package-cargo-{uuid.uuid4().hex}"
    if target.exists() or target.is_symlink() or target.resolve().parent != build_dir.resolve():
        raise PackageError("Could not select a fresh Cargo target inside the verified build directory")
    previous = cache.get("CC_RUST_TARGET_DIR", str(build_dir / "cargo"))
    configure = [args.cmake, "-S", str(root), "-B", str(build_dir),
                 f"-DCC_RUST_TARGET_DIR:PATH={target.as_posix()}"]
    restore = [args.cmake, "-S", str(root), "-B", str(build_dir),
               f"-DCC_RUST_TARGET_DIR:PATH={previous}"]
    build = [args.cmake, "--build", str(build_dir), "--config", args.config,
             "--clean-first", "--parallel", str(args.jobs)]
    try:
        subprocess.run(configure, cwd=root, env=environment, check=True)
        subprocess.run(build, cwd=root, env=environment, check=True)
    finally:
        # Keep the developer's selected target configuration on success or error.
        # Both the original cache and the fresh build outputs remain available.
        subprocess.run(restore, cwd=root, env=environment, check=True)
    return {"configure": configure, "build": build, "restore": restore}


def package(args, root=ROOT):
    root = root.resolve()
    revision = clean_revision(root)
    version = read_version(root)
    repository = source_repository(root)
    epoch = int(git(root, "show", "-s", "--format=%ct", revision))
    build_dir = args.build_dir.resolve()
    cache, dll_path = build_cache(root, build_dir, args.config)
    name = f"Camera-Colony-{version}-{revision[:12]}"
    output = args.output.resolve() / name
    if output.exists():
        raise PackageError(f"Output already exists; preserve it before retrying: {output}")
    project_source = archive_source(root, revision, "Colony-Camera")
    dependency_source, dependency_notices, dependency_revisions = dependency_sources(root)
    licenses = consolidate_notices(root, dependency_notices, args.rustc)
    ini = root / "assets/SKSE/Plugins/ColonyCamera.ini"
    ini_before = ini.read_bytes()
    environment = dict(os.environ, SOURCE_DATE_EPOCH=str(epoch), RUSTC=args.rustc)
    cargo = cache.get("CARGO")
    if not cargo or cargo.endswith("-NOTFOUND"):
        raise PackageError("CMake cache does not identify the Cargo executable")
    # Rebuild from clean C++ outputs and an unused Rust target. Existing Cargo
    # directories are never deleted or supplied with fabricated cache markers.
    build_commands = rebuild(root, build_dir, cache, args, environment)
    binary = dll_path.read_bytes()
    validate_dll(binary, version)
    clean_revision(root, revision)
    for dep, commit in dependency_revisions.items():
        clean_revision(root / "deps" / dep, commit)
    if ini.read_bytes() != ini_before:
        raise PackageError("Default INI changed during packaging")
    source_link = f"{repository}/tree/{revision}"
    readme = f"""Camera Colony {version} - local candidate

Install SKSE/Plugins with your mod manager as a separate candidate mod.
Close Skyrim first and back up the installed DLL and your existing INI.
The bundled INI contains defaults: merge settings instead of overwriting yours.
Disable the candidate and restore the backup to roll back. Do not modify saves.

Requirements, controls, implemented features and validation limits:
{repository}/blob/{revision}/README.md
This candidate is not evidence of in-game validation or a finished feature set.

Exact corresponding source commit: {revision}
Source: {source_link}
Complete project and native dependency sources: {name}-sources.zip
These files are local candidates. Source links become available only after the
commit and matching source download are published; this tool publishes nothing.

Original code: GPL-3.0-or-later. Preserve the complete LICENSES.txt, including
dependency credits, CommonLib additional permissions and Rust runtime notices.
Publish the matching source download alongside any authorized binary release.
""".encode()
    player = {"SKSE/Plugins/ColonyCamera.dll": binary,
              "SKSE/Plugins/ColonyCamera.ini": ini_before,
              "README.txt": readme, "LICENSES.txt": licenses}
    if set(player) != PLAYER_FILES:
        raise PackageError("Unexpected player archive contents")
    metadata = {"version": version, "revision": revision, "repository": repository,
                "dependencies": dependency_revisions, "configuration": args.config,
                "rustc": command(args.rustc, "-Vv").decode().strip(),
                "cargo": command(cargo, "--version").decode().strip(),
                "cmake": command(args.cmake, "--version").decode().splitlines()[0],
                "cpp_compiler": cache.get("CMAKE_CXX_COMPILER"),
                "cmake_generator": cache.get("CMAKE_GENERATOR"),
                "build_commands": build_commands,
                "dll_sha256": hashlib.sha256(binary).hexdigest(),
                "default_ini_sha256": hashlib.sha256(ini_before).hexdigest(),
                "validation": "Clean build; x64 PE and SKSE exports; archive integrity. In-game validation pending."}
    source_readme = f"""Camera Colony {version} corresponding sources
Commit: {revision}
Extract colony-camera.tar.gz, then extract dependencies.tar.gz inside the
resulting Colony-Camera directory. Follow README.md; skip fetch-dependencies.py
because the supplied dependency sources have no Git metadata.
build.json records the source revisions and toolchain used for this candidate.
CommonLib's optional OpenVR submodule is unused by this SE/AE build and omitted.
No SmoothCam or Improved Camera binary, assets or presets are supplied.
""".encode()
    sources = {"colony-camera.tar.gz": gzip.compress(project_source, mtime=epoch),
               "dependencies.tar.gz": gzip.compress(dependency_source, mtime=epoch),
               "README.txt": source_readme, "LICENSES.txt": licenses,
               "build.json": (json.dumps(metadata, indent=2) + "\n").encode()}
    output.mkdir(parents=True, exist_ok=False)
    write_zip(output / f"{name}.zip", player, epoch)
    write_zip(output / f"{name}-sources.zip", sources, epoch)
    pdb = dll_path.with_suffix(".pdb")
    if pdb.is_file():
        write_zip(output / f"{name}-symbols.zip", {pdb.name: pdb.read_bytes()}, epoch)
    manifest = {"build": metadata,
                "archives": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in sorted(output.glob("*.zip"))},
                "player_files": {path: hashlib.sha256(data).hexdigest()
                                 for path, data in sorted(player.items())}}
    (output / "SHA256.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if ini.read_bytes() != ini_before:
        raise PackageError("Default INI changed during archive generation")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--rustc", default="rustc")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        print(package(args))
    except (PackageError, OSError, subprocess.CalledProcessError, ValueError) as error:
        parser.exit(1, f"Packaging refused: {error}\n")


if __name__ == "__main__":
    main()
