#include <RE/Skyrim.h>
#include "../plugin/framework_menu.h"
#include <cassert>
#include <cstring>

namespace {
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        for (unsigned i = 0; i < 100; ++i) {
            path = std::filesystem::temp_directory_path() / (L"ColonyCamera-menu-test-"
                + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())
                + L"-" + std::to_wstring(i));
            if (std::filesystem::create_directory(path)) return;
        }
        throw std::runtime_error("Cannot reserve test directory");
    }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};
void Put(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    assert(file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
}
bool Same(const CameraConfig& a, const CameraConfig& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
}

int main() {
    using namespace framework_menu;
    const auto defaults = cc_defaults();
    Register(defaults, [](const CameraConfig&) { assert(false && "No GUI edits should apply in this test"); });
    assert(!ready && !Blocking()); // Optional DLL is absent: no import/load failure.
    Sync(defaults);
    assert(draft.enabled == defaults.enabled);
    dirty = true;
    draft.body_alignment.body_backset = 17;
    auto external = defaults;
    external.enabled = 0;
    Sync(external);
    assert(latest.enabled == 0 && draft.body_alignment.body_backset == 17);
    busy = true; dirty = false;
    Sync(defaults);
    assert(draft.body_alignment.body_backset == 17);
    busy = false;
    Sync(defaults);
    assert(draft.body_alignment.body_backset == defaults.body_alignment.body_backset);
    draft.keys[1] = draft.keys[0];
    Submit(true);
    assert(!busy && status.find("Invalid") != std::string::npos);

    for (const auto* name : {"User", "My preset-2", "Camera_01", "COM10"}) assert(SafePresetName(name));
    for (const auto* name : {"", ".", "..", "../escape", "a/b", "a\\b", "C:escape", "name.",
        " name", "name ", "CON", "con", "NUL", "Aux", "PRN", "COM1", "lpt9", "CON.txt",
        "a\nb", "x*y", "preset##id", "CONIN$"}) assert(!SafePresetName(name));
    assert(!SafePresetName(std::string(65, 'a')));

    TemporaryDirectory isolated;
    const auto plugins = isolated.path / "Plugins";
    const auto own = plugins / "ColonyCamera/Presets";
    const auto smooth = plugins / "SmoothCam/Presets";
    Put(plugins / "SmoothCam.json", "not parsed during discovery");
    Put(plugins / "SmoothCamPresetSame.json", "{}");
    Put(plugins / "SmoothCamPresetUpper.JSON", "{}");
    Put(plugins / "unrelated.json", "{}");
    Put(own / "User.ccpreset", "also not parsed during discovery");
    Put(own / "SmoothCamPresetSame.json", "{}");
    Put(smooth / "Named.json", "{}");
    Put(smooth / "Named.json.bak", "{}");
    Put(smooth / "nested/Hidden.json", "{}");
    std::filesystem::create_directory(plugins / "SmoothCamPresetDirectory.json");
    draft = defaults; dirty = false;
    RefreshPresets(plugins);
    assert(presets.size() == 1 && presets.front() == own / "User.ccpreset");
    assert(smoothcam_presets.size() == 5);
    assert(std::is_sorted(smoothcam_presets.begin(), smoothcam_presets.end()));
    assert(Same(draft, defaults) && !dirty);
    Put(smooth / "Added.json", "{}");
    assert(smoothcam_presets.size() == 5); // Discovery is explicit, never a render-frame poll.
    RefreshPresets(plugins);
    assert(smoothcam_presets.size() == 6);
    for (unsigned i = 0; i < 140; ++i) Put(smooth / (std::to_string(i) + ".json"), "{}");
    RefreshPresets(plugins);
    assert(smoothcam_presets.size() == 128);

    assert(!ExportPreset("../escape", own));
    assert(!std::filesystem::exists(plugins / "ColonyCamera/escape.ccpreset"));
    assert(ExportPreset("Named export", own));
    CameraConfig exported{};
    assert(settings::Read(own / "Named export.ccpreset", exported) && Same(exported, draft));

    draft.enabled = 0;
    draft.first_person_enabled = 1;
    draft.body_alignment.body_backset = 17;
    draft.keys[0] = 80; draft.keys[1] = 81; draft.keys[2] = 82; draft.menu_key = 83;
    draft.profiles[0].offset[0] = 27;
    const auto before = draft;
    const auto active = latest;
    const auto json = plugins / "SmoothCam.json";
    Put(json, R"({"name":"GUI import %s", "config":{"standing":{"sideOffset":42},"unknown":{"value":"100%"}}})");
    assert(!show_import_report);
    show_import_report = true;
    assert(ImportSmoothCam(json));
    assert(!show_import_report);
    assert(dirty && !busy && Same(latest, active));
    assert(draft.third.profiles[0].offset[0] == 42);
    auto preserved = draft;
    preserved.third = before.third; preserved.third_person_enabled = before.third_person_enabled;
    assert(Same(preserved, before));
    assert(import_report.find("Supported") != std::string::npos);
    assert(import_report.find("approximate") != std::string::npos);
    assert(import_report.find("Unsupported") != std::string::npos);
    assert(import_report.find("unknown.value") != std::string::npos);
    assert(import_report.find("100%") != std::string::npos);
    const auto imported = draft;
    for (const auto& invalid : {std::string("{broken"), std::string(R"({"standing":{"sideOffset":999999}})"),
        std::string(R"({"standing":{},"standing":{}})"), std::string(512 * 1024 + 1, 'x'),
        std::string(1, static_cast<char>(0xff))}) {
        Put(json, invalid);
        dirty = false;
        show_import_report = true;
        assert(!ImportSmoothCam(json));
        assert(!show_import_report);
        assert(!dirty && Same(draft, imported) && Same(latest, active));
        assert(!import_report.empty());
    }
    assert(!ImportSmoothCam(plugins / "missing.json") && Same(draft, imported));
    dirty = true;
    assert(!ImportSmoothCam(json) && dirty && Same(draft, imported));
    busy = true;
    Put(json, "{}");
    assert(!ImportSmoothCam(json) && Same(draft, imported));
    busy = false;
}
