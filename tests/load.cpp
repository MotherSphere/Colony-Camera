#include <windows.h>
#include <cassert>
int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    auto module = LoadLibraryW(argv[1]);
    assert(module && "DLL or one of its dependencies could not be loaded");
    auto version = GetProcAddress(module, "SKSEPlugin_Version");
    auto load = reinterpret_cast<bool (*)(const void*)>(GetProcAddress(module, "SKSEPlugin_Load"));
    assert(version && load);
    assert(!load(nullptr)); // No SKSE interface: refuse without touching game memory.
    FreeLibrary(module);
}
