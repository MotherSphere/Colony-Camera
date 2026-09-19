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
    // Do not advertise source-only candidate targets before binary verification.
    for (auto runtime : {0x01064920u, 0x01062800u, 0x01050610u, 0x01070690u}) {
        Query unsupported{0x02030010, runtime, 0, 0};
        assert(!query(&unsupported, &info) && !load(&unsupported));
    }
    supported.editor = 0;
    supported.skse = 0;
    assert(!query(&supported, &info) && !load(&supported));
    assert(!load(nullptr)); // No SKSE interface: refuse without touching game memory.
    FreeLibrary(module);
}
