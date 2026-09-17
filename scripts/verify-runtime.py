"""Read-only validation of runtime, RTTI, camera and native settings entry points."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(exe, database):
    data, db = exe.read_bytes(), database.read_bytes()
    require(len(db) >= 96, "Truncated Address Library")
    require(struct.unpack_from("<5I", db) == (5, 1, 7, 104, 0),
            "Expected Address Library format 5 for 1.7.104.0")
    ptr, encoding, count = struct.unpack_from("<3I", db, 84)
    require((ptr, encoding) == (8, 0) and len(db) == 96 + count * 4,
            "Unsupported database layout")
    addresses = struct.unpack_from(f"<{count}I", db, 96)
    require(len(data) >= 64 and data[:2] == b"MZ", "Not a PE executable")
    pe = struct.unpack_from("<I", data, 60)[0]
    require(pe + 112 <= len(data) and data[pe:pe+4] == b"PE\0\0", "Invalid PE header")
    require(struct.unpack_from("<H", data, pe+4)[0] == 0x8664
            and struct.unpack_from("<H", data, pe+24)[0] == 0x20B,
            "Expected Windows x64 PE32+ executable")
    base = struct.unpack_from("<Q", data, pe+48)[0]
    section_count = struct.unpack_from("<H", data, pe+6)[0]
    start = pe+24+struct.unpack_from("<H", data, pe+20)[0]
    require(start + section_count * 40 <= len(data), "Truncated PE section table")
    sections = []
    for i in range(section_count):
        at = start + 40*i
        _, va, length, raw = struct.unpack_from("<4I", data, at+8)
        flags = struct.unpack_from("<I", data, at+36)[0]
        require(raw+length <= len(data), "Truncated PE section")
        sections.append((va, length, raw, flags))

    def read(rva, size, executable=False):
        for va, length, raw, flags in sections:
            if va <= rva and rva+size <= va+length:
                require(not executable or flags & 0x20000000,
                        f"Expected executable section at {rva:#x}")
                return data[raw+rva-va:raw+rva-va+size]
        raise ValueError(f"RVA outside mapped file: {rva:#x}")

    header = (Path(__file__).resolve().parents[1] / "plugin/runtime.h").read_text()
    entries = re.findall(r'Entry (\w+)\{"\w+", (0x[0-9A-F]+), \{([^}]+)\}', header)
    expected = {"begin", "end", "update", "collision", "matrix",
                "firstBegin", "firstEnd", "firstUpdate", "firstTranslation", "messageBox",
                "sceneUpdate", "bodySceneUpdate", "cameraUpdate", "playerCameraUpdate"}
    require(len(entries) == len(expected) and {e[0] for e in entries} == expected,
            "Runtime header entry inventory differs from verifier")
    vtables = dict(re.findall(r'std::uintptr_t (\w+Vtable) = (0x[0-9A-F]+);', header))
    table_ids = {"thirdVtable": (205236, "ThirdPersonState"),
                 "firstVtable": (214855, "FirstPersonState")}
    for name, (identifier, class_name) in table_ids.items():
        require(identifier < count and name in vtables, f"Missing {name} database entry")
        rva = int(vtables[name], 16)
        require(addresses[identifier] == rva, f"{name}: Address Library mismatch")
        # Primary MSVC complete-object locator: verify class identity and offset 0.
        locator = struct.unpack("<Q", read(rva-8, 8))[0] - base
        signature, offset, _, descriptor, _, self_rva = struct.unpack("<6I", read(locator, 24))
        require((signature, offset, self_rva) == (1, 0, locator), f"{name}: invalid primary RTTI locator")
        type_name = f".?AV{class_name}@@\0".encode("ascii")
        require(read(descriptor+16, len(type_name)) == type_name, f"{name}: RTTI class mismatch")
    direct_ids = {"collision": 50832, "matrix": 70641, "messageBox": 442726,
                  "sceneUpdate": 70251, "bodySceneUpdate": 40522,
                  "cameraUpdate": 33025, "playerCameraUpdate": 50784}
    for name, rva_text, prefix_text in entries:
        rva = int(rva_text, 16)
        prefix = bytes(int(x, 16) for x in prefix_text.split(','))
        require(len(prefix) == 16 and read(rva, 16, executable=True) == prefix,
                f"{name}: entry bytes mismatch")
        if name in direct_ids:
            identifier = direct_ids[name]
            require(identifier < count and addresses[identifier] == rva,
                    f"{name}: Address Library mismatch")
        else:
            first = name.startswith("first")
            method = name[5:].lower() if first else name
            slot = {"begin": 1, "end": 2, "update": 3, "translation": 5}[method]
            table = int(vtables["firstVtable" if first else "thirdVtable"], 16)
            require(struct.unpack("<Q", read(table+8*slot, 8))[0] == base+rva,
                    f"{name}: vtable slot mismatch")
    calls = re.findall(r'SceneCall (\w+)\{(0x[0-9A-F]+), (0x[0-9A-F]+),\s*'
                       r'\{([^}]+)\},\s*\{([^}]+)\}\};', header)
    # These instruction offsets were decoded from the supplied 1.7.104 image.
    expected_calls = {"bodySceneCall": (40522, 0xD7, 70251),
                      "cameraUpdateCall": (50784, 0x1A6, 33025)}
    require(len(calls) == len(expected_calls) and {c[0] for c in calls} == set(expected_calls),
            "Runtime call inventory differs from verifier")
    for name, site_text, target_text, before_text, after_text in calls:
        site, target = int(site_text, 16), int(target_text, 16)
        before, after = (bytes(int(x, 16) for x in part.split(','))
                         for part in (before_text, after_text))
        require(len(before) == 14 and len(after) == 12, f"{name}: context size mismatch")
        require(read(site-len(before), len(before), executable=True) == before
                and read(site+5, len(after), executable=True) == after,
                f"{name}: context mismatch")
        instruction = read(site, 5, executable=True)
        require(instruction[0] == 0xE8 and site+5+struct.unpack_from('<i', instruction, 1)[0] == target,
                f"{name}: instruction or target mismatch")
        owner_id, offset, target_id = expected_calls[name]
        require(owner_id < count and target_id < count
                and target == addresses[target_id] and site == addresses[owner_id]+offset,
                f"{name}: Address Library owner/target mismatch")
    return {"runtime": "1.7.104.0", "entries_verified": len(entries),
            "scene_calls_verified": 1,
            "camera_calls_verified": 1,
            "rtti_vtables_verified": len(table_ids),
            "exe_sha256": hashlib.sha256(data).hexdigest(),
            "database_sha256": hashlib.sha256(db).hexdigest()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("database", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.exe, args.database), indent=2))
    except (OSError, ValueError, struct.error) as error:
        raise SystemExit(f"Runtime verification failed: {error}") from error
