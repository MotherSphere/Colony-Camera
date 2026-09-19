"""Packaging regressions; synthetic PE bytes are test data, never a plugin build."""

import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import tomllib
import unittest
from unittest.mock import patch
import zipfile


SPEC = importlib.util.spec_from_file_location("camera_package", Path(__file__).resolve().parents[1] / "scripts/package.py")
packaging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packaging)


def pe_fixture():
    """A non-executable PE header fixture with an export table."""
    data = bytearray(2048)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<HH", data, 0x94, 240, 0x2000)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<II", data, 0x98 + 112, 0x1000, 256)
    struct.pack_into("<4I", data, 0x98 + 240 + 8, 1024, 0x1000, 1024, 512)
    struct.pack_into("<I", data, 512 + 24, 2)
    struct.pack_into("<I", data, 512 + 20, 2)
    struct.pack_into("<I", data, 512 + 28, 0x10A0)
    struct.pack_into("<I", data, 512 + 32, 0x1040)
    struct.pack_into("<I", data, 512 + 36, 0x10B0)
    struct.pack_into("<II", data, 576, 0x1060, 0x1080)
    data[608:624] = b"SKSEPlugin_Load\0"
    data[640:659] = b"SKSEPlugin_Version\0"
    struct.pack_into("<II", data, 672, 0x1100, 0x1120)
    struct.pack_into("<HH", data, 688, 0, 1)
    struct.pack_into("<II", data, 800, 1, 1 << 24 | 2 << 16 | 3 << 4)
    data[808:821] = b"ColonyCamera\0"
    return bytes(data)


