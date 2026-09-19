"""Read-only validation of runtime, RTTI, camera and native settings entry points."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_image(data):
    require(len(data) >= 64 and data[:2] == b"MZ", "Not a PE executable")
    pe = struct.unpack_from("<I", data, 60)[0]
    require(pe + 160 <= len(data) and data[pe:pe+4] == b"PE\0\0", "Invalid PE header")
    require(struct.unpack_from("<H", data, pe+4)[0] == 0x8664
            and struct.unpack_from("<H", data, pe+24)[0] == 0x20B,
            "Expected Windows x64 PE32+ executable")
    base = struct.unpack_from("<Q", data, pe+48)[0]
    section_count = struct.unpack_from("<H", data, pe+6)[0]
    optional_size = struct.unpack_from("<H", data, pe+20)[0]
    require(optional_size >= 136, "Truncated PE optional header")
    start = pe+24+optional_size
    require(start + section_count * 40 <= len(data), "Truncated PE section table")
    sections = []
    for i in range(section_count):
        at = start + 40*i
        _, va, length, raw = struct.unpack_from("<4I", data, at+8)
        flags = struct.unpack_from("<I", data, at+36)[0]
        require(raw+length <= len(data), "Truncated PE section")
        sections.append((va, length, raw, flags))

    def read(rva, size, executable=False):
        require(rva >= 0 and size > 0, "Invalid RVA or read size")
        for va, length, raw, flags in sections:
            if va <= rva and rva+size <= va+length:
                require(not executable or flags & 0x20000000,
                        f"Expected executable section at {rva:#x}")
                return data[raw+rva-va:raw+rva-va+size]
        raise ValueError(f"RVA outside mapped file: {rva:#x}")

    require(struct.unpack_from("<I", data, pe+24+108)[0] >= 3, "Missing resource directory")
    resource_rva, resource_size = struct.unpack_from("<II", data, pe+24+128)
    resource = read(resource_rva, resource_size)

    visited_directories = set()
    def directory(offset):
        require(offset not in visited_directories, "Repeated resource directory")
        visited_directories.add(offset)
        require(offset + 16 <= len(resource), "Truncated resource directory")
        named, count = struct.unpack_from("<HH", resource, offset+12)
        total = named + count
        require(total <= (len(resource)-offset-16)//8, "Truncated resource entries")
        return [struct.unpack_from("<II", resource, offset+16+8*i) for i in range(total)]

    roots = [target for identifier, target in directory(0) if identifier == 16]
    require(len(roots) == 1 and roots[0] & 0x80000000, "Missing or ambiguous RT_VERSION")
    versions = set()
    for _, name in directory(roots[0] & 0x7fffffff):
        require(name & 0x80000000, "Invalid version-name directory")
        for _, leaf in directory(name & 0x7fffffff):
            require(not leaf & 0x80000000 and leaf+16 <= len(resource), "Invalid version-language entry")
            rva, size = struct.unpack_from("<II", resource, leaf)
            block = read(rva, size)
            key = "VS_VERSION_INFO\0".encode("utf-16-le")
            fixed = (6 + len(key) + 3) & ~3
            require(len(block) >= fixed+52, "Truncated fixed version info")
            length, value_length, kind = struct.unpack_from("<HHH", block)
            require(fixed+52 <= length <= len(block) and value_length == 52 and kind == 0
                    and block[6:6+len(key)] == key, "Invalid version info")
            signature, structure = struct.unpack_from("<2I", block, fixed)
            require((signature, structure) == (0xfeef04bd, 0x10000), "Invalid fixed version signature")

            def child(start, end, wanted):
                matches = []
                at = (start+3) & ~3
                while at < end:
                    require(at+6 <= end, "Truncated version child")
                    size, count, kind = struct.unpack_from("<HHH", block, at)
                    limit = at+size
                    require(size >= 8 and limit <= end, "Invalid version child size")
                    key_end = at+6
                    while key_end+2 <= limit and block[key_end:key_end+2] != b"\0\0":
                        key_end += 2
                    require(key_end+2 <= limit, "Unterminated version key")
                    name = block[at+6:key_end].decode("utf-16le")
                    value = (key_end+2+3) & ~3
                    value_end = value+count*(2 if kind == 1 else 1)
                    require(kind in (0,1) and value_end <= limit, "Invalid version child value")
                    if name.casefold() == wanted.casefold():
                        matches.append((value,value_end,limit,kind))
                    at = (limit+3) & ~3
                require(len(matches) == 1, f"Missing or ambiguous {wanted}")
                return matches[0]

            # Match CommonLib's version source. Steam 1.5.97 has fixed 1.0.0.0.
            begin, end, limit, _ = child(fixed+52,length,"StringFileInfo")
            require(begin == end, "Unexpected StringFileInfo value")
            begin, end, limit, _ = child(end,limit,"040904B0")
            require(begin == end, "Unexpected string table value")
            begin, end, _, kind = child(end,limit,"ProductVersion")
            require(kind == 1, "ProductVersion is not a string")
            product = block[begin:end].decode("utf-16le")
            require(re.fullmatch(r"[0-9]{1,5}(?:\.[0-9]{1,5}){3}\x00", product),
                    "Invalid ProductVersion string")
            version = tuple(map(int,product[:-1].split(".")))
            require(all(part <= 65535 for part in version), "ProductVersion component overflow")
            versions.add(version)
    require(len(versions) == 1, "Missing or conflicting executable versions")
    return base, read, versions.pop()


def runtime_ids(version):
    require(version in ((1,7,104,0), (1,6,1170,0), (1,6,640,0), (1,5,97,0)),
            "Runtime is outside the inspection target list")
    se = version == (1,5,97,0)
    # CommonLib c7662fc5: primary VTABLEs, NiAVObject and MessageBoxMenu.
    # Improved Camera 2e441c19: model/camera update relocation pairs.
    return {
        "thirdVtable": 256647 if se else 205236,
        "firstVtable": 267810 if se else 214855,
        "cameraVtable": 256547 if se else 205197,
        "collision": 49899 if se else 50832,
        "matrix": 69271 if se else 70641,
        "sceneUpdate": 68900 if se else 70251,
        "bodySceneUpdate": 39446 if se else 40522,
        "playerCameraUpdate": 49852 if se else 50784,
        "messageBox": 51421 if se else (52270 if version < (1,6,1130,0) else 442726),
    }


def inspect_runtime(exe, database):
    """Collect native evidence; this never adds a runtime to the DLL allowlist."""
    from runtime_database import read_database
    data, db = exe.read_bytes(), database.read_bytes()
    version, addresses = read_database(db)
    base, read, image_version = read_image(data)
    require(version == image_version, "Executable and Address Library versions differ")
    ids = runtime_ids(version)
    def address(name):
        value = addresses.get(ids[name], 0)
        require(value > 0, f"Missing Address Library ID for {name}")
        return value
    def slot(table, index):
        return struct.unpack("<Q", read(table+8*index, 8))[0] - base
    tables = {}
    for name, class_name in (("thirdVtable", "ThirdPersonState"),
                             ("firstVtable", "FirstPersonState"), ("cameraVtable", "TESCamera")):
        rva = address(name)
        locator = slot(rva, -1)
        signature, offset, _, descriptor, _, self_rva = struct.unpack("<6I", read(locator,24))
        require((signature,offset,self_rva) == (1,0,locator), f"{name}: invalid primary RTTI")
        expected = f".?AV{class_name}@@\0".encode("ascii")
        require(read(descriptor+16,len(expected)) == expected, f"{name}: wrong RTTI class")
        tables[name] = rva
    entries = {name: address(name) for name in ids if not name.endswith("Vtable")}
    # Resolve TESCamera::Update through its verified primary vtable, not a guessed SE ID.
    entries["cameraUpdate"] = slot(tables["cameraVtable"], 2)
    for name, table, index in (("begin","thirdVtable",1),("end","thirdVtable",2),
                               ("update","thirdVtable",3),("firstBegin","firstVtable",1),
                               ("firstEnd","firstVtable",2),("firstUpdate","firstVtable",3),
                               ("firstTranslation","firstVtable",5)):
        entries[name] = slot(tables[table], index)
    output = {name: {"rva": rva, "prefix": read(rva,16,True).hex()}
              for name,rva in entries.items()}
    calls = {}
    for name, owner, offset, target in (("bodySceneCall","bodySceneUpdate",0xd7,"sceneUpdate"),
                                        ("cameraUpdateCall","playerCameraUpdate",0x1a6,"cameraUpdate")):
        site = entries[owner] + offset
        instruction = read(site,5,True)
        require(instruction[0] == 0xe8, f"{name}: expected E8 call at reference offset")
        destination = site + 5 + struct.unpack_from("<i",instruction,1)[0]
        require(destination == entries[target], f"{name}: call destination differs from expected native function")
        calls[name] = {"rva":site,"target":destination,
                       "before":read(site-14,14,True).hex(), "after":read(site+5,12,True).hex()}
    return {"status":"inspection_only_not_runtime_support", "runtime":".".join(map(str,version)),
            "entries":output, "vtables":tables, "calls":calls,
            "exe_sha256":hashlib.sha256(data).hexdigest(),
            "database_sha256":hashlib.sha256(db).hexdigest()}


def verify_profile(report, header):
    """Compare compiled-profile declarations with independently inspected evidence."""
    namespace = "steam" + report["runtime"].removesuffix(".0").replace(".", "")
    blocks = re.findall(r"namespace " + re.escape(namespace) + r" \{(.*?)\n\}", header, re.S)
    require(len(blocks) == 1, "Missing or ambiguous runtime profile")
    block = blocks[0]
    entries = re.findall(r'Entry (\w+)\{"\w+", (0x[0-9A-F]+), \{([^}]+)\}', block)
    require(len(entries) == len(report["entries"])
            and {entry[0] for entry in entries} == set(report["entries"]),
            "Runtime profile entry inventory mismatch")
    for name, rva, prefix in entries:
        actual = {"rva": int(rva,16), "prefix": bytes(int(x,16) for x in prefix.split(",")).hex()}
        require(actual == report["entries"][name], f"{name}: entry profile mismatch")
    tables = re.findall(r'std::uintptr_t (\w+Vtable) = (0x[0-9A-F]+);', block)
    require(len(tables) == len(report["vtables"])
            and {name:int(rva,16) for name,rva in tables} == report["vtables"],
            "Runtime profile vtable mismatch")
    calls = re.findall(r'SceneCall (\w+)\{(0x[0-9A-F]+), (0x[0-9A-F]+),\s*'
                       r'\{([^}]+)\},\s*\{([^}]+)\}\};', block)
    require(len(calls) == len(report["calls"])
            and {call[0] for call in calls} == set(report["calls"]),
            "Runtime profile call inventory mismatch")
    for name, rva, target, before, after in calls:
        actual = {"rva":int(rva,16), "target":int(target,16),
                  "before":bytes(int(x,16) for x in before.split(",")).hex(),
                  "after":bytes(int(x,16) for x in after.split(",")).hex()}
        require(actual == report["calls"][name], f"{name}: call profile mismatch")
    return {"runtime":report["runtime"], "entries_verified":len(entries),
            "scene_calls_verified":1, "camera_calls_verified":1,
            "rtti_vtables_verified":len(tables),
            "status":"profile_verified_gameplay_unverified",
            "exe_sha256":report["exe_sha256"], "database_sha256":report["database_sha256"]}


def verify_compiled_profile(report, cxx, header_path=None):
    """Compile assertions against Select(), not merely the named constants."""
    path = header_path or Path(__file__).resolve().parents[1] / "plugin/runtime.h"
    version = list(map(int,report["runtime"].split(".")))
    packed = version[0]<<24 | version[1]<<16 | version[2]<<4 | version[3]
    lines = ['#include "runtime.h"', f'constexpr auto p = runtime::Select({packed}u);',
             'static_assert(p != nullptr);', f'static_assert(p->version == {packed}u);']
    def scalar(field, value): lines.append(f"static_assert(p->{field} == {value}u);")
    def array(field, value):
        data = bytes.fromhex(value)
        lines.append(f"static_assert(p->{field} == std::array<unsigned char,{len(data)}>{{"
                     + ",".join(map(str,data)) + "});")
    for name, entry in report["entries"].items():
        scalar(name+".rva",entry["rva"]); array(name+".prefix",entry["prefix"])
    for name, rva in report["vtables"].items(): scalar(name,rva)
    for name, call in report["calls"].items():
        for field in ("rva","target"): scalar(name+"."+field,call[field])
        for field in ("before","after"): array(name+"."+field,call[field])
    result = subprocess.run([cxx,"-std=c++20","-fsyntax-only","-x","c++","-I",str(path.parent),"-"],
                            input="\n".join(lines),text=True,capture_output=True,timeout=60)
    require(result.returncode == 0, "Compiled runtime profile mismatch: " + result.stderr[-4000:])


def verify(exe, database, cxx=None):
    report = inspect_runtime(exe, database)
    header = (Path(__file__).resolve().parents[1] / "plugin/runtime.h").read_text()
    result = verify_profile(report, header)
    if cxx:
        verify_compiled_profile(report,cxx)
        result["compiled_profile_verified"] = True
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("database", type=Path)
    parser.add_argument("--inspect", action="store_true",
                        help="Collect candidate runtime evidence without claiming compatibility")
    parser.add_argument("--cxx", help="Also compile Select() assertions with c++ or clang++")
    args = parser.parse_args()
    if args.inspect and args.cxx:
        parser.error("--cxx verifies a compiled profile; use it without --inspect")
    try:
        result = inspect_runtime(args.exe,args.database) if args.inspect else verify(args.exe,args.database,args.cxx)
        print(json.dumps(result,indent=2))
    except (OSError, ValueError, struct.error, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"Runtime verification failed: {error}") from error
