"""Read-only validation of the exact runtime and camera entry points."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("exe", type=Path)
parser.add_argument("database", type=Path)
args = parser.parse_args()
data = args.exe.read_bytes()
db = args.database.read_bytes()
if struct.unpack_from("<5I", db) != (5, 1, 7, 104, 0):
    raise SystemExit("Expected Address Library format 5 for 1.7.104.0")
ptr, encoding, count = struct.unpack_from("<3I", db, 84)
if (ptr, encoding) != (8, 0) or len(db) != 96 + count * 4:
    raise SystemExit("Unsupported database layout")
addresses = struct.unpack_from(f"<{count}I", db, 96)
pe = struct.unpack_from("<I", data, 60)[0]
if data[:2] != b"MZ" or data[pe:pe+4] != b"PE\0\0":
    raise SystemExit("Not a PE executable")
base = struct.unpack_from("<Q", data, pe+48)[0]
section_count = struct.unpack_from("<H", data, pe+6)[0]
start = pe+24+struct.unpack_from("<H", data, pe+20)[0]
sections = [struct.unpack_from("<4I", data, start+40*i+8) for i in range(section_count)]
def read(rva, size):
    for _, va, length, raw in sections:
        if va <= rva and rva+size <= va+length:
            return data[raw+rva-va:raw+rva-va+size]
    raise ValueError(f"RVA outside mapped file: {rva:#x}")
header = (Path(__file__).resolve().parents[1] / "plugin/runtime.h").read_text()
entries = re.findall(r'Entry (\w+)\{"\w+", (0x[0-9A-F]+), \{([^}]+)\}', header)
assert len(entries) == 5
for name, rva_text, prefix_text in entries:
    rva = int(rva_text, 16)
    prefix = bytes(int(x, 16) for x in prefix_text.split(','))
    assert read(rva, len(prefix)) == prefix, f"{name}: entry bytes mismatch"
    if name in ("begin", "end", "update"):
        slot = {"begin": 1, "end": 2, "update": 3}[name]
        assert struct.unpack("<Q", read(addresses[205236]+8*slot, 8))[0] == base+rva
    else:
        assert addresses[50832 if name == "collision" else 70641] == rva
print(json.dumps({"runtime":"1.7.104.0", "entries_verified": len(entries),
                  "exe_sha256": hashlib.sha256(data).hexdigest(),
                  "database_sha256": hashlib.sha256(db).hexdigest()}, indent=2))
