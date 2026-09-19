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
inline void Wrapped(const std::string& text) {
    ImGuiMCP::PushTextWrapPos();
    ImGuiMCP::TextUnformatted(text.data(), text.data() + text.size());
    ImGuiMCP::PopTextWrapPos();
}
inline void Help(const char* text) {
    if (text && ImGuiMCP::IsItemHovered() && ImGuiMCP::BeginTooltip()) {
        ImGuiMCP::PushTextWrapPos(ImGuiMCP::GetFontSize() * 30.0f);
        ImGuiMCP::TextUnformatted(text);
        ImGuiMCP::PopTextWrapPos();
        ImGuiMCP::EndTooltip();
    }
}
// One aligned form per section; stretch weights follow the framework window size.
inline bool Form(const char* id) {
    if (!ImGuiMCP::BeginTable(id, 2, ImGuiMCP::ImGuiTableFlags_SizingStretchProp
        | ImGuiMCP::ImGuiTableFlags_PadOuterX)) return false;
    ImGuiMCP::TableSetupColumn("Setting", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 0.43f);
    ImGuiMCP::TableSetupColumn("Value", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 0.57f);
    return true;
}
inline void Field(const char* label, const char* help = nullptr) {
    ImGuiMCP::TableNextRow(); ImGuiMCP::TableNextColumn();
    ImGuiMCP::AlignTextToFramePadding();
    ImGuiMCP::TextUnformatted(label); Help(help);
    ImGuiMCP::TableNextColumn();
    ImGuiMCP::PushID(label); ImGuiMCP::SetNextItemWidth(-1.0f);
}
inline void Slider(const char* label, float& value, float low, float high, const char* help = nullptr) {
    Field(label, help);
    dirty |= ImGuiMCP::SliderFloat("##value", &value, low, high, "%.3f");
    Help(help); ImGuiMCP::PopID();
}
inline void Select(const char* label, int& value, const char* const* items, int count, const char* help = nullptr) {
    Field(label, help);
    ImGuiMCP::Combo("##value", &value, items, count);
    Help(help); ImGuiMCP::PopID();
}
inline void Curve(const char* label, std::uint32_t& field) {
    Field(label, "The shape of the response. Linear is uniform; in/out curves change how quickly the effect starts and finishes.");
    int value = static_cast<int>(field);
    if (ImGuiMCP::Combo("##value", &value, curves, 22)) { field = static_cast<std::uint32_t>(value); dirty = true; }
    ImGuiMCP::PopID();
}
inline void Mask(const char* label, std::uint32_t& field, unsigned bit) {
    bool value = (field & (1u << bit)) != 0;
    if (ImGuiMCP::Checkbox(label, &value)) {
        if (value) field |= 1u << bit; else field &= ~(1u << bit);
        dirty = true;
    }
}
inline AdvancedProfile& AdvancedSelection() {
    ImGuiMCP::SeparatorText("Editing this profile");
    if (Form("profile-selection")) {
        Select("Movement", selected_group, groups, 7, "Changes here affect this movement group and weapon stance only. Special camera states remain native.");
        Select("Weapon stance", selected_stance, stances, 4, "Neutral: weapons put away. Melee, ranged and magic: corresponding equipment readied.");
        ImGuiMCP::EndTable();
    }
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
        spdlog::info("Menu Framework settings applied: save={} saved={} advanced={} preset_geometry={}",
            save, saved, value.third.enabled, value.third.preset_geometry);
        status = saved ? (save ? "Applied and saved." : "Applied. Save to keep settings after restart.")
            : "Applied, but saving failed. Previous INI retained.";
    });
}
inline bool Begin() {
    ImGuiMCP::TextUnformatted("Camera Colony 0.2.0");
    ImGuiMCP::SameLine();
    ImGuiMCP::TextUnformatted(busy ? "  Applying..." : dirty ? "  Draft modified" : "  Draft matches active settings");
    if (busy) return false;
    if (ImGuiMCP::Button("Apply")) { Submit(false); return false; }
    Help("Preview these settings in game. They will not survive a restart until saved.");
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Apply and save")) { Submit(true); return false; }
    Help("Apply the draft and save it to ColonyCamera.ini, keeping the preceding file as a backup.");
    ImGuiMCP::SameLine();
    if (ImGuiMCP::Button("Discard edits")) { draft = latest; dirty = false; }
    Help("Return to the last applied settings. Hover a setting label for help; Ctrl-click a slider to enter a number.");
    if (!status.empty()) Wrapped(status);
    ImGuiMCP::Separator();
    return true;
}
inline void __stdcall General() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    ImGuiMCP::SeparatorText("Camera Colony");
    Toggle("Enable camera effects", draft.enabled);
    Help("Master switch for both perspectives. Each perspective also has its own switch.");
    if (ImGuiMCP::CollapsingHeader("Keyboard shortcuts")) {
        ImGuiMCP::TextUnformatted("Hold Ctrl with each key. Values are keyboard scan codes.");
        if (Form("bindings")) {
            const char* labels[] = {"Toggle effects", "Switch shoulder", "Reload settings", "Native settings menu"};
            for (unsigned i = 0; i < 4; ++i) {
                auto& code = i == 3 ? draft.menu_key : draft.keys[i];
                Field(labels[i], "Defaults: F8 = 66, F9 = 67, F10 = 68, F7 = 65. Duplicate bindings are rejected.");
                int value = static_cast<int>(code);
                if (ImGuiMCP::InputInt("##value", &value)) { code = static_cast<std::uint32_t>(value); dirty = true; }
                ImGuiMCP::PopID();
            }
            ImGuiMCP::EndTable();
        }
    }
    if (ImGuiMCP::CollapsingHeader("Configuration")) {
        if (ImGuiMCP::Button("Reload saved settings")) {
            CameraConfig value{};
            if (settings::Read(ini, value)) { draft = value; dirty = true; status = "Saved settings loaded into draft."; }
            else status = "INI unreadable or invalid. Settings retained.";
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Reset all settings to defaults")) { draft = cc_defaults(); dirty = true; }
        Help("Loads factory defaults into the draft, including key bindings and first-person settings. Apply to activate.");
    }
}
inline void __stdcall ThirdPerson() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    ImGuiMCP::SeparatorText("Third-person camera");
    Toggle("Enable third-person effects", draft.third_person_enabled);
    Toggle("Use advanced profiles", draft.third.enabled);
    Help("Off: use Legacy Profiles. On: separate movement and weapon profiles, with configurable response curves.");
    if (draft.third.enabled && ImGuiMCP::CollapsingHeader("Distance and height model")) {
        Toggle("Use preset distance and world height", draft.third.preset_geometry);
        Help("Enabled automatically by a new SmoothCam import. Off retains native-relative Colony geometry.");
        if (draft.third.preset_geometry && Form("preset-distance")) {
            Slider("Base distance", draft.third.min_distance, 0, 10000, "Distance behind the follow anchor before profile depth and mouse-wheel zoom.");
            Slider("Zoom scale", draft.third.zoom_scale, 0, 10000, "Distance added per native zoom step. SmoothCam zoomMul.");
            ImGuiMCP::EndTable();
        }
    }
    ImGuiMCP::SeparatorText("Where to adjust your camera");
    Wrapped("Offsets: position, field of view and transitions.\nFollowing: how the camera follows movement and rotation.\nAiming: zoom when looking down.\nPresets: load, import or save a camera setup.");
}
inline void __stdcall FirstPerson() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    ImGuiMCP::SeparatorText("Body visibility");
    Toggle("Show first-person body", draft.first_person_enabled);
    Help("Keeps the native first-person camera and weapon rig. Unsupported actions and competing body providers use native fallback.");
    Toggle("Align body with the camera", draft.body_alignment.alignment_enabled);
    ImGuiMCP::SeparatorText("Body placement");
    if (Form("body-placement")) {
        Slider("Backward offset", draft.body_alignment.body_backset, 0, 40, "Additional distance behind the camera, in body axes at scale one.");
        Slider("Sideways offset", draft.body_alignment.body_side, -20, 20, "Positive moves the body right. The native camera is not moved.");
        ImGuiMCP::EndTable();
    }
    if (ImGuiMCP::Button("Reset body placement")) { draft.body_alignment = cc_defaults().body_alignment; dirty = true; }
}
inline void __stdcall Profiles() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    Wrapped("Used when advanced profiles are off, or a movement group is disabled.");
    static int selected = 0;
    if (Form("legacy-selection")) { Select("Profile", selected, names, CC_PROFILE_COUNT); ImGuiMCP::EndTable(); }
    auto& p = draft.profiles[selected];
    if (selected >= CC_SPRINT) Mask("Use this movement override", draft.locomotion_profiles, selected);
    if (ImGuiMCP::CollapsingHeader("Camera position", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen) && Form("legacy-position")) {
        Slider("Sideways", p.offset[0], -300, 300);
        Slider("Depth", p.offset[1], -300, 300);
        Slider("Height", p.offset[2], -300, 300);
        Slider("Additional zoom", p.zoom, -300, 300);
        Slider("Field of view offset", p.fov_offset, -60, 60);
        ImGuiMCP::EndTable();
    }
    if (ImGuiMCP::CollapsingHeader("Following") && Form("legacy-following")) {
        Slider("Position half-life (s)", p.half_life, 0, 1, "Time to remove half the following error. Zero follows immediately.");
        Slider("Maximum lag", p.max_lag, 0, 300);
        ImGuiMCP::EndTable();
    }
    if (ImGuiMCP::CollapsingHeader("Transitions") && Form("legacy-transitions")) {
        Slider("Offset half-life (s)", p.offset_half_life, 0, 1);
        Slider("Depth half-life (s)", p.zoom_half_life, 0, 1);
        Slider("FOV half-life (s)", p.fov_half_life, 0, 1);
        ImGuiMCP::EndTable();
    }
    if (ImGuiMCP::Button("Reset this legacy profile")) { draft.profiles[selected] = cc_defaults().profiles[selected]; dirty = true; }
}
inline void FollowControls(const char* label, Follow& follow) {
    if (!ImGuiMCP::BeginTabItem(label)) return;
    Toggle("Enable smoothing", follow.enabled);
    Help("Disabled follows immediately. Rates are response fractions at 60 Hz: zero holds and one snaps.");
    if (Form(label)) {
        Curve("Response curve", follow.curve);
        Slider("Minimum response", follow.min_rate, 0, follow.max_rate, "Response to small following errors. Larger values catch up faster.");
        Slider("Maximum response", follow.max_rate, follow.min_rate, 1, "Response at the maximum smoothing distance. Larger values catch up faster.");
        Slider("Smoothing distance", follow.distance, 0.001f, 100000, "Following error at which the maximum response is reached, in game units. Ctrl-click to enter an exact value.");
        ImGuiMCP::EndTable();
    }
    ImGuiMCP::EndTabItem();
}
inline void __stdcall Following() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    if (!draft.third.enabled) { Wrapped("Enable advanced profiles on Third Person to use these controls."); return; }
    auto& p = AdvancedSelection();
    ImGuiMCP::SeparatorText("Camera following");
    if (ImGuiMCP::BeginTabBar("following-tabs")) {
        FollowControls("Movement", p.world);
        FollowControls("Rotation / orbit", p.local);
        FollowControls("Vertical", p.vertical);
        ImGuiMCP::EndTabBar();
    }
}
inline void __stdcall Offsets() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    if (!draft.third.enabled) { Wrapped("Enable advanced profiles on Third Person to use these controls."); return; }
    auto& third = draft.third;
    if (ImGuiMCP::BeginTabBar("offset-tabs")) {
        if (ImGuiMCP::BeginTabItem("Position")) {
            auto& p = AdvancedSelection();
            Mask("Use this movement group", third.group_mask, static_cast<unsigned>(selected_group));
            Help("Applies to all four weapon stances. Disabled groups fall back to Legacy Profiles.");
            ImGuiMCP::SeparatorText("Camera position");
            if (Form("position")) {
                Slider("Sideways (X)", p.offset[0], -1000, 1000, "Absolute shoulder position in game units. Negative is left, positive is right.");
                Slider("Depth (Y)", p.offset[1], -1000, 1000, "Added to the selected distance model. Positive moves forward, negative moves backward.");
                Slider("Height (Z)", p.offset[2], -1000, 1000, "Preset model: world-vertical height above the follow anchor. Native model: camera-local shoulder height.");
                Slider("Field of view offset", p.fov, -60, 60, "Degrees added to the native field of view. Zero keeps the native FOV.");
                ImGuiMCP::EndTable();
            }
            ImGuiMCP::EndTabItem();
        }
        if (ImGuiMCP::BeginTabItem("Transitions")) {
            ImGuiMCP::SeparatorText("Global settings - all advanced profiles");
            if (ImGuiMCP::CollapsingHeader("Sideways and height", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen) && Form("offset-transition")) {
                Curve("Curve", third.offset_curve);
                Slider("Duration (s)", third.offset_duration, 0, 10, "Time to reach a new offset when the profile or shoulder changes. Zero switches immediately.");
                ImGuiMCP::EndTable();
            }
            if (ImGuiMCP::CollapsingHeader("Depth / zoom", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen) && Form("zoom-transition")) {
                Curve("Curve", third.zoom_curve); Slider("Duration (s)", third.zoom_duration, 0, 10);
                ImGuiMCP::EndTable();
            }
            if (ImGuiMCP::CollapsingHeader("Field of view", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen) && Form("fov-transition")) {
                Curve("Curve", third.fov_curve); Slider("Duration (s)", third.fov_duration, 0, 10);
                ImGuiMCP::EndTable();
            }
            ImGuiMCP::EndTabItem();
        }
        if (ImGuiMCP::BeginTabItem("Advanced limits")) {
            ImGuiMCP::SeparatorText("Global settings - all advanced profiles");
            const char* axes[] = {"Sideways lag (X)", "Depth lag (Y)", "Vertical lag (Z)"};
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (!ImGuiMCP::CollapsingHeader(axes[axis])) continue;
                ImGuiMCP::PushID(axes[axis]);
                Mask("Limit this axis", third.clamp_mask, axis);
                Help("Limits following lag in camera axes. This does not limit the configured camera position.");
                if (Form("limits")) {
                    Slider("Minimum lag", third.clamp_min[axis], -10000, 0);
                    Slider("Maximum lag", third.clamp_max[axis], 0, 10000);
                    ImGuiMCP::EndTable();
                }
                ImGuiMCP::PopID();
            }
            Toggle("Mirror sideways limits with shoulder", third.mirror_clamp);
            ImGuiMCP::EndTabItem();
        }
        ImGuiMCP::EndTabBar();
    }
}
inline void __stdcall Aiming() {
    std::lock_guard lock(mutex);
    if (!Begin()) return;
    if (!draft.third.enabled) { Wrapped("Enable advanced profiles on Third Person to use these controls."); return; }
    ImGuiMCP::SeparatorText("Zoom while looking down - all profiles");
    auto& third = draft.third;
    Toggle("Enable pitch zoom", third.pitch_enabled);
    if (Form("pitch-zoom")) {
        Curve("Response curve", third.pitch_curve);
        Slider("Maximum pitch angle", third.pitch_max_angle, 0.001f, 90, "Downward angle in degrees at which the full depth adjustment is reached.");
        Slider("Depth adjustment", third.pitch_zoom, -1000, 1000);
        ImGuiMCP::EndTable();
    }
    if (ImGuiMCP::CollapsingHeader("Advanced")) {
        Toggle("Apply zoom after following", third.pitch_after);
        Help("On: bypass orbit smoothing for pitch zoom. Off: smooth the pitch adjustment with the orbit.");
    }
    ImGuiMCP::Separator();
    Wrapped("Native crosshair; no projectile correction. Check aim after changing camera offsets.");
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
    status = path.filename().string() + " loaded into draft. Apply to activate, or Apply and save to keep it.";
    spdlog::info("SmoothCam import accepted: {} (draft only), preset_geometry={} base={} zoom_scale={}",
        path.string(), value.third.preset_geometry, value.third.min_distance, value.third.zoom_scale);
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
    ImGuiMCP::TextUnformatted("Load a preset, review it, then Apply. Hover controls for help.");
    if (ImGuiMCP::CollapsingHeader("Built-in legacy presets")) {
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
    }
    if (ImGuiMCP::CollapsingHeader("Save a named preset")) {
        static char export_name[65] = "User";
        if (Form("export-preset")) {
            Field("Preset name", "Letters, digits, spaces, hyphens or underscores. No extension needed.");
            ImGuiMCP::InputText("##value", export_name, sizeof(export_name));
            ImGuiMCP::PopID(); ImGuiMCP::EndTable();
        }
        if (ImGuiMCP::Button("Export draft as Colony preset") && ExportPreset(export_name)) RefreshPresets();
        Help("Writes ColonyCamera/Presets/<name>.ccpreset, including advanced settings. The previous file is kept as .bak.");
    }
    if (ImGuiMCP::Button("Refresh preset lists")) RefreshPresets();
    if (ImGuiMCP::CollapsingHeader("Your Colony presets", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
        if (presets.empty()) ImGuiMCP::TextUnformatted("No saved presets yet.");
        for (const auto& path : presets) {
            ImGuiMCP::PushID(path.string().c_str());
            if (ImGuiMCP::Button(path.filename().string().c_str())) {
                CameraConfig value{};
                if (settings::Read(path, value)) {
                    std::copy(std::begin(draft.keys), std::end(draft.keys), value.keys);
                    value.menu_key = draft.menu_key; draft = value; dirty = true;
                    status = "Preset loaded into draft.";
                } else status = "Invalid preset. Draft retained.";
            }
            Help(path.string().c_str()); ImGuiMCP::PopID();
        }
    }
    if (ImGuiMCP::CollapsingHeader("Import from SmoothCam", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
        Help("SmoothCam JSON: SmoothCam.json or SmoothCamPreset*.json in Data/SKSE/Plugins, or JSON files "
            "in ColonyCamera/Presets and SmoothCam/Presets. Lists refresh only on request (up to 128 entries each). "
            "Select a file to parse it; no source file is modified.");
        Wrapped("Approximate conversion. Review the report before applying.");
        Help("Keeps your keys and first-person settings. No 3D crosshair, projectile correction, dialogue or special-state camera support.");
        if (smoothcam_presets.empty()) ImGuiMCP::TextUnformatted("No SmoothCam JSON found. Add a file, then Refresh.");
        for (const auto& path : smoothcam_presets) {
            ImGuiMCP::PushID(path.string().c_str());
            if (ImGuiMCP::Button(path.filename().string().c_str())) ImportSmoothCam(path);
            Help(path.string().c_str()); ImGuiMCP::PopID();
        }
    }
    if (!import_report.empty()) {
        ImGuiMCP::Separator();
        Wrapped(status);
        if (dirty && ImGuiMCP::Button("Apply draft and save")) { Submit(true); return; }
        Help("Applies all current draft settings and saves them. Import alone only prepares the draft.");
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
        "igPushID_Str", "igPopID", "igBeginTable", "igEndTable", "igTableSetupColumn",
        "igTableNextRow", "igTableNextColumn", "igSetNextItemWidth", "igAlignTextToFramePadding",
        "igIsItemHovered", "igBeginTooltip", "igEndTooltip", "igGetFontSize", "igSeparatorText",
        "igCollapsingHeader_TreeNodeFlags", "igBeginTabBar", "igEndTabBar", "igBeginTabItem", "igEndTabItem"}) {
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
