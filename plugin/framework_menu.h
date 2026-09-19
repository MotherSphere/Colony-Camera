#pragma once
#include "config_store.h"
#include <SKSE/SKSE.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <string_view>
#include <vector>
#include "../third_party/skse-menu-framework/SKSEMenuFramework.h"

namespace framework_menu {
inline std::mutex mutex;
inline CameraConfig draft{}, latest{};
inline bool dirty = false, busy = false;
inline std::atomic_bool ready{false};
inline std::function<void(const CameraConfig&)> apply;
inline std::string status;
inline std::string import_report;
inline bool show_import_report = false;
inline std::vector<std::filesystem::path> presets, smoothcam_presets;
inline constexpr const char* plugins_folder = "Data/SKSE/Plugins";
inline constexpr const char* ini = "Data/SKSE/Plugins/ColonyCamera.ini";
inline constexpr const char* folder = "Data/SKSE/Plugins/ColonyCamera/Presets";
inline constexpr const char* names[] = {"Exploration", "Combat", "Aiming", "Sprinting", "Sneaking", "Swimming", "Airborne"};
inline constexpr const char* groups[] = {"Standing", "Walking", "Running", "Sprinting", "Sneaking", "Swimming", "Bow aim"};
inline constexpr const char* stances[] = {"Neutral", "Melee", "Ranged", "Magic"};
inline constexpr const char* curves[] = {"Linear", "Quadratic in", "Quadratic out", "Quadratic in/out",
    "Cubic in", "Cubic out", "Cubic in/out", "Quartic in", "Quartic out", "Quartic in/out",
    "Quintic in", "Quintic out", "Quintic in/out", "Sine in", "Sine out", "Sine in/out",
    "Circular in", "Circular out", "Circular in/out", "Exponential in", "Exponential out", "Exponential in/out"};
inline int selected_group = 0, selected_stance = 0;

// Main-thread publication never replaces unsaved render-thread edits.
inline void Sync(const CameraConfig& config) {
    std::lock_guard lock(mutex);
    latest = config;
    if (!dirty && !busy) draft = config;
}
inline bool Blocking() {
    return ready && SKSEMenuFramework::IsAnyBlockingWindowOpened();
}
inline void Toggle(const char* label, std::uint32_t& field) {
    bool value = field != 0;
    if (ImGuiMCP::Checkbox(label, &value)) { field = value; dirty = true; }
}
inline void Slider(const char* label, float& value, float low, float high) {
    dirty |= ImGuiMCP::SliderFloat(label, &value, low, high, "%.3f");
}
inline void Wrapped(const std::string& text) {
    ImGuiMCP::PushTextWrapPos();
    ImGuiMCP::TextUnformatted(text.data(), text.data() + text.size());
    ImGuiMCP::PopTextWrapPos();
}
inline void Curve(const char* label, std::uint32_t& field) {
    int value = static_cast<int>(field);
    if (ImGuiMCP::Combo(label, &value, curves, 22)) { field = static_cast<std::uint32_t>(value); dirty = true; }
}
inline void Mask(const char* label, std::uint32_t& field, unsigned bit) {
    bool value = (field & (1u << bit)) != 0;
    if (ImGuiMCP::Checkbox(label, &value)) {
        if (value) field |= 1u << bit; else field &= ~(1u << bit);
        dirty = true;
    }
}
inline AdvancedProfile& AdvancedSelection() {
    ImGuiMCP::Combo("Movement group", &selected_group, groups, 7);
    ImGuiMCP::Combo("Weapon stance", &selected_stance, stances, 4);
    Wrapped("Only ordinary third-person groups listed here are available. Sitting, horseback, dragon, "
        "vampire lord, werewolf, user-defined and vanity states use native fallback; imported values remain stored.");
    return draft.third.profiles[selected_group * 4 + selected_stance];
}
// Called with the UI lock. Work executes later on the game thread, never in ImGui.
inline void Submit(bool save) {
    if (cc_validate_config(&draft)) { status = "Invalid settings or duplicate bindings. Nothing applied."; return; }
    auto* tasks = SKSE::GetTaskInterface();
    if (!tasks) { status = "Game task interface unavailable."; return; }
    const auto value = draft;
    busy = true;
    tasks->AddTask([value, save] {
        apply(value);
        const bool saved = !save || settings::Write(ini, value);
        std::lock_guard lock(mutex);
        latest = draft = value; busy = dirty = false;
        status = saved ? (save ? "Applied and saved." : "Applied. Save to keep settings after restart.")
            : "Applied, but saving failed. Previous INI retained.";
    });
}
inline bool Begin() {
    ImGuiMCP::TextUnformatted("Camera Colony 0.3.0");
    Wrapped(status);
    if (busy) { ImGuiMCP::TextUnformatted("Applying settings..."); return false; }
    ImGuiMCP::TextUnformatted("Edit, then Apply or Apply and save. Ctrl+F7 still opens the native menu.");
    if (ImGuiMCP::Button("Apply")) { Submit(false); return false; }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Apply and save")) { Submit(true); return false; }
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Discard edits")) { draft = latest; dirty = false; }
    ImGuiMCP::Separator();
    return true;
}
inline void __stdcall General() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    Toggle("Enable Camera Colony", draft.enabled);
    ImGuiMCP::TextUnformatted("Keyboard scan codes; hold Ctrl. Duplicate bindings are rejected on Apply.");
    const char* labels[] = {"Toggle effect", "Switch shoulder", "Reload INI", "Native settings menu"};
    for (unsigned i = 0; i < 4; ++i) {
        auto& code = i == 3 ? draft.menu_key : draft.keys[i];
        int value = static_cast<int>(code);
        if (ImGuiMCP::InputInt(labels[i], &value)) { code = static_cast<std::uint32_t>(value); dirty = true; }
    }
    if (ImGuiMCP::Button("Reload saved INI")) {
        CameraConfig value{};
        if (settings::Read(ini, value)) { draft = value; dirty = true; status = "INI loaded into draft. Apply to activate."; }
        else status = "INI unreadable or invalid. Settings retained.";
    }
    if (ImGuiMCP::Button("Restore defaults to draft")) { draft = cc_defaults(); dirty = true; }
}
inline void __stdcall ThirdPerson() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    Toggle("Enable third-person camera", draft.third_person_enabled);
    Toggle("Enable advanced third-person settings (opt-in)", draft.third.enabled);
    Wrapped("With advanced settings disabled, Legacy Profiles controls the camera. With advanced settings enabled, "
        "use Following, Offsets and Aiming. The master and third-person switches must also be enabled.");
    ImGuiMCP::TextUnformatted("Native collision remains active. First-person rendering has its own switch.");
}
inline void __stdcall FirstPerson() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    Toggle("Enable first-person body", draft.first_person_enabled);
    Toggle("Align body", draft.body_alignment.alignment_enabled);
    Slider("Additional backward offset", draft.body_alignment.body_backset, 0, 40);
    Slider("Sideways offset (positive: body-right)", draft.body_alignment.body_side, -20, 20);
    if (ImGuiMCP::Button("Reset alignment")) { draft.body_alignment = cc_defaults().body_alignment; dirty = true; }
    ImGuiMCP::TextUnformatted("The native camera is preserved. Special states and competing body providers use native fallback.");
}
inline void __stdcall Profiles() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    if (draft.third.enabled) {
        Wrapped("Advanced settings are enabled. Edit Following, Offsets and Aiming, or disable advanced settings "
            "on Third Person to use these preserved legacy profiles.");
        return;
    }
    static int selected = 0;
    ImGuiMCP::Combo("Profile", &selected, names, CC_PROFILE_COUNT);
    auto& p = draft.profiles[selected];
    if (selected >= CC_SPRINT) {
        bool enabled = (draft.locomotion_profiles & (1u << selected)) != 0;
        if (ImGuiMCP::Checkbox("Enable locomotion override", &enabled)) {
            if (enabled) draft.locomotion_profiles |= 1u << selected;
            else draft.locomotion_profiles &= ~(1u << selected);
            dirty = true;
        }
    }
    ImGuiMCP::TextUnformatted("Priority: aiming, swimming, sneaking, sprinting, airborne, combat, exploration.");
    Slider("Offset X", p.offset[0], -300, 300);
    Slider("Offset Y", p.offset[1], -300, 300);
    Slider("Offset Z", p.offset[2], -300, 300);
    Slider("Position half-life (seconds)", p.half_life, 0, 1);
    Slider("Maximum lag", p.max_lag, 0, 300);
    Slider("Offset half-life (seconds)", p.offset_half_life, 0, 1);
    Slider("Additional depth", p.zoom, -300, 300);
    Slider("Depth half-life (seconds)", p.zoom_half_life, 0, 1);
    Slider("FOV offset (degrees)", p.fov_offset, -60, 60);
    Slider("FOV half-life (seconds)", p.fov_half_life, 0, 1);
    if (ImGuiMCP::Button("Reset selected profile")) { draft.profiles[selected] = cc_defaults().profiles[selected]; dirty = true; }
}
inline void FollowControls(const char* label, Follow& follow) {
    ImGuiMCP::PushID(label);
    ImGuiMCP::Separator();
    ImGuiMCP::TextUnformatted(label);
    Toggle("Enable following", follow.enabled);
    Curve("Response curve", follow.curve);
    Slider("Minimum follow rate", follow.min_rate, 0, follow.max_rate);
    Slider("Maximum follow rate", follow.max_rate, follow.min_rate, 1);
    Slider("Distance to maximum rate", follow.distance, 0.001f, 100000);
    ImGuiMCP::PopID();
}
inline void __stdcall Following() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    Toggle("Enable advanced third-person settings (opt-in)", draft.third.enabled);
    if (!draft.third.enabled) { Wrapped("Advanced settings are off. Legacy Profiles remain active."); return; }
    auto& p = AdvancedSelection();
    Wrapped("Follow rates are response fractions at 60 Hz: 0 holds, 1 snaps. Minimum must not exceed maximum. "
        "The master and third-person switches must also be enabled. World, local and Z following are "
        "independent approximations of SmoothCam behavior; separate local/Z and focus-anchor semantics "
        "are not exact parity. Review the import report for unsupported controls.");
    FollowControls("World following", p.world);
    FollowControls("Local following", p.local);
    FollowControls("Vertical (Z) following", p.vertical);
}
inline void __stdcall Offsets() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    if (!draft.third.enabled) { Wrapped("Enable advanced settings on Third Person or Following. Legacy Profiles remain active."); return; }
    auto& p = AdvancedSelection();
    auto& third = draft.third;
    Mask("Enable selected movement group (all four stances)", third.group_mask, static_cast<unsigned>(selected_group));
    Wrapped("Disabled groups fall back to Legacy Profiles. X and Z are absolute shoulder offsets; Y is additional depth.");
    Slider("X / sideways offset", p.offset[0], -1000, 1000);
    Slider("Y / additional depth", p.offset[1], -1000, 1000);
    Slider("Z / vertical offset", p.offset[2], -1000, 1000);
    Slider("FOV offset (degrees)", p.fov, -60, 60);
    ImGuiMCP::Separator();
    Wrapped("Transitions and lag clamps below apply to all advanced profiles. Zero duration switches immediately.");
    Curve("Side/up transition curve", third.offset_curve);
    Slider("Side/up transition duration (seconds)", third.offset_duration, 0, 10);
    Curve("Depth transition curve", third.zoom_curve);
    Slider("Depth transition duration (seconds)", third.zoom_duration, 0, 10);
    Curve("FOV transition curve", third.fov_curve);
    Slider("FOV transition duration (seconds)", third.fov_duration, 0, 10);
    const char* axes[] = {"X / sideways lag", "Y / depth lag", "Z / vertical lag"};
    for (unsigned axis = 0; axis < 3; ++axis) {
        ImGuiMCP::PushID(axes[axis]);
        ImGuiMCP::TextUnformatted(axes[axis]);
        Mask("Enable axis clamp", third.clamp_mask, axis);
        Slider("Minimum lag", third.clamp_min[axis], -10000, 0);
        Slider("Maximum lag", third.clamp_max[axis], 0, 10000);
        ImGuiMCP::PopID();
    }
    Toggle("Mirror sideways clamp when switching shoulder", third.mirror_clamp);
}
inline void __stdcall Aiming() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    Wrapped("The native crosshair is retained. 3D crosshair, projectile prediction and projectile correction are not implemented. "
        "Advanced camera offsets can affect aiming accuracy; verify accuracy in game.");
    if (!draft.third.enabled) { Wrapped("Enable advanced settings on Third Person or Following to edit pitch zoom."); return; }
    auto& third = draft.third;
    Wrapped("Pitch zoom applies to all enabled advanced profiles. Bow-aim offsets and following use the shared "
        "Bow aim group and weapon-stance selection on Following and Offsets.");
    Toggle("Enable pitch zoom", third.pitch_enabled);
    Toggle("Apply pitch zoom after following (off: before)", third.pitch_after);
    Curve("Pitch zoom curve", third.pitch_curve);
    Slider("Maximum pitch angle (degrees)", third.pitch_max_angle, 0.001f, 90);
    Slider("Depth at maximum pitch (signed)", third.pitch_zoom, -1000, 1000);
}

