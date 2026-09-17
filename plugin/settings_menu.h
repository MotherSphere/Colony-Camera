#pragma once
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <functional>
#include <format>
#include <vector>
#include "config_store.h"

namespace settings {
// Skyrim's modal MessageBoxMenu owns input capture, pause and controller focus.
// No rendering detour, Papyrus VM or external SWF is required.
class Menu {
    CameraConfig draft{};
    std::function<void(const CameraConfig&)> apply;
    std::uint64_t generation = 0;
    bool open = false;
    std::string status;
    static constexpr const char* names[] = {"Exploration", "Combat", "Aiming", "Sprinting", "Sneaking", "Swimming", "Airborne"};
    struct Callback final : RE::IMessageBoxCallback {
        std::function<void(std::uint8_t)> action;
        explicit Callback(std::function<void(std::uint8_t)> value) : action(std::move(value)) {}
        void Run(std::uint8_t button) override {
            // Finish the engine's menu callback before queuing the next page.
            SKSE::GetTaskInterface()->AddTask([fn = action, button] { fn(button); });
        }
    };
    void Page(std::string text, std::vector<std::string> labels, std::function<void(std::uint8_t)> action) {
        const auto token = generation;
        RE::BSString message(text.c_str());
        RE::BSTArray<RE::BSString> buttons;
        for (const auto& label : labels) buttons.push_back(RE::BSString(label.c_str()));
        RE::BSTSmartPointer<RE::IMessageBoxCallback> callback(new Callback(
            [this, token, action = std::move(action)](std::uint8_t button) {
                if (open && token == generation) action(button);
            }));
        if (!RE::MessageBoxMenu::Create(message, callback, 0, 0, 10, buttons)) {
            open = false;
            spdlog::warn("Settings menu unavailable; Ctrl+F10 and INI remain available");
        }
    }
    void Notice(std::string text, std::function<void()> back) {
        Page(std::move(text), {"Back"}, [back = std::move(back)](auto) { back(); });
    }
    void CommitDraft() {
        if (!cc_validate_config(&draft)) apply(draft);
    }
    void General() {
        Page(std::format("General\nEffect: {}\nChanges apply immediately. Save writes the INI with a .bak backup.", draft.enabled ? "On" : "Off"),
            {"Toggle effect", "Controls", "Save settings", "Reload saved settings", "Restore defaults", "Back"}, [this](auto b) {
            switch (b) {
            case 0: draft.enabled ^= 1; CommitDraft(); General(); break;
            case 1: Controls(); break;
            case 2: Notice(Write("Data/SKSE/Plugins/ColonyCamera.ini", draft) ? "Settings saved." : "Save failed. Current INI retained; check file permissions and .tmp files.", [this] { General(); }); break;
            case 3: { CameraConfig loaded{}; if (Read("Data/SKSE/Plugins/ColonyCamera.ini", loaded)) { draft = loaded; CommitDraft(); General(); } else Notice("Reload rejected; current settings retained.", [this] { General(); }); break; }
            case 4: draft = cc_defaults(); CommitDraft(); General(); break;
            default: Root(); break;
            }
        });
    }
    void Controls() {
        Page(std::format("Controls (hold Ctrl)\nToggle: {}  Shoulder: {}  Reload: {}  Menu: {}\nSKSE keyboard scan codes; original defaults are F8/F9/F10 and F7 for this menu.",
            draft.keys[0], draft.keys[1], draft.keys[2], draft.menu_key),
            {"Toggle binding", "Shoulder binding", "Reload binding", "Menu binding", "Reset controls", "Back"}, [this](auto b) {
            if (b < 4) Binding(b);
            else if (b == 4) { const auto d = cc_defaults(); std::copy(std::begin(d.keys), std::end(d.keys), draft.keys); draft.menu_key = d.menu_key; CommitDraft(); Controls(); }
            else General();
        });
    }
    void Binding(unsigned index) {
        const auto value = index == 3 ? draft.menu_key : draft.keys[index];
        Page(std::format("Keyboard scan code: {}\nUse +/- to remap. Codes 29/157 (Ctrl) and duplicate bindings are rejected. All shortcuts require Ctrl.", value),
            {"-1", "+1", "-10", "+10", "Default", "Back"}, [this, index](auto b) {
            if (b > 4) { Controls(); return; }
            auto candidate = draft;
            auto& key = index == 3 ? candidate.menu_key : candidate.keys[index];
            const int deltas[] = {-1, 1, -10, 10};
            const auto defaults = cc_defaults();
            if (b == 4) key = index == 3 ? defaults.menu_key : defaults.keys[index];
            else key = static_cast<std::uint32_t>(std::clamp(static_cast<int>(key) + deltas[b], 1, 211));
            if (!cc_validate_config(&candidate)) { draft = candidate; CommitDraft(); }
            Binding(index);
        });
    }
    void Profiles() {
        Page("Third Person\nAiming takes precedence over locomotion. Locomotion overrides are optional; ordinary combat and exploration remain available.",
            {names[0], names[1], names[3], names[4], names[5], names[6], "Back"}, [this](auto b) {
            const unsigned indices[] = {0, 1, 3, 4, 5, 6};
            if (b < 6) Profile(indices[b]); else Root();
        });
    }
    void Profile(unsigned index) {
        const auto& p = draft.profiles[index];
        Page(std::format("{}\nOffset ({:.1f}, {:.1f}, {:.1f}), added depth {:.1f}, FOV delta {:.1f}\nPosition half-life {:.2f}s, maximum lag {:.1f}.",
            names[index], p.offset[0], p.offset[1], p.offset[2], p.zoom, p.fov_offset, p.half_life, p.max_lag),
            {"Offsets", "Motion", "Depth / FOV", "Toggle locomotion override", "Reset profile", "Back"}, [this, index](auto b) {
            if (b < 3) Fields(index, b);
            else if (b == 3) { if (index >= 3) { draft.locomotion_profiles ^= 1u << index; CommitDraft(); }
                Notice(index >= 3 ? ((draft.locomotion_profiles & (1u << index)) ? "Locomotion override enabled." : "Locomotion override disabled.") : "This base profile is always available.", [this, index] { Profile(index); }); }
            else if (b == 4) { draft.profiles[index] = cc_defaults().profiles[index]; CommitDraft(); Profile(index); }
            else if (index == CC_AIM) Root(); else Profiles();
        });
    }
    void Fields(unsigned index, unsigned group) {
        if (group == 0) Page("Offsets\nAdded camera-space X/Y/Z in game units. Offset half-life filters profile and shoulder changes independently from following motion.",
            {"X", "Y", "Z", "Offset half-life", "Back"}, [this, index, group](auto b) {
                const unsigned fields[] = {0, 1, 2, 5}; if (b < 4) Number(index, fields[b], group); else Profile(index); });
        else if (group == 1) Page("Motion\nHalf-life is seconds to halve the remaining error. Zero is immediate. Maximum lag is a distance cap; zero follows native position immediately. Collision correction is always last.",
            {"Position half-life", "Maximum lag", "Back"}, [this, index, group](auto b) { if (b < 2) Number(index, b + 3, group); else Profile(index); });
        else Page("Depth / FOV\nDepth adds a camera-local Y displacement and preserves the native zoom input. FOV delta is relative to the engine's current world FOV. Aiming changes can affect accuracy; defaults keep native aim.",
            {"Added depth", "Depth half-life", "FOV delta", "FOV half-life", "Back"}, [this, index, group](auto b) {
                const unsigned fields[] = {8, 6, 9, 7}; if (b < 4) Number(index, fields[b], group); else Profile(index); });
    }
    static float& Field(CameraProfile& p, unsigned field) {
        switch (field) { case 0: return p.offset[0]; case 1: return p.offset[1]; case 2: return p.offset[2];
        case 3: return p.half_life; case 4: return p.max_lag; case 5: return p.offset_half_life;
        case 6: return p.zoom_half_life; case 7: return p.fov_half_life; case 8: return p.zoom; default: return p.fov_offset; }
    }
    void Number(unsigned index, unsigned field, unsigned group) {
        const char* labels[] = {"X", "Y", "Z", "Position half-life", "Maximum lag", "Offset half-life", "Depth half-life", "FOV half-life", "Added depth", "FOV delta"};
        Page(std::format("{} / {}\nCurrent: {:.3f}\nOut-of-range values are rejected; Default restores this field only.", names[index], labels[field], Field(draft.profiles[index], field)),
            {"Decrease", "Increase", "Decrease x10", "Increase x10", "Default", "Back"}, [this, index, field, group](auto b) {
            if (b > 4) { Fields(index, group); return; }
            auto candidate = draft;
            auto& value = Field(candidate.profiles[index], field);
            const bool half = field == 3 || (field >= 5 && field <= 7);
            const float delta = half ? 0.01f : 1.0f;
            const float multipliers[] = {-1, 1, -10, 10};
            auto defaults = cc_defaults();
            if (b == 4) value = Field(defaults.profiles[index], field); else value += delta * multipliers[b];
            if (!cc_validate_config(&candidate)) { draft = candidate; CommitDraft(); }
            Number(index, field, group);
        });
    }
    void FirstPerson() {
        Page(std::format("First Person\nBody experiment: {}  Alignment: {}\nBody backset: {:.1f}  Sideways: {:.1f}\nNative camera; body arms while sheathed, native hands for weapons or a torch. Alignment preserves body size and foot height. Equipment and shadows still need testing.",
            draft.first_person_enabled ? "Requested" : "Off", draft.body_alignment.alignment_enabled ? "On" : "Off",
            draft.body_alignment.body_backset, draft.body_alignment.body_side),
            {"Toggle body experiment", "Toggle alignment", "Body backset", "Body sideways", "Reset alignment", "Back"}, [this](auto b) {
                if (b == 0) { draft.first_person_enabled ^= 1; CommitDraft(); FirstPerson(); }
                else if (b == 1) { draft.body_alignment.alignment_enabled ^= 1; CommitDraft(); FirstPerson(); }
                else if (b == 2 || b == 3) BodyNumber(b - 2);
                else if (b == 4) { draft.body_alignment = cc_defaults().body_alignment; CommitDraft(); FirstPerson(); }
                else Root();
            });
    }
    void BodyNumber(unsigned field) {
        const auto value = field == 0 ? draft.body_alignment.body_backset : draft.body_alignment.body_side;
        Page(std::format("{}: {:.1f}\n{}\nDistances scale with the body. Alignment must be on. Changes apply after closing the menu; Save settings keeps them for the next session.",
            field == 0 ? "Body backset" : "Body sideways", value,
            field == 0 ? "0..40: higher values move the body farther behind your eye, reducing torso obstruction when looking down."
                       : "-20..20: positive moves the body to your right; negative moves it left."),
            {"Decrease", "Increase", "Decrease x5", "Increase x5", "Default", "Back"}, [this, field](auto b) {
                if (b > 4) { FirstPerson(); return; }
                auto candidate = draft;
                auto& number = field == 0 ? candidate.body_alignment.body_backset : candidate.body_alignment.body_side;
                const float deltas[] = {-1, 1, -5, 5};
                const auto defaults = cc_defaults();
                if (b == 4) number = field == 0 ? defaults.body_alignment.body_backset : defaults.body_alignment.body_side;
                else number += deltas[b];
                if (!cc_validate_config(&candidate)) { draft = candidate; CommitDraft(); }
                BodyNumber(field);
            });
    }
    void Presets() {
        Page("Presets\nOur own .ccpreset format. Export and import use Data/SKSE/Plugins/ColonyCamera/Presets. Import preserves your keyboard bindings. No external preset mapping is provided.",
            {"Balanced", "Responsive", "Native position", "Import", "Export to User preset", "Back"}, [this](auto b) {
            if (b < 3) { auto replacement = cc_defaults();
                if (b == 1) for (auto& p : replacement.profiles) { p.half_life *= 0.5f; p.offset_half_life *= 0.5f; }
                if (b == 2) for (auto& p : replacement.profiles) { p.half_life = 0; p.max_lag = 0; }
                for (unsigned i = 0; i < CC_PROFILE_COUNT; ++i) draft.profiles[i] = replacement.profiles[i];
                draft.locomotion_profiles = replacement.locomotion_profiles; CommitDraft(); Presets();
            } else if (b == 3) Import(0);
            else if (b == 4) Notice(Write("Data/SKSE/Plugins/ColonyCamera/Presets/User.ccpreset", draft) ? "User.ccpreset exported. An earlier version is kept as .bak." : "Export failed; existing preset retained.", [this] { Presets(); });
            else Root();
        });
    }
    void Import(std::size_t page) {
        std::vector<std::filesystem::path> files;
        std::error_code error;
        const std::filesystem::path folder("Data/SKSE/Plugins/ColonyCamera/Presets");
        for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error) && it->path().extension() == ".ccpreset" && files.size() < 128) files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        const auto start = page * 5;
        if (start >= files.size()) { Notice("No presets on this page. Export User first, or copy your own .ccpreset files into the Presets folder.", [this] { Presets(); }); return; }
        const auto count = std::min<std::size_t>(5, files.size() - start);
        std::vector<std::string> labels;
        for (std::size_t i = 0; i < count; ++i) labels.push_back(files[start + i].stem().string());
        labels.push_back("Next page"); labels.push_back("Back");
        Page("Import a Camera Colony preset", std::move(labels), [this, files = std::move(files), start, count, page](auto b) {
            if (b < count) { CameraConfig loaded{};
                if (Read(files[start + b], loaded)) { std::copy(std::begin(draft.keys), std::end(draft.keys), loaded.keys); loaded.menu_key = draft.menu_key; draft = loaded; CommitDraft(); Presets(); }
                else Notice("Invalid or unreadable preset. Current settings retained.", [this] { Presets(); });
            } else if (b == count) Import((start + count < files.size()) ? page + 1 : 0); else Presets();
        });
    }
    void Root() {
        Page("Camera Colony\nSettings apply immediately; General > Save persists them. Native menu navigation supports keyboard and controller.",
            {"General", "Third Person", "First Person", "Aiming", "Presets", "Compatibility", "Diagnostics", "Close"}, [this](auto b) {
            switch (b) { case 0: General(); break; case 1: Profiles(); break; case 2: FirstPerson(); break; case 3: Profile(CC_AIM); break;
            case 4: Presets(); break;
            case 5: Notice("Full camera providers compete for camera state. SmoothCam blocks this plugin; Improved Camera blocks our body experiment. TDM, SkyParkour, skeleton and rendering mod combinations require separate tests.", [this] { Root(); }); break;
            case 6: Notice(status + "\nCPU logs are sampled and do not measure FPS or GPU cost. Visual validation is still required.", [this] { Root(); }); break;
            default: open = false; ++generation; break; }
        });
    }
public:
    bool IsOpen() const { return open; }
    void Cancel() { open = false; ++generation; }
    void Open(const CameraConfig& config, std::function<void(const CameraConfig&)> onApply, std::string diagnostics) {
        if (open) return;
        draft = config; apply = std::move(onApply); status = std::move(diagnostics); open = true; ++generation; Root();
    }
};
}