def commit(path):
    subprocess.run(["git", "-C", str(path), "add", "."], check=True, capture_output=True)
    subprocess.run(["git", "-C", str(path), "-c", "user.name=Packaging test",
                    "-c", "user.email=packaging-test@example.invalid", "commit", "-qm", "Fixture"],
                   check=True, capture_output=True)
    return packaging.git(path, "rev-parse", "HEAD").decode().strip()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "project"
        self.root.mkdir()
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "remote", "add", "origin",
                        "https://github.com/Example/Camera.git"], check=True)
        write(self.root / ".gitignore", "/build/\n/deps/\n/dist/\n")
        write(self.root / "Cargo.toml", '[package]\nname = "camera"\nversion = "1.2.3"\n')
        write(self.root / "Cargo.lock", 'version = 4\n[[package]]\nname = "camera"\nversion = "1.2.3"\n')
        write(self.root / "src/lib.rs", "pub fn camera() {}\n")
        write(self.root / "CMakeLists.txt", "project(ColonyCamera VERSION 1.2.3 LANGUAGES CXX)\n")
        write(self.root / "third_party/skse-menu-framework/LICENSE", "Complete Menu Framework license fixture\n")
        write(self.root / "LICENSE", "Project complete license fixture\n")
        write(self.root / "THIRD-PARTY-NOTICES.md", "Project attribution fixture\n")
        write(self.root / "licenses/ImprovedCamera/MPL-2.0.txt", "Complete MPL notice fixture\n")
        write(self.root / "README.md", "Fixture build instructions\n")
        write(self.root / "assets/SKSE/Plugins/ColonyCamera.ini", "[General]\nenabled = true\n")
        dep = self.root / "deps/commonlib"
        dep.mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(dep)], check=True)
        for file in ("COPYING.txt", "EXCEPTIONS.md", "licenses/LICENSE-MIT.txt", "nested/fmt.license.rst"):
            write(dep / file, file + " preserved notice fixture\n")
        write(dep / "include/source.h", "// Exact dependency source fixture\n")
        self.dep_revision = commit(dep)
        write(self.root / "dependencies.json", json.dumps({"commonlib": {
            "url": "https://github.com/Example/CommonLib.git", "revision": self.dep_revision}}))
        self.revision = commit(self.root)
        self.build = self.root / "build"
        self.configure("multi")
        self.sysroot = self.base / "rust"
        write(self.sysroot / "share/doc/rust/COPYRIGHT-library.html", "<html>Complete Rust notice fixture</html>\n")
        self.args = argparse.Namespace(build_dir=self.build, config="Release", output=self.root / "dist",
                                       rustc="fixture-rustc", cmake="fixture-cmake", jobs=2)
        self.crates = {}

    def add_crate(self):
        """Registry responses are fixtures; archive and checksum validation are real."""
        self.crates = {"serde-1.0.229": {
            "Cargo.toml": b'[package]\nname = "serde"\nversion = "1.0.229"\n',
            "src/lib.rs": b"// exact locked crate source fixture\n",
            "LICENSE-MIT": b"Complete upstream MIT license fixture\n",
            "LICENSE-APACHE": b"Complete upstream Apache license fixture\n",
        }}
        self.checksum = "a" * 64
        with (self.root / "Cargo.toml").open("a") as file:
            file.write('\n[dependencies]\nserde = "1.0.229"\n')
        with (self.root / "Cargo.lock").open("a") as file:
            file.write('dependencies = ["serde"]\n\n[[package]]\nname = "serde"\n'
                       'version = "1.0.229"\nsource = "registry+https://github.com/rust-lang/crates.io-index"\n'
                       f'checksum = "{self.checksum}"\n')
        self.revision = commit(self.root)

    def configure(self, generator):
        text = (f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root.as_posix()}\nCARGO:FILEPATH=fixture-cargo\n"
                f"CC_RUST_TARGET_DIR:PATH={(self.build / 'cargo').as_posix()}\n")
        text += ("CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;RelWithDebInfo\n" if generator == "multi"
                 else "CMAKE_BUILD_TYPE:STRING=Release\n")
        write(self.build / "CMakeCache.txt", text)

    def compiler_tools(self):
        original = packaging.command
        original_run = subprocess.run

        def tool(*args, **kwargs):
            if args[:2] == ("fixture-cargo", "metadata"):
                self.assertIn("--frozen", args)
                packages = tomllib.loads((self.root / "Cargo.lock").read_text())["package"]
                return json.dumps({"packages": [dict(p, manifest_path=str(self.root / "Cargo.toml")
                    if "source" not in p else "/cache/Cargo.toml") for p in packages]}).encode()
            if args[:2] == ("fixture-cargo", "vendor"):
                self.assertIn("--frozen", args)
                self.assertIn("--versioned-dirs", args)
                destination = Path(args[-1])
                for name, files in self.crates.items():
                    for relative, data in files.items():
                        path = destination / name / relative
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(data)
                    write(destination / name / ".cargo-checksum.json", json.dumps({
                        "package": self.checksum,
                        "files": {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}}))
                return ('[source.crates-io]\nreplace-with = "vendored-sources"\n'
                        '[source.vendored-sources]\ndirectory = ' + json.dumps(str(destination)) + '\n').encode()
            if args[0] == "fixture-rustc":
                return str(self.sysroot).encode() if "--print" in args else b"rustc fixture 1.0\n"
            if args[0] in {"fixture-cmake", "fixture-cargo"}:
                return b"tool fixture 1.0\n"
            return original(*args, **kwargs)

        def build(args, **kwargs):
            if args[0] not in {"fixture-cmake", "fixture-cargo"}:
                return original_run(args, **kwargs)
            if args[0] == "fixture-cmake" and "--build" in args:
                home = Path(kwargs["env"]["CARGO_HOME"])
                config = tomllib.loads((home / "config.toml").read_text())
                self.assertTrue(config["net"]["offline"])
                path = self.build / "Release/ColonyCamera.dll"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(pe_fixture())
                path.with_suffix(".pdb").write_bytes(b"Symbol fixture")
            return subprocess.CompletedProcess(args, 0)

        return patch.object(packaging, "command", side_effect=tool), patch.object(packaging.subprocess, "run", side_effect=build)

    def test_player_is_minimal_and_sources_match_default_ini_is_unchanged(self):
        ini = self.root / "assets/SKSE/Plugins/ColonyCamera.ini"
        before = ini.read_bytes()
        installed = self.base / "installed/ColonyCamera.ini"
        write(installed, "User configuration must survive\n")
        tools, build = self.compiler_tools()
        with tools, build as builder:
            output = packaging.package(self.args, self.root)
        calls = [call.args[0] for call in builder.call_args_list
                 if call.args[0][0] in {"fixture-cmake", "fixture-cargo"}]
        self.assertEqual(len(calls), 3)
        self.assertIn("--clean-first", calls[1])
        self.assertIn("package-cargo-", calls[0][-1])
        self.assertEqual(calls[2][-1], f"-DCC_RUST_TARGET_DIR:PATH={(self.build / 'cargo').as_posix()}")
        self.assertEqual(ini.read_bytes(), before)
        self.assertEqual(installed.read_text(), "User configuration must survive\n")
        prefix = f"Camera-Colony-1.2.3-{self.revision[:12]}"
        with zipfile.ZipFile(output / f"{prefix}.zip") as player:
            self.assertEqual(set(player.namelist()), packaging.PLAYER_FILES)
            self.assertEqual(player.read("SKSE/Plugins/ColonyCamera.ini"), before)
            self.assertIn(self.revision.encode(), player.read("README.txt"))
            notices = player.read("LICENSES.txt")
            self.assertIn(b"Complete MPL notice fixture", notices)
            self.assertIn(b"Complete Menu Framework license fixture", notices)
            self.assertIn(b"EXCEPTIONS.md preserved notice fixture", notices)
            self.assertIn(b"nested/fmt.license.rst preserved notice fixture", notices)
            self.assertIn(b"<html>Complete Rust notice fixture</html>", notices)
        with zipfile.ZipFile(output / f"{prefix}-sources.zip") as sources:
            metadata = json.loads(sources.read("build.json"))
            self.assertEqual(metadata["revision"], self.revision)
            self.assertEqual(metadata["dependencies"]["commonlib"], self.dep_revision)
            with tarfile.open(fileobj=io.BytesIO(sources.read("colony-camera.tar.gz"))) as archive:
                self.assertEqual(archive.extractfile("Colony-Camera/Cargo.toml").read(),
                                 packaging.git(self.root, "show", "HEAD:Cargo.toml"))
            with tarfile.open(fileobj=io.BytesIO(sources.read("dependencies.tar.gz"))) as archive:
                self.assertEqual(archive.extractfile("deps/commonlib/include/source.h").read(),
                                 packaging.git(self.root / "deps/commonlib", "show", "HEAD:include/source.h"))
        hashes = json.loads((output / "SHA256.json").read_text())
        for filename, digest in hashes["archives"].items():
            self.assertEqual(hashlib.sha256((output / filename).read_bytes()).hexdigest(), digest)
        with self.assertRaisesRegex(packaging.PackageError, "Output already exists"):
            packaging.package(self.args, self.root)

    def test_dirty_project_and_dependency_are_rejected_before_build(self):
        write(self.root / "README.md", "uncommitted\n")
        with patch.object(packaging.subprocess, "run", wraps=subprocess.run) as build:
            with self.assertRaisesRegex(packaging.PackageError, "source changes"):
                packaging.package(self.args, self.root)
            self.assertFalse(any(call.args[0][0] in {"fixture-cmake", "fixture-cargo"}
                                 for call in build.call_args_list))
        write(self.root / "deps/commonlib/include/source.h", "uncommitted dependency\n")
        with self.assertRaisesRegex(packaging.PackageError, "source changes"):
            packaging.dependency_sources(self.root)

    def test_locked_crate_sources_and_verbatim_notices_are_bundled(self):
        self.add_crate()
        tools, build = self.compiler_tools()
        with tools, build:
            output = packaging.package(self.args, self.root)
        prefix = f"Camera-Colony-1.2.3-{self.revision[:12]}"
        with zipfile.ZipFile(output / f"{prefix}.zip") as player:
            self.assertEqual(set(player.namelist()), packaging.PLAYER_FILES)
            for name, data in self.crates["serde-1.0.229"].items():
                if name.startswith("LICENSE"):
                    self.assertIn(data, player.read("LICENSES.txt"))
        with zipfile.ZipFile(output / f"{prefix}-sources.zip") as sources:
            metadata = json.loads(sources.read("build.json"))
            self.assertEqual(metadata["rust_crates"][0]["checksum"], self.checksum)
            self.assertEqual(metadata["cargo_lock_sha256"],
                             hashlib.sha256((self.root / "Cargo.lock").read_bytes()).hexdigest())
            with tarfile.open(fileobj=io.BytesIO(sources.read("rust-crates.tar.gz"))) as archive:
                for name, data in self.crates["serde-1.0.229"].items():
                    self.assertEqual(archive.extractfile(f"vendor/serde-1.0.229/{name}").read(), data)
                config = tomllib.loads(archive.extractfile(".cargo/config.toml").read().decode())
                self.assertTrue(config["net"]["offline"])
                self.assertEqual(config["source"]["crates-io"]["replace-with"], "vendored-sources")
                self.assertEqual(config["source"]["vendored-sources"]["directory"], "vendor")

    @unittest.skipUnless(shutil.which("cargo"), "Cargo is required for the offline source rebuild regression")
    def test_extracted_sources_build_offline_without_a_registry_cache(self):
        self.add_crate()
        tools, build = self.compiler_tools()
        with tools, build:
            output = packaging.package(self.args, self.root)
        extracted = self.base / "extracted"
        extracted.mkdir()
        prefix = f"Camera-Colony-1.2.3-{self.revision[:12]}"
        with zipfile.ZipFile(output / f"{prefix}-sources.zip") as sources:
            with tarfile.open(fileobj=io.BytesIO(sources.read("colony-camera.tar.gz"))) as archive:
                archive.extractall(extracted, filter="data")
            root = extracted / "Colony-Camera"
            with tarfile.open(fileobj=io.BytesIO(sources.read("rust-crates.tar.gz"))) as archive:
                archive.extractall(root, filter="data")
        environment = dict(os.environ, CARGO_HOME=str(self.base / "empty-cargo-home"), CARGO_NET_OFFLINE="true")
        result = subprocess.run(["cargo", "check", "--frozen"], cwd=root, env=environment, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        # Directory-source checksums must still protect the extracted source.
        write(root / "vendor/serde-1.0.229/src/lib.rs", "// altered crate source\n")
        result = subprocess.run(["cargo", "check", "--frozen", "--target-dir", str(self.base / "fresh-target")],
                                cwd=root, env=environment, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"checksum", result.stderr)

    def test_crate_checksum_mismatch_or_missing_license_prevents_output(self):
        self.add_crate()
        for defect in ("checksum", "license"):
            with self.subTest(defect=defect):
                if defect == "checksum":
                    self.checksum = "b" * 64
                else:
                    self.checksum = "a" * 64
                    self.crates["serde-1.0.229"] = {"src/lib.rs": b"// No license\n"}
                tools, build = self.compiler_tools()
                with tools, build:
                    with self.assertRaisesRegex(packaging.PackageError, "checksum" if defect == "checksum" else "license"):
                        packaging.package(self.args, self.root)
                self.assertFalse(self.args.output.exists())

    @unittest.skipUnless(shutil.which("cargo"), "Cargo is required for the frozen lockfile regression")
    def test_manifest_lockfile_dependency_mismatch_is_rejected_by_cargo(self):
        write(self.base / "new-dependency/Cargo.toml", '[package]\nname="new-dependency"\nversion="1.0.0"\n')
        write(self.base / "new-dependency/src/lib.rs", "pub fn dependency() {}\n")
        with (self.root / "Cargo.toml").open("a") as file:
            file.write('\n[dependencies]\nnew-dependency = { path = "../new-dependency" }\n')
        original = (self.root / "Cargo.lock").read_bytes()
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(subprocess.CalledProcessError):
                packaging.rust_sources(self.root, "cargo", Path(temp), 1700000000)
        self.assertEqual((self.root / "Cargo.lock").read_bytes(), original)

    def test_wrong_dependency_commit_is_rejected(self):
        manifest = json.loads((self.root / "dependencies.json").read_text())
        manifest["commonlib"]["revision"] = "0" * 40
        write(self.root / "dependencies.json", json.dumps(manifest))
        with self.assertRaisesRegex(packaging.PackageError, "does not match"):
            packaging.dependency_sources(self.root)

    def test_changed_ini_during_build_prevents_candidate_output(self):
        tools, build = self.compiler_tools()
        original_validate = packaging.validate_dll

        def change_ini(data, version):
            original_validate(data, version)
            write(self.root / "assets/SKSE/Plugins/ColonyCamera.ini", "[General]\nenabled = false\n")

        with tools, build, patch.object(packaging, "validate_dll", side_effect=change_ini):
            with self.assertRaisesRegex(packaging.PackageError, "source changes"):
                packaging.package(self.args, self.root)
        self.assertFalse(self.args.output.exists())

    def test_fresh_rust_target_preserves_unmarked_cache_and_restores_after_failure(self):
        original_target = self.build / "cargo"
        original_target.mkdir()
        sentinel = original_target / "user-owned-sentinel"
        sentinel.write_bytes(b"preserve existing cache")
        cache, _ = packaging.build_cache(self.root, self.build, "Release")
        calls = []

        def fail_build(args, **kwargs):
            calls.append(args)
            if "--build" in args:
                raise subprocess.CalledProcessError(1, args)
            return subprocess.CompletedProcess(args, 0)

        with patch.object(packaging.subprocess, "run", side_effect=fail_build):
            with self.assertRaises(subprocess.CalledProcessError):
                packaging.rebuild(self.root, self.build, cache, self.args, {})
        self.assertEqual(len(calls), 3)
        selected = Path(calls[0][-1].split("=", 1)[1])
        self.assertEqual(selected.parent, self.build)
        self.assertFalse(selected.exists())
        self.assertEqual(calls[-1][-1], f"-DCC_RUST_TARGET_DIR:PATH={original_target.as_posix()}")
        self.assertEqual(sentinel.read_bytes(), b"preserve existing cache")
        self.assertFalse((original_target / "CACHEDIR.TAG").exists())
        self.assertFalse(any(args[0] == "fixture-cargo" for args in calls))

    def test_source_links_cannot_escape_dependency_tree(self):
        def source_link(target):
            buffer = io.BytesIO()
            with tarfile.open(fileobj=buffer, mode="w") as archive:
                entry = tarfile.TarInfo("deps/commonlib/config/instructions")
                entry.type = tarfile.SYMTYPE
                entry.linkname = target
                archive.addfile(entry)
            return buffer.getvalue()

        for target in ("../../../escape", "/absolute", "../../other-dependency/file"):
            with self.subTest(target=target), patch.object(packaging, "git", return_value=source_link(target)):
                with self.assertRaises(packaging.PackageError):
                    packaging.archive_source(self.root, self.revision, "deps/commonlib")
        allowed = source_link("../LICENSE")
        with patch.object(packaging, "git", return_value=allowed):
            self.assertEqual(packaging.archive_source(self.root, self.revision, "deps/commonlib"), allowed)

    def test_version_and_cargo_sources_cannot_silently_disagree(self):
        self.assertEqual(packaging.read_version(self.root), "1.2.3")
        write(self.root / "CMakeLists.txt", "project(ColonyCamera VERSION 9.0.0)\n")
        with self.assertRaisesRegex(packaging.PackageError, "versions disagree"):
            packaging.read_version(self.root)
        write(self.root / "CMakeLists.txt", "project(ColonyCamera VERSION 1.2.3)\n")
        write(self.root / "Cargo.lock", 'version = 4\n[[package]]\nname = "camera"\nversion = "1.2.2"\n')
        with self.assertRaisesRegex(packaging.PackageError, "versions disagree"):
            packaging.read_version(self.root)

    def test_native_and_single_config_paths_and_wrong_checkout(self):
        self.assertEqual(packaging.build_cache(self.root, self.build, "Release")[1],
                         self.build / "Release/ColonyCamera.dll")
        with self.assertRaisesRegex(packaging.PackageError, "absent"):
            packaging.build_cache(self.root, self.build, "Unknown")
        self.configure("single")
        self.assertEqual(packaging.build_cache(self.root, self.build, "Release")[1],
                         self.build / "ColonyCamera.dll")
        with self.assertRaisesRegex(packaging.PackageError, "CMAKE_BUILD_TYPE"):
            packaging.build_cache(self.root, self.build, "Debug")
        with self.assertRaisesRegex(packaging.PackageError, "different source"):
            packaging.build_cache(self.base, self.build, "Release")

    def test_zip_paths_no_overwrite_and_deterministic_metadata(self):
        one, two = self.base / "one.zip", self.base / "two.zip"
        entries = {"SKSE/Plugins/config.ini": b"defaults", "LICENSES.txt": b"notices"}
        packaging.write_zip(one, entries, 1700000000)
        packaging.write_zip(two, dict(reversed(list(entries.items()))), 1700000000)
        self.assertEqual(one.read_bytes(), two.read_bytes())
        with self.assertRaises(FileExistsError):
            packaging.write_zip(one, entries, 1700000000)
        for name in ("../escape", "/absolute", "C:/drive", "SKSE\\evil", "a/../b", "a//b"):
            with self.subTest(name=name), self.assertRaises(packaging.PackageError):
                packaging.write_zip(self.base / "unsafe.zip", {name: b"x"}, 1700000000)
        self.assertFalse((self.base / "unsafe.zip").exists())

    def test_dll_rejects_bad_architecture_and_missing_exports(self):
        good = pe_fixture()
        packaging.validate_dll(good, "1.2.3")
        with self.assertRaisesRegex(packaging.PackageError, "version does not match"):
            packaging.validate_dll(good, "1.2.4")
        wrong_arch = bytearray(good)
        struct.pack_into("<H", wrong_arch, 0x84, 0x14C)
        for bad in (b"not a DLL", good[:200], bytes(wrong_arch), good.replace(b"SKSEPlugin_Load", b"WrongPluginName")):
            with self.subTest(size=len(bad)), self.assertRaises(packaging.PackageError):
                packaging.validate_dll(bad)


if __name__ == "__main__":
    unittest.main()
