#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include "core.h"
#include "runtime.h"
#include "scene.h"
#include <Windows.h>

namespace {
using Clock = std::chrono::steady_clock;
CameraConfig config = cc_defaults();
CameraState state{};
std::atomic_uint commands{0};
std::atomic_uint bindings[3]{66, 67, 68};
std::atomic_bool resetRequested{true};
bool leftShoulder = false;
bool hooksInstalled = false;
bool reportedFrame = false;
bool reportedMovement = false;
RE::ThirdPersonState* appliedTo = nullptr;
RE::TESObjectCELL* lastCell = nullptr;
Clock::time_point lastTick{};
RE::Setting* shoulderSettings[2]{};
using UpdateFn = void (*)(RE::ThirdPersonState*, RE::BSTSmartPointer<RE::TESCameraState>&);
using BeginFn = void (*)(RE::ThirdPersonState*);
REL::Relocation<UpdateFn> originalUpdate;
REL::Relocation<BeginFn> originalBegin;
REL::Relocation<BeginFn> originalEnd;

void LoadConfig() {
    std::ifstream file("Data/SKSE/Plugins/ColonyCamera.ini", std::ios::binary | std::ios::ate);
    if (!file) { spdlog::warn("Configuration unavailable; retaining current settings"); return; }
    const auto size = file.tellg();
    if (size < 0 || size > 65536) { spdlog::error("Configuration too large or unreadable"); return; }
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(text.data(), size)) { spdlog::error("Configuration read failed"); return; }
    CameraConfig replacement{};
    if (cc_parse_config(reinterpret_cast<const unsigned char*>(text.data()), text.size(), &replacement)) {
        spdlog::error("Invalid configuration; retaining current settings (unknown/duplicate key, range or encoding)");
        return;
    }
    config = replacement;
    for (int i = 0; i < 3; ++i) bindings[i].store(config.keys[i]);
    resetRequested = true;
    spdlog::info("Configuration loaded; enabled={}", config.enabled);
}

bool NativeAim(RE::PlayerCharacter* player, RE::PlayerCamera* camera) {
    if (camera->GetRuntimeData2().bowZoomedIn || player->WhoIsCasting() != 0) return true;
    if (!player->IsWeaponDrawn()) return false;
    auto* item = player->GetEquippedObject(false);
    auto* weapon = item ? item->As<RE::TESObjectWEAP>() : nullptr;
    return weapon && (weapon->IsBow() || weapon->IsCrossbow());
}

void RestoreNative(RE::ThirdPersonState* self) {
    if (appliedTo == self && self->translation.x == state.position[0]
        && self->translation.y == state.position[1] && self->translation.z == state.position[2]) {
        self->translation = {state.native[0], state.native[1], state.native[2]};
    }
    appliedTo = nullptr;
}
void End(RE::ThirdPersonState* self) {
    RestoreNative(self);
    state = {};
    lastTick = {};
    originalEnd(self);
}
void Begin(RE::ThirdPersonState* self) {
    RestoreNative(self);
    state = {};
    lastCell = nullptr;
    lastTick = {};
    originalBegin(self);
}

