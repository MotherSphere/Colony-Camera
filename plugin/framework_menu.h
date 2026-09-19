#pragma once
#include "config_store.h"
#include <SKSE/SKSE.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include "../third_party/skse-menu-framework/SKSEMenuFramework.h"

namespace framework_menu {
inline std::mutex mutex;
inline CameraConfig draft{}, latest{};
inline bool dirty = false, busy = false;
inline std::atomic_bool ready{false};
inline std::function<void(const CameraConfig&)> apply;
inline std::string status;
inline std::vector<std::filesystem::path> presets;
inline constexpr const char* ini = "Data/SKSE/Plugins/ColonyCamera.ini";
inline constexpr const char* folder = "Data/SKSE/Plugins/ColonyCamera/Presets";
inline constexpr const char* names[] = {"Exploration", "Combat", "Aiming", "Sprinting", "Sneaking", "Swimming", "Airborne"};

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
    ImGuiMCP::TextUnformatted("Camera Colony 0.2.10");
    ImGuiMCP::TextUnformatted(status.c_str());
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
    ImGuiMCP::TextUnformatted("Choose Profiles to edit offsets, smoothing, zoom and FOV for each situation.");
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
inline void RefreshPresets() {
    presets.clear();
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == ".ccpreset" && presets.size() < 128)
            presets.push_back(it->path());
    }
    std::sort(presets.begin(), presets.end());
}
inline void __stdcall Presets() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    ImGuiMCP::TextUnformatted("Imports preserve keyboard bindings. Imported settings stay in draft until Apply.");
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
    if (ImGuiMCP::Button("Export draft to User.ccpreset")) {
        status = settings::Write(std::filesystem::path(folder) / "User.ccpreset", draft)
            ? "Preset exported; previous version retained as .bak." : "Export failed; invalid settings or file error.";
        RefreshPresets();
    }
    if (ImGuiMCP::Button("Refresh preset list")) RefreshPresets();
    for (const auto& path : presets) {
        if (ImGuiMCP::Button(path.filename().string().c_str())) {
            CameraConfig value{};
            if (settings::Read(path, value)) {
                std::copy(std::begin(draft.keys), std::end(draft.keys), value.keys);
                value.menu_key = draft.menu_key; draft = value; dirty = true;
                status = "Preset loaded into draft.";
            } else status = "Invalid preset. Draft retained.";
        }
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
        "igInputInt", "igCombo_Str_arr"}) {
        if (!GetProcAddress(module, name)) { spdlog::warn("Menu Framework missing {}; native settings retained", name); return; }
    }
    { std::lock_guard lock(mutex); draft = latest = value; apply = std::move(callback); RefreshPresets(); }
    SKSEMenuFramework::SetSection("Camera Colony");
    SKSEMenuFramework::AddSectionItem("General", General);
    SKSEMenuFramework::AddSectionItem("Third Person", ThirdPerson);
    SKSEMenuFramework::AddSectionItem("First Person", FirstPerson);
    SKSEMenuFramework::AddSectionItem("Profiles", Profiles);
    SKSEMenuFramework::AddSectionItem("Presets", Presets);
    ready = true;
    spdlog::info("Menu Framework: Camera Colony pages registered");
}
}
