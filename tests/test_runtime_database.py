"""Synthetic decoder tests; optional real databases stay outside the repository.

Set SKYRIM_ADDRESS_LIBRARY_DIR to an installed SKSE/Plugins directory to run
the four read-only integration tests. No network access is needed by tests.
"""

import importlib.util
import os
from pathlib import Path
import struct
import tracemalloc
import unittest


SPEC = importlib.util.spec_from_file_location(
    "runtime_database", Path(__file__).resolve().parents[1] / "scripts/runtime_database.py")
database = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(database)
read_database = database.read_database


def sparse(payload=b"", count=0, *, format=2, version=(1, 6, 1170, 0),
           name=b"SkyrimSE.exe", pointer=8):
    return (struct.pack("<6I", format, *version, len(name)) + name
            + struct.pack("<II", pointer, count) + payload)


def dense(payload=b"", count=0, *, pointer=8, encoding=0):
    return (struct.pack("<5I64sIII", 5, 1, 7, 104, 0, b"SkyrimSE.exe",
                        pointer, encoding, count) + payload)


def absolute(identifier, offset):
    return b"\x00" + struct.pack("<QQ", identifier, offset)


class DatabaseTests(unittest.TestCase):
    def test_sparse_formats_return_version_and_explicit_ids(self):
        for format, version in ((1, (1, 5, 97, 0)), (2, (1, 6, 640, 0)),
                                (2, (1, 6, 1170, 0))):
            with self.subTest(format=format, version=version):
                data = sparse(absolute(90, 1234) + absolute(3, 5678), 2,
                              format=format, version=version)
                self.assertEqual(read_database(data), (version, {90: 1234, 3: 5678}))

    def test_dense_zero_slots_are_absent_without_renumbering(self):
        self.assertEqual(read_database(dense(struct.pack("<5I", 0, 1234, 0, 5678, 0), 5)),
                         ((1, 7, 104, 0), {1: 1234, 3: 5678}))

    def test_all_delta_types_and_scaled_offsets(self):
        # Literal results for a previous value of 1000, independent of the decoder.
        cases = ((0, struct.pack("<Q", 0x100000005), 0x100000005),
                 (1, b"", 1001), (2, b"\x07", 1007), (3, b"\x07", 993),
                 (4, struct.pack("<H", 300), 1300),
                 (5, struct.pack("<H", 300), 700),
                 (6, struct.pack("<H", 321), 321),
                 (7, struct.pack("<I", 0x12345678), 0x12345678))
        for format in (1, 2):
            for scaled in (False, True):
                for id_type, id_bytes, identifier in cases:
                    for offset_type, offset_bytes, offset in cases:
                        with self.subTest(format=format, scaled=scaled,
                                          id_type=id_type, offset_type=offset_type):
                            previous = 8003 if scaled else 1000
                            tag = id_type | (offset_type << 4) | (0x80 if scaled else 0)
                            payload = absolute(1000, previous) + bytes([tag]) + id_bytes + offset_bytes
                            self.assertEqual(read_database(sparse(payload, 2, format=format))[1],
                                             {1000: previous, identifier: offset * (8 if scaled else 1)})

    def test_scaled_then_unscaled_uses_previous_byte_offset(self):
        payload = absolute(10, 83) + b"\x91\x21\x03"
        self.assertEqual(read_database(sparse(payload, 3))[1], {10: 83, 11: 88, 12: 91})

    def test_initial_delta_base_is_zero(self):
        self.assertEqual(read_database(sparse(b"\x91", 1))[1], {1: 8})

    def test_empty_databases_and_opaque_names(self):
        for data in (sparse(), sparse(format=1, name=b""), sparse(name=b"\xff\x00"), dense()):
            with self.subTest(data=data):
                self.assertEqual(read_database(data)[1], {})

    def test_sparse_zero_is_an_explicit_mapping(self):
        self.assertEqual(read_database(sparse(absolute(0, 0), 1))[1], {0: 0})

    def test_every_truncated_prefix_is_rejected(self):
        fixtures = (sparse(absolute(8, 16), 1, format=1),
                    sparse(absolute(8, 16) + b"\xf4" + struct.pack("<HI", 300, 321), 2),
                    dense(struct.pack("<3I", 0, 32, 64), 3))
        for data in fixtures:
            for size in range(len(data)):
                with self.subTest(format=data[0], size=size), self.assertRaises(ValueError):
                    read_database(data[:size])

    def test_trailing_bytes_and_wrong_counts_are_rejected(self):
        for data in (sparse(absolute(1, 8), 0), sparse(absolute(1, 8), 2),
                     sparse(absolute(1, 8), 1) + b"\x00", dense(b"\x00"),
                     dense(struct.pack("<I", 8), 2), dense(struct.pack("<I", 8), 0)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                read_database(data)

    def test_unsupported_formats_are_rejected(self):
        for format in (0, 3, 4, 6, 0xFFFFFFFF):
            with self.subTest(format=format), self.assertRaisesRegex(ValueError, "format"):
                read_database(sparse(format=format))

    def test_invalid_id_type_nibbles_are_rejected(self):
        for tag in range(256):
            if tag & 15 >= 8:
                with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, "type"):
                    read_database(sparse(bytes([tag]) + b"\0" * 16, 1))

    def test_invalid_pointer_sizes_and_dense_encoding_are_rejected(self):
        for pointer in (0, 1, 4, 16, 0xFFFFFFFF):
            for data in (sparse(pointer=pointer), dense(pointer=pointer)):
                with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                    read_database(data)
        for encoding in (1, 2, 0xFFFFFFFF):
            with self.subTest(encoding=encoding), self.assertRaises(ValueError):
                read_database(dense(encoding=encoding))

    def test_version_components_cannot_be_silently_narrowed(self):
        for template in (sparse(), dense()):
            for at in (4, 8, 12, 16):
                for value in (0x10000, 0xFFFFFFFF):
                    data = bytearray(template)
                    struct.pack_into("<I", data, at, value)
                    with self.subTest(at=at, value=value), self.assertRaises(ValueError):
                        read_database(bytes(data))

    def test_duplicate_ids_are_rejected_even_with_identical_offsets(self):
        for payload in (absolute(4, 8) * 2, absolute(4, 8) + absolute(4, 16),
                        absolute(4, 8) + absolute(5, 9) + b"\x13\x01"):
            count = 3 if payload.endswith(b"\x13\x01") else 2
            with self.subTest(payload=payload), self.assertRaisesRegex(ValueError, "[Dd]uplicate"):
                read_database(sparse(payload, count))

    def test_uint64_limits_do_not_wrap(self):
        maximum = (1 << 64) - 1
        self.assertEqual(read_database(sparse(absolute(maximum, maximum), 1))[1],
                         {maximum: maximum})
        payloads = (b"\x13\x01", b"\x31\x01", b"\x15\x01\x00", b"\x51\x01\x00",
                    absolute(maximum, 0) + b"\x11", absolute(0, maximum) + b"\x11",
                    absolute(maximum, 0) + b"\x12\x01",
                    absolute(0, maximum) + b"\x41\x01\x00",
                    b"\x81" + struct.pack("<Q", 1 << 61),
                    absolute(0, maximum) + b"\x91")
        for payload in payloads:
            count = 2 if payload.startswith(b"\x00") else 1
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                read_database(sparse(payload, count))

    def test_hostile_lengths_and_counts_do_not_allocate_from_header(self):
        bad_name = bytearray(sparse())
        struct.pack_into("<I", bad_name, 20, 0x7FFFFFFF)
        fixtures = [bytes(bad_name)]
        struct.pack_into("<I", bad_name, 20, 0xFFFFFFFF)
        fixtures.append(bytes(bad_name))
        for count in (0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
            fixtures.extend((sparse(b"\x11", count), dense(b"\0" * 4, count)))
        tracemalloc.start()
        try:
            for data in fixtures:
                with self.subTest(data=data), self.assertRaises(ValueError):
                    read_database(data)
            self.assertLess(tracemalloc.get_traced_memory()[1], 1_000_000)
        finally:
            tracemalloc.stop()

    def test_input_must_be_bytes(self):
        for data in (None, "", [], bytearray(sparse()), memoryview(sparse())):
            with self.subTest(type=type(data)), self.assertRaises(TypeError):
                read_database(data)


@unittest.skipUnless(os.environ.get("SKYRIM_ADDRESS_LIBRARY_DIR"),
                     "Set SKYRIM_ADDRESS_LIBRARY_DIR for read-only real-file tests")
class InstalledDatabaseTests(unittest.TestCase):
    def check_database(self, filename, version, expected):
        path = Path(os.environ["SKYRIM_ADDRESS_LIBRARY_DIR"]) / filename
        actual_version, addresses = read_database(path.read_bytes())
        self.assertEqual(actual_version, version)
        for identifier, offset in expected.items():
            with self.subTest(identifier=identifier):
                self.assertEqual(addresses[identifier], offset)

    # Public mappings: alandtse/skyrim_vr_address_library at
    # a68aa583e6c66bf92be3fa81860060ad7be73094, offsets-<runtime>.csv.
    # The 1.5.97 CSV contains VAs; subtract its 0x140000000 image base.
    def test_1_5_97(self):
        self.check_database("version-1-5-97-0.bin", (1, 5, 97, 0),
                            {11483: 0x10F5C0, 256647: 0x162F2A0, 267810: 0x16A9E60})

    def test_1_6_1170(self):
        self.check_database("versionlib-1-6-1170-0.bin", (1, 6, 1170, 0),
                            {11483: 0x14FCD0, 205236: 0x1872B48, 214855: 0x18EF1F8})

    def test_1_7_104(self):
        self.check_database("versionlib-1-7-104-0.bin", (1, 7, 104, 0),
                            {11483: 0x155260, 205236: 0x18ECC08, 214855: 0x196D8F0})

    def test_1_6_640(self):
        # ianpatt/skse64 v2.2.3 (5409b8ba273a64769bb5744d06fffa52f2b687e3),
        # skse64/GameRTTI.inl: ThirdPersonState and FirstPersonState.
        # IDs: pinned CommonLib include/RE/Offsets_RTTI.h (AE VariantID field).
        self.check_database("versionlib-1-6-640-0.bin", (1, 6, 640, 0),
                            {394200: 0x1ECBB40, 396630: 0x1F13FB8})


if __name__ == "__main__":
    unittest.main()