void Update(RE::ThirdPersonState* self, RE::BSTSmartPointer<RE::TESCameraState>& next) {
    static bool entered = false;
    if (!entered) { spdlog::info("Third-person Update callback reached"); entered = true; }
    // Feed the native solver its own previous result, not our filtered output.
    // Restore only an output that is still ours; never overwrite an engine/load change.
    RestoreNative(self);
    if (resetRequested.load()) { state = {}; reportedFrame = false; reportedMovement = false; }
    const auto pending = commands.exchange(0);
    if (pending & 4) LoadConfig();
    if (pending & 1) { config.enabled = !config.enabled; resetRequested = true; spdlog::info("Enabled={}", config.enabled); }
    if (pending & 2) { leftShoulder = !leftShoulder; spdlog::info("Left shoulder={}", leftShoulder); }
    auto* camera = RE::PlayerCamera::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ui = RE::UI::GetSingleton();
    const bool eligible = config.enabled && self->id == RE::CameraState::kThirdPerson && camera && player && ui && self->camera == camera
        && camera->currentState.get() == self && camera->cameraTarget.get().get() == player
        && self->IsInputEventHandlingEnabled()
        && !player->IsInKillMove() && RE::ControlMap::GetSingleton()
        && RE::ControlMap::GetSingleton()->IsMovementControlsEnabled()
        && !ui->GameIsPaused() && !ui->IsMenuOpen("Dialogue Menu") && !ui->IsMenuOpen("Loading Menu")
        && !ui->IsMenuOpen("Console");
    const bool aim = eligible && NativeAim(player, camera);
    // Mirror native shoulder offsets only during this native update, restoring both values.
    float saved[2]{};
    const bool mirror = eligible && !aim && leftShoulder && shoulderSettings[0] && shoulderSettings[1];
    if (mirror) {
        for (int i=0; i<2; ++i) { saved[i] = shoulderSettings[i]->data.f; shoulderSettings[i]->data.f = -saved[i]; }
    }
    originalUpdate(self, next);
    if (mirror) for (int i=0; i<2; ++i) shoulderSettings[i]->data.f = saved[i];
    if (!eligible || camera->currentState.get() != self || (next && next.get() != self)) {
        state = {}; lastTick = {}; return;
    }
    const auto now = Clock::now();
    const float dt = lastTick == Clock::time_point{} ? 0.0f : std::chrono::duration<float>(now-lastTick).count();
    lastTick = now;
    auto* cell = player->GetParentCell();
    const bool reset = resetRequested.exchange(false) || lastCell != cell;
    lastCell = cell;
    auto profile = config.profiles[aim ? 2 : (player->IsWeaponDrawn() ? 1 : 0)];
    if (leftShoulder && !aim) profile.offset[0] = -profile.offset[0];
    if ((profile.half_life == 0.0f || profile.max_lag == 0.0f)
        && profile.offset[0] == 0.0f && profile.offset[1] == 0.0f && profile.offset[2] == 0.0f) {
        // Default aiming really is native: not even an extra collision query.
        state = {}; return;
    }
    auto* root = camera->cameraRoot.get();
    // A parented root belongs to a different camera rig; do not reinterpret its local coordinates.
    RE::NiCamera* rendered = nullptr;
    if (root && !root->parent) {
        for (const auto& child : root->GetChildren()) {
            if (child && (rendered = netimmerse_cast<RE::NiCamera*>(child.get()))) break;
        }
    }
    if (!rendered) {
        static bool warned = false;
        if (!warned) { spdlog::warn("Camera render node missing or root parented; retaining native camera"); warned = true; }
        state = {}; return;
    }
    const auto native = self->translation;
    if (!std::isfinite(native.x) || !std::isfinite(native.y) || !std::isfinite(native.z)) {
        state = {}; return;
    }
    const auto q = self->rotation;
    CameraFrame frame{{native.x, native.y, native.z}, {q.w, q.x, q.y, q.z}, dt, reset ? 1u : 0u};
    auto candidate = cc_step(state, frame, profile);
    if (!candidate.initialized) { state = {}; return; }
    RE::NiPoint3 position{candidate.position[0], candidate.position[1], candidate.position[2]};
    // Native collision is deliberately last. Never interpolate away from its correction.
    camera->CheckCameraCollision(position, true);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        state = {}; return;
    }
    candidate.position[0] = position.x; candidate.position[1] = position.y; candidate.position[2] = position.z;
    state = candidate;
    scene::PublishPosition(self->translation, root->local.translate, root->world.translate,
        rendered->world.translate, position);
    // Refresh the actual render camera's projection after changing its world position.
    static REL::Relocation<void (*)(RE::NiCamera*)> updateMatrix{RELOCATION_ID(69271, 70641)};
    updateMatrix(rendered);
    appliedTo = self;
    const float dx = position.x-native.x, dy = position.y-native.y, dz = position.z-native.z;
    if (!reportedMovement && dx*dx+dy*dy+dz*dz > 0.01f) {
        spdlog::info("Visible camera displacement applied: ({},{},{})", dx, dy, dz);
        reportedMovement = true;
    }
    if (!reportedFrame) {
        spdlog::info("Camera frame applied; native=({},{},{}) output=({},{},{}) half_life={} max_lag={}",
            native.x, native.y, native.z, position.x, position.y, position.z, profile.half_life, profile.max_lag);
        reportedFrame = true;
    }
}

