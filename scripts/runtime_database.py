"""Strict Skyrim Address Library reader for formats 1, 2 and 5.

Schema follows deps/commonlib/{include,src}/REL/IDDB.{h,cpp}, pinned at
c7662fc59e531f8c14d00665c5ad15a0577dc9b6. All integers are little-endian.
Formats 1/2: version[4], name length + bytes, pointer size, entry count,
then tagged ID/offset deltas. Format 5: version[4], name[64], pointer size,
encoding (0), slot count, then uint32 RVAs indexed by ID (zero = absent).
"""

import struct


def read_database(data: bytes) -> tuple[tuple[int, int, int, int], dict[int, int]]:
    """Return runtime version and ID-to-RVA mappings, without accessing files.

    Only 8-byte pointers are supported. Malformed/unsupported data raises
    ValueError; non-bytes input raises TypeError. Sparse zero offsets remain
    explicit mappings, matching CommonLib. No runtime compatibility is implied.
    Work and memory are bounded by the supplied bytes, never a header alone.
    """
    if not isinstance(data, bytes):
        raise TypeError("Address Library data must be bytes")
    position = 0

    def take(size):
        nonlocal position
        if size < 0 or size > len(data) - position:
            raise ValueError(f"Truncated Address Library at byte {position}")
        start = position
        position += size
        return start

    def read(code):
        return struct.unpack_from("<" + code, data, take(struct.calcsize("<" + code)))[0]

    format = read("i")
    if format not in (1, 2, 5):
        raise ValueError(f"Unsupported Address Library format: {format}")
    version = struct.unpack_from("<4I", data, take(16))
    if any(component > 0xFFFF for component in version):
        raise ValueError("Invalid runtime version component")
    take(64 if format == 5 else read("i"))
    if read("i") != 8:
        raise ValueError("Unsupported Address Library pointer size")
    if format == 5 and read("i") != 0:
        raise ValueError("Unsupported Address Library data encoding")
    count = read("i")
    remaining = len(data) - position
    if count < 0 or count > remaining // (4 if format == 5 else 1):
        raise ValueError("Invalid or truncated Address Library entry count")
    if format == 5:
        if remaining != count * 4:
            raise ValueError("Trailing Address Library data")
        return version, {identifier: offset for identifier, (offset,) in enumerate(
            struct.iter_unpack("<I", memoryview(data)[position:])) if offset}

    def decode(kind, previous):
        if kind == 0:
            return read("Q")
        if kind == 1:
            return previous + 1
        if kind in (2, 3, 4, 5):
            delta = read("B" if kind < 4 else "H")
            return previous + delta if kind % 2 == 0 else previous - delta
        if kind in (6, 7):
            return read("H" if kind == 6 else "I")
        raise ValueError(f"Invalid Address Library ID type: {kind}")

    addresses = {}
    identifier = offset = 0
    for _ in range(count):
        tag = read("B")
        identifier = decode(tag & 15, identifier)
        scale = 8 if tag & 0x80 else 1
        # CommonLib truncates the previous byte offset before scaled deltas.
        offset = decode((tag >> 4) & 7, offset // scale) * scale
        if not (0 <= identifier <= 0xFFFFFFFFFFFFFFFF and 0 <= offset <= 0xFFFFFFFFFFFFFFFF):
            raise ValueError("Address Library ID or offset outside uint64 range")
        if identifier in addresses:
            raise ValueError(f"Duplicate Address Library ID: {identifier}")
        addresses[identifier] = offset
    if position != len(data):
        raise ValueError("Trailing Address Library data")
    return version, addresses