inline bool SafePresetName(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == ' ' || name.back() == ' ') return false;
    std::string upper;
    for (const auto ch : name) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '_')) return false;
        upper += ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
    }
    return upper != "CON" && upper != "PRN" && upper != "AUX" && upper != "NUL"
        && !(upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT"))
            && upper.back() >= '0' && upper.back() <= '9');
}
// Draft helpers run under the UI mutex; tests may call them without a renderer.
inline bool ExportPreset(std::string_view name, const std::filesystem::path& directory = folder) {
    if (busy) return false;
    if (!SafePresetName(name)) { status = "Use 1-64 letters, digits, spaces, '-' or '_'; no device names or edge spaces."; return false; }
    const bool success = settings::Write(directory / (std::string(name) + ".ccpreset"), draft);
    status = success ? "Preset exported; previous version retained as .bak."
        : "Export failed; invalid settings or file error. Previous preset retained.";
    return success;
}
inline bool ImportSmoothCam(const std::filesystem::path& path) {
    if (busy) return false;
    show_import_report = false;
    import_report.clear();
    auto fail = [&](const char* message) { import_report = status = message; return false; };
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return fail("Cannot read SmoothCam JSON. Draft retained.");
    const auto size = file.tellg();
    if (size < 0 || size > 512 * 1024) return fail("SmoothCam JSON must be at most 512 KiB. Draft retained.");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(bytes.data(), size) || file.peek() != std::char_traits<char>::eof())
        return fail("SmoothCam JSON read failed or changed size. Draft retained.");
    const auto current = draft;
    CameraConfig value{};
    std::size_t required = 0;
    const auto* input = reinterpret_cast<const unsigned char*>(bytes.data());
    if (cc_import_smoothcam(input, bytes.size(), &current, &value, nullptr, 0, &required) != 4)
        return fail("Import unavailable: invalid draft or JSON encoding. Draft retained.");
    std::string report(required, '\0');
    std::size_t written = 0;
    const auto result = cc_import_smoothcam(input, bytes.size(), &current, &value,
        reinterpret_cast<unsigned char*>(report.data()), report.size(), &written);
    if (written > report.size()) return fail("Import report size changed. Draft retained.");
    report.resize(written);
    import_report = path.string() + "\n" + report;
    if (result != 0 || cc_validate_config(&value)) {
        status = "SmoothCam import rejected. Draft retained; see the full report below.";
        return false;
    }
    draft = value; dirty = true;
    status = "SmoothCam settings loaded into draft only. Review the full report, then Apply to activate.";
    return true;
}
inline void RefreshPresets(const std::filesystem::path& plugins = plugins_folder) {
    presets.clear(); smoothcam_presets.clear();
    const auto own = plugins / "ColonyCamera/Presets";
    // Nonrecursive discovery only. Contents are read only by an explicit import.
    for (const auto& directory : {plugins, own, plugins / "SmoothCam/Presets"}) {
        std::error_code error;
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
            if (!std::filesystem::is_regular_file(it->symlink_status(error))) continue;
            auto name = it->path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : static_cast<char>(ch);
            });
            if (directory == own && name.ends_with(".ccpreset") && presets.size() < 128) presets.push_back(it->path());
            if (name.ends_with(".json") && (directory != plugins || name == "smoothcam.json"
                || name.starts_with("smoothcampreset")) && smoothcam_presets.size() < 128)
                smoothcam_presets.push_back(it->path());
        }
    }
    std::sort(presets.begin(), presets.end());
    std::sort(smoothcam_presets.begin(), smoothcam_presets.end());
}
inline void __stdcall Presets() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    Wrapped("Imports preserve keyboard bindings and stay in draft until Apply. SmoothCam imports also preserve "
        "the master switch, first-person settings and legacy profiles. Compatibility is approximate.");
    ImGuiMCP::TextUnformatted("Legacy profile presets (used with advanced settings disabled):");
    for (unsigned i = 0; i < 3; ++i) {
        const char* labels[] = {"Balanced", "Responsive", "Native position"};
        if (ImGuiMCP::Button(labels[i])) {
            auto defaults = cc_defaults();
            for (auto& p : defaults.profiles) {
                if (i == 1) { p.half_life *= 0.5f; p.offset_half_life *= 0.5f; }
                if (i == 2) { p.half_life = 0; p.max_lag = 0; }
            }
            std::copy(std::begin(defaults.profiles), std::end(defaults.profiles), draft.profiles);
            draft.locomotion_profiles = defaults.locomotion_profiles; dirty = true;
        }
    }
    static char export_name[65] = "User";
    ImGuiMCP::InputText("Export name (without extension)", export_name, sizeof(export_name));
    if (ImGuiMCP::Button("Export draft as Colony preset") && ExportPreset(export_name)) RefreshPresets();
    Wrapped("Exports use ColonyCamera/Presets/<name>.ccpreset. Use letters, digits, spaces, '-' or '_'. "
        "Exports include advanced settings and retain the preceding file as .bak.");
    if (ImGuiMCP::Button("Refresh preset lists")) RefreshPresets();
    ImGuiMCP::TextUnformatted("Colony presets:");
    for (const auto& path : presets) {
        ImGuiMCP::PushID(static_cast<const void*>(&path));
        if (ImGuiMCP::Button(path.string().c_str())) {
            CameraConfig value{};
            if (settings::Read(path, value)) {
                std::copy(std::begin(draft.keys), std::end(draft.keys), value.keys);
                value.menu_key = draft.menu_key; draft = value; dirty = true;
                status = "Preset loaded into draft.";
            } else status = "Invalid preset. Draft retained.";
        }
        ImGuiMCP::PopID();
    }
    ImGuiMCP::Separator();
    Wrapped("SmoothCam JSON: SmoothCam.json or SmoothCamPreset*.json in Data/SKSE/Plugins, or JSON files "
        "in ColonyCamera/Presets and SmoothCam/Presets. Lists refresh only on request (up to 128 entries each). "
        "Select a file to parse it; no source file is modified.");
    Wrapped("Compatibility is approximate. No 3D crosshair, projectile prediction/correction, dialogue-camera "
        "or special-state support. Review the full report for details.");
    for (const auto& path : smoothcam_presets) {
        ImGuiMCP::PushID(static_cast<const void*>(&path));
        if (ImGuiMCP::Button(path.string().c_str())) ImportSmoothCam(path);
        ImGuiMCP::PopID();
    }
    if (!import_report.empty()) {
        ImGuiMCP::Separator();
        if (ImGuiMCP::Button(show_import_report ? "Hide full compatibility report" : "Show full compatibility report"))
            show_import_report = !show_import_report;
        if (show_import_report) Wrapped(import_report);
    }
}
inline void Register(const CameraConfig& value, std::function<void(const CameraConfig&)> callback) {
    if (ready) return;
    // IsInstalled only checks a file. Require the loaded DLL and every used export
    // before entering upstream wrappers, which can call unchecked function pointers.
    auto module = GetModuleHandleW(L"SKSEMenuFramework.dll");
    if (!module) { spdlog::info("Menu Framework not loaded; native settings retained"); return; }
    for (const auto* name : {"AddSectionItem", "IsAnyBlockingWindowOpened",
        "igTextUnformatted", "igButton", "igSameLine", "igSeparator", "igCheckbox", "igSliderFloat",
        "igInputInt", "igCombo_Str_arr", "igInputText", "igPushTextWrapPos", "igPopTextWrapPos",
        "igPushID_Str", "igPushID_Ptr", "igPopID"}) {
        if (!GetProcAddress(module, name)) { spdlog::warn("Menu Framework missing {}; native settings retained", name); return; }
    }
    { std::lock_guard lock(mutex); draft = latest = value; apply = std::move(callback); RefreshPresets(); }
    SKSEMenuFramework::SetSection("Camera Colony");
    SKSEMenuFramework::AddSectionItem("General", General);
    SKSEMenuFramework::AddSectionItem("Third Person", ThirdPerson);
    SKSEMenuFramework::AddSectionItem("First Person", FirstPerson);
    SKSEMenuFramework::AddSectionItem("Legacy Profiles", Profiles);
    SKSEMenuFramework::AddSectionItem("Following", Following);
    SKSEMenuFramework::AddSectionItem("Offsets", Offsets);
    SKSEMenuFramework::AddSectionItem("Aiming", Aiming);
    SKSEMenuFramework::AddSectionItem("Presets", Presets);
    ready = true;
    spdlog::info("Menu Framework: Camera Colony pages registered");
}
}
