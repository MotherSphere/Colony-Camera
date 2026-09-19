"""Small PE fixtures: resource version must match the selected Address Library."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
import tempfile
import shutil
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
spec = importlib.util.spec_from_file_location("verify_runtime", Path(__file__).resolve().parents[1] / "scripts/verify-runtime.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def version_block(key, value=b"", children=b""):
    block=bytearray(struct.pack("<HHH",0,len(value)//2,1)+f"{key}\0".encode("utf-16le"))
    block.extend(bytes(-len(block)%4)); block.extend(value)
    if children:
        block.extend(bytes(-len(block)%4)); block.extend(children)
    struct.pack_into("<H",block,0,len(block))
    return bytes(block)


def image(version=(1, 6, 1170, 0), product=None):
    data = bytearray(0x600)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 240)
    struct.pack_into("<H", data, 0x98, 0x20b)
    struct.pack_into("<Q", data, 0xb0, 0x140000000)
    struct.pack_into("<I", data, 0x104, 16)
    struct.pack_into("<II", data, 0x118, 0x1000, 0x400)
    struct.pack_into("<IIII", data, 0x190, 0x400, 0x1000, 0x400, 0x200)
    struct.pack_into("<I", data, 0x1ac, 0x40000040)
    # RT_VERSION -> name 1 -> language 1033 -> resource data entry.
    for off, identifier, next_off in [(0,16,0x20),(0x20,1,0x40),(0x40,1033,0x60)]:
        struct.pack_into("<HH", data, 0x200+off+12, 0, 1)
        struct.pack_into("<II", data, 0x200+off+16, identifier, next_off | (0x80000000 if off != 0x40 else 0))
    key = "VS_VERSION_INFO\0".encode("utf-16-le")
    value_off = (6 + len(key) + 3) & ~3
    value = bytearray(value_off + 52)
    struct.pack_into("<HHH", value, 0, len(value), 52, 0)
    value[6:6+len(key)] = key
    struct.pack_into("<IIII", value, value_off, 0xFEEF04BD, 0x10000,
                     version[0]<<16|version[1], version[2]<<16|version[3])
    product = ".".join(map(str,version)) if product is None else product
    strings=version_block("StringFileInfo",children=version_block("040904b0",children=
        version_block("ProductVersion",f"{product}\0".encode("utf-16le"))))
    value.extend(bytes(-len(value)%4)); value.extend(strings)
    struct.pack_into("<H",value,0,len(value))
    struct.pack_into("<IIII", data, 0x260, 0x1100, len(value), 0, 0)
    data[0x300:0x300+len(value)] = value
    return bytes(data)

def camera_image(version):
    data=bytearray(image(version)); data.extend(bytes(0x4600-len(data)))
    struct.pack_into("<H",data,0x86,2)
    struct.pack_into("<IIII",data,0x1b8,0x4000,0x2000,0x4000,0x600)
    struct.pack_into("<I",data,0x1d4,0x60000020)
    def put(rva, payload): data[0x600+rva-0x2000:0x600+rva-0x2000+len(payload)]=payload
    # Literal public primary vtable/relocation IDs; not derived from runtime_ids().
    if version==(1,5,97,0):
        ids=[256647,267810,256547,49899,69271,68900,39446,49852,51421]
    else:
        ids=[205236,214855,205197,50832,70641,70251,40522,50784,
             52270 if version==(1,6,640,0) else 442726]
    rvas=[0x2140,0x2340,0x2540,0x3600,0x3700,0x3900,0x3800,0x3a00,0x3e00]
    for table, name in zip(rvas[:3],["ThirdPersonState","FirstPersonState","TESCamera"]):
        locator=table-0x40; descriptor=table+0x80
        put(table-8,struct.pack("<Q",0x140000000+locator))
        put(locator,struct.pack("<6I",1,0,0,descriptor,0,locator))
        put(descriptor+16,f".?AV{name}@@\0".encode())
    for table,index,target in [(0x2140,1,0x3000),(0x2140,2,0x3040),(0x2140,3,0x3080),
                               (0x2340,1,0x3100),(0x2340,2,0x3140),(0x2340,3,0x3180),
                               (0x2340,5,0x31c0),(0x2540,2,0x3d00)]:
        put(table+index*8,struct.pack("<Q",0x140000000+target))
    for site,target in [(0x38d7,0x3900),(0x3ba6,0x3d00)]:
        put(site,b"\xe8"+struct.pack("<i",target-site-5))
    db=struct.pack("<6I",1 if version==(1,5,97,0) else 2,*version,12)+b"SkyrimSE.exe"
    db+=struct.pack("<II",8,len(ids))
    db+=b"".join(b"\0"+struct.pack("<QQ",identifier,rva) for identifier,rva in zip(ids,rvas))
    return bytes(data),db


class RuntimeImageTests(unittest.TestCase):
    def inspect(self,data,db):
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/"SkyrimSE.exe"; database=Path(d)/"addresses.bin"
            exe.write_bytes(data); database.write_bytes(db)
            return module.inspect_runtime(exe,database)

    def test_inspects_all_target_images_without_claiming_support(self):
        for version in [(1,6,1170,0),(1,6,640,0),(1,5,97,0),(1,7,104,0)]:
            report=self.inspect(*camera_image(version))
            self.assertEqual(report["status"],"inspection_only_not_runtime_support")
            self.assertEqual(report["runtime"],".".join(map(str,version)))
            self.assertEqual(len(report["entries"]),14)
            self.assertEqual(report["calls"]["bodySceneCall"]["target"],0x3900)
            self.assertEqual(report["calls"]["cameraUpdateCall"]["target"],0x3d00)

    def test_rejects_wrong_database_version(self):
        data,db=camera_image((1,6,1170,0)); db=bytearray(db)
        struct.pack_into("<I",db,12,640)
        with self.assertRaisesRegex(ValueError,"versions differ"): self.inspect(data,bytes(db))

    def test_rejects_wrong_rtti_opcode_and_call_destination(self):
        data,db=camera_image((1,6,1170,0))
        for rva,payload in [(0x2100,b"\0"),(0x38d7,b"\xe9"),(0x38d8,bytes(4)),(0x3ba7,bytes(4))]:
            altered=bytearray(data); offset=0x600+rva-0x2000
            altered[offset:offset+len(payload)]=payload
            with self.assertRaises(ValueError): self.inspect(bytes(altered),db)

    def test_profile_verification_rejects_changed_evidence(self):
        report=self.inspect(*camera_image((1,6,1170,0)))
        lines=[]
        for name,entry in report["entries"].items():
            prefix=",".join(f"0x{b:02X}" for b in bytes.fromhex(entry["prefix"]))
            lines.append(f'inline constexpr Entry {name}{{"Test", 0x{entry["rva"]:X}, {{{prefix}}}}};')
        for name,rva in report["vtables"].items():
            lines.append(f"inline constexpr std::uintptr_t {name} = 0x{rva:X};")
        for name,call in report["calls"].items():
            before,after=(",".join(f"0x{b:02X}" for b in bytes.fromhex(call[k])) for k in ("before","after"))
            lines.append(f"inline constexpr SceneCall {name}{{0x{call['rva']:X}, 0x{call['target']:X}, {{{before}}}, {{{after}}}}};")
        header="namespace steam161170 {\n"+"\n".join(lines)+"\n}"
        self.assertEqual(module.verify_profile(report,header)["entries_verified"],14)
        for altered in (header.replace("steam161170", "steam16640"),
                        header.replace("0x3600", "0x3601"),
                        header.replace("0x2140", "0x2148"),
                        header.replace("0x38D7", "0x38D8"),
                        header.replace("0x00", "0x01",1),
                        header.replace("0x3900", "0x3910"),
                        header.replace(lines[0],""),
                        header.replace(lines[0],lines[0]+"\n"+lines[0])):
            with self.subTest(header=altered), self.assertRaises(ValueError):
                module.verify_profile(report,altered)

    @unittest.skipUnless(shutil.which("c++"), "C++ compiler required")
    def test_compiled_selection_cannot_hide_wrong_profile_binding(self):
        header=(Path(__file__).resolve().parents[1]/"plugin/runtime.h").read_text()
        # Independent literal subset is sufficient to catch the review's exact mutation.
        report={"runtime":"1.7.104.0", "entries":{
            "firstUpdate":{"rva":0x8F47A0,"prefix":"488bc45556574154415541564157488d"}},
            "vtables":{}, "calls":{}}
        module.verify_compiled_profile(report,"c++")
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"runtime.h"
            path.write_text(header.replace("steam17104::firstUpdate", "steam17104::update"))
            with self.assertRaisesRegex(ValueError,"Compiled runtime profile"):
                module.verify_compiled_profile(report,"c++",path)

    def test_inspection_selects_version_specific_ids(self):
        self.assertEqual(module.runtime_ids((1,6,1170,0))["messageBox"], 442726)
        self.assertEqual(module.runtime_ids((1,6,640,0))["messageBox"], 52270)
        self.assertEqual(module.runtime_ids((1,5,97,0))["messageBox"], 51421)
        self.assertEqual(module.runtime_ids((1,5,97,0))["bodySceneUpdate"], 39446)
        with self.assertRaises(ValueError): module.runtime_ids((1,6,659,0))
        with self.assertRaises(ValueError): module.runtime_ids((1,7,105,0))

    def test_uses_product_string_when_fixed_version_is_generic(self):
        # Real Steam 1.5.97 uses fixed 1.0.0.0; CommonLib reads ProductVersion.
        _,_,version=module.read_image(image((1,0,0,0),product="1.5.97.0"))
        self.assertEqual(version,(1,5,97,0))

    def test_rejects_missing_or_invalid_product_version(self):
        for product in ("", "1.5.97", "1.5.97.0junk", "1.5.65536.0"):
            with self.subTest(product=product), self.assertRaises(ValueError):
                module.read_image(image(product=product))
        data=image().replace("ProductVersion".encode("utf-16le"),"UnknownVersion".encode("utf-16le"))
        with self.assertRaises(ValueError): module.read_image(data)

    def test_rejects_malformed_version_children(self):
        original=image()
        for key in ("StringFileInfo", "040904b0", "ProductVersion"):
            start=original.index(f"{key}\0".encode("utf-16le"))-6
            for field,value in ((0,0),(0,65535),(2,65535),(4,2)):
                with self.subTest(key=key,field=field,value=value):
                    data=bytearray(original)
                    struct.pack_into("<H",data,start+field,value)
                    with self.assertRaises(ValueError): module.read_image(bytes(data))

    def test_reads_each_target_file_version(self):
        for version in [(1,7,104,0),(1,6,1170,0),(1,6,640,0),(1,5,97,0)]:
            _, read, found = module.read_image(image(version))
            self.assertEqual(found, version)
            with self.assertRaises(ValueError): read(0x1000, 8, executable=True)
    def test_rejects_truncated_and_wrong_architecture(self):
        data=image()
        for size in [0,63,200,0x210,len(data)-1]:
            with self.assertRaises(ValueError): module.read_image(data[:size])
        data=bytearray(data); struct.pack_into("<H",data,0x84,0x14c)
        with self.assertRaises(ValueError): module.read_image(bytes(data))
    def test_rejects_resource_offset_escape_and_bad_signature(self):
        for offset, value in [(0x214,0xfffffff0),(0x260,0x7fffffff),(0x328,0)]:
            data=bytearray(image()); struct.pack_into("<I",data,offset,value)
            with self.assertRaises(ValueError): module.read_image(bytes(data))
    def test_rejects_aliased_resource_directories(self):
        data=bytearray(image())
        # Two version-name entries must not expand the same language directory twice.
        struct.pack_into("<H",data,0x22e,2)
        struct.pack_into("<II",data,0x238,2,0x80000040)
        with self.assertRaisesRegex(ValueError,"Repeated resource directory"):
            module.read_image(bytes(data))

    def test_rejects_versionless_executable(self):
        data=bytearray(image()); struct.pack_into("<II",data,0x118,0,0)
        with self.assertRaises(ValueError): module.read_image(bytes(data))

if __name__ == "__main__": unittest.main()