class Input final : public RE::BSTEventSink<RE::InputEvent*> {
    bool ctrlLeft = false, ctrlRight = false;
public:
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override {
        if (!events) return RE::BSEventNotifyControl::kContinue;
        for (auto* e = *events; e; e = e->next) {
            if (e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
            auto* button = e->AsButtonEvent();
            if (!button) continue;
            const auto key = button->GetIDCode();
            if (key == 29) ctrlLeft = button->IsPressed();
            if (key == 157) ctrlRight = button->IsPressed();
            auto* ui = RE::UI::GetSingleton();
            if (!(ctrlLeft || ctrlRight) || !button->IsDown() || !ui || ui->GameIsPaused() || ui->IsMenuOpen("Console")) continue;
            for (int i=0; i<3; ++i) if (key == bindings[i].load()) commands.fetch_or(1u << i);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
Input input;

bool Executable(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory))) return false;
    return memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        && (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
void LogEntry(const char* name, std::uintptr_t address) {
    HMODULE module{};
    char path[MAX_PATH]{};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &module)) GetModuleFileNameA(module, path, MAX_PATH);
    spdlog::info("{} chain entry=0x{:x} module={}", name, address, path[0] ? path : "executable trampoline");
}
void InstallHooks() {
    if (hooksInstalled) return;
    if (GetModuleHandleW(L"SmoothCam.dll")) {
        spdlog::error("SmoothCam.dll is loaded; disable SmoothCam before testing Colony Camera"); return;
    }
    auto* camera = RE::PlayerCamera::GetSingleton();
    auto* third = camera ? camera->cameraStates[RE::CameraState::kThirdPerson].get() : nullptr;
    if (!third) { spdlog::error("Third-person camera state unavailable; hooks not installed"); return; }
    REL::Relocation<std::uintptr_t> vtable{*reinterpret_cast<std::uintptr_t*>(third)};
    const auto* slots = reinterpret_cast<const std::uintptr_t*>(vtable.address());
    for (int i = 1; i <= 3; ++i) {
        if (!Executable(slots[i])) { spdlog::error("Non-executable camera slot {}; hooks not installed", i); return; }
    }
    LogEntry("Begin", slots[1]); LogEntry("End", slots[2]); LogEntry("Update", slots[3]);
    // Save the existing chain, including other SKSE plugins. Never replace it with a vanilla address.
    originalBegin = vtable.write_vfunc(1, Begin);
    originalEnd = vtable.write_vfunc(2, End);
    originalUpdate = vtable.write_vfunc(3, Update);
    hooksInstalled = true;
    spdlog::info("Camera hooks installed after game load; ImprovedCameraSE={}", GetModuleHandleW(L"ImprovedCameraSE.dll") != nullptr);
}
void Message(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoadGame || message->type == SKSE::MessagingInterface::kNewGame) InstallHooks();
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        auto* settings = RE::INISettingCollection::GetSingleton();
        if (settings) {
            shoulderSettings[0] = settings->GetSetting("fOverShoulderPosX:Camera");
            shoulderSettings[1] = settings->GetSetting("fOverShoulderCombatPosX:Camera");
        }
        for (auto& s : shoulderSettings) if (s && s->GetType() != RE::Setting::Type::kFloat) s = nullptr;
        if (!shoulderSettings[0] || !shoulderSettings[1]) spdlog::warn("Shoulder mirroring unavailable: native settings missing");
        if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) manager->AddEventSink(&input);
        spdlog::info("Data ready; Ctrl+F8 toggle, Ctrl+F9 shoulder, Ctrl+F10 reload (default bindings)");
    }
    if (message->type == SKSE::MessagingInterface::kPreLoadGame || message->type == SKSE::MessagingInterface::kPostLoadGame
        || message->type == SKSE::MessagingInterface::kNewGame) resetRequested = true;
}
}

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = [] {
    SKSE::PluginVersionData info;
    info.PluginVersion({0,1,1,0}); info.PluginName("ColonyCamera"); info.AuthorName("MotherSphere");
    info.CompatibleVersions({REL::Version{1,7,104,0}});
    info.MinimumRequiredXSEVersion({2,3,1,0});
    return info;
}();
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    if (!skse || skse->RuntimeVersion() != REL::Version{1,7,104,0}) return false;
    SKSE::Init(skse);
    try {
        auto directory = SKSE::log::log_directory();
        if (!directory) return false;
        auto logger = std::make_shared<spdlog::logger>("ColonyCamera",
            std::make_shared<spdlog::sinks::basic_file_sink_mt>((*directory / "ColonyCamera.log").string(), true));
        spdlog::set_default_logger(logger); spdlog::flush_on(spdlog::level::info);
        spdlog::info("Colony Camera 0.1.1 alpha; runtime {}", skse->RuntimeVersion().string());
        LoadConfig();
        const auto base = REL::Module::get().base();
        REL::Relocation<std::uintptr_t> collisionAddress{RELOCATION_ID(49899, 50832)};
        REL::Relocation<std::uintptr_t> matrixAddress{RELOCATION_ID(69271, 70641)};
        if (collisionAddress.address() != base + runtime::collision.rva
            || matrixAddress.address() != base + runtime::matrix.rva
            || !Executable(collisionAddress.address()) || !Executable(matrixAddress.address())) {
            spdlog::error("Unexpected runtime camera addresses; initialization refused"); return false;
        }
        // Entry detours are valid: Improved Camera hooks collision through MinHook.
        // Offline verification checks the pristine executable; runtime calls retain these detours.
        LogEntry("Collision", collisionAddress.address());
        if (!SKSE::GetMessagingInterface()->RegisterListener(Message)) return false;
        spdlog::info("Waiting for a saved or new game before installing camera hooks");
        return true;
    } catch (const std::exception& error) {
        spdlog::error("Initialization failed: {}", error.what()); return false;
    }
}
