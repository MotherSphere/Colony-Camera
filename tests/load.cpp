#include <windows.h>
#include <cassert>
#include <cstring>
#include <initializer_list>
int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    auto module = LoadLibraryW(argv[1]);
    assert(module && "DLL or one of its dependencies could not be loaded");
    auto version = GetProcAddress(module, "SKSEPlugin_Version");
    auto load = reinterpret_cast<bool (*)(const void*)>(GetProcAddress(module, "SKSEPlugin_Load"));
    auto query = reinterpret_cast<bool (*)(const void*, void*)>(GetProcAddress(module, "SKSEPlugin_Query"));
    assert(version && load && query);
    // SKSE reads these fixed ABI fields before any game memory is accessed.
    struct Query { unsigned skse, runtime, editorVersion, editor; };
    struct Info { unsigned infoVersion; const char* name; unsigned version; };
    Info info{};
    Query supported{0x02030010, 0x01070680, 0, 0};
    assert(!query(nullptr, &info) && !query(&supported, nullptr));
    assert(query(&supported, &info));
    assert(info.infoVersion == 1 && info.name && std::strcmp(info.name, "ColonyCamera") == 0);
    supported.editor = 1;
    assert(!query(&supported, &info) && !load(&supported));
    // Real export ABI: each verified candidate accepts its matching SKSE floor.
    for (auto pair : {Query{0x02020060,0x01064920,0,0},
                      Query{0x02020030,0x01062800,0,0},
                      Query{0x02000140,0x01050610,0,0},
                      Query{0x02030010,0x01070680,0,0}}) {
        assert(query(&pair,&info));
        --pair.skse;
        assert(!query(&pair,&info) && !load(&pair));
    }
    for (auto runtime : {0x01062930u,0x010649B0u,0x010404F0u,0x01070690u,0u}) {
        Query unsupported{0xFFFFFFFF, runtime, 0, 0};
        assert(!query(&unsupported, &info) && !load(&unsupported));
    }
    // Metadata must advertise exactly the same targets, not blanket compatibility.
    auto metadata=reinterpret_cast<const unsigned char*>(version);
    unsigned flags=0; std::memcpy(&flags,metadata+0x308,4);
    assert((flags & 3) == 0); // Neither universal Address Library nor signature claim.
    const unsigned expected[]={0x01070680,0x01064920,0x01062800,0x01050610};
    for (unsigned i=0;i<16;++i) {
        unsigned target=0; std::memcpy(&target,metadata+0x30C+4*i,4);
        assert(target==(i<4?expected[i]:0));
    }
    unsigned floor=0; std::memcpy(&floor,metadata+0x34C,4);
    assert(floor==0x02000140);
    supported.editor = 0;
    supported.skse = 0;
    assert(!query(&supported, &info) && !load(&supported));
    assert(!load(nullptr)); // No SKSE interface: refuse without touching game memory.
    FreeLibrary(module);
}
