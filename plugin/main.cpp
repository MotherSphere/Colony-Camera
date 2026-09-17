#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include "core.h"
#include "runtime.h"
#include "scene.h"
#include "timing.h"
#include "first_person.h"
#include "settings_menu.h"
#include "hook_validation.h"
#include <Windows.h>

namespace {
using Clock = std::chrono::steady_clock;
CameraConfig config = cc_defaults();
CameraState state{};
std::atomic_uint commands{0};
std::atomic_uint bindings[3]{66, 67, 68};
std::atomic_uint menuBinding{65};
std::atomic_bool resetRequested{true};
bool leftShoulder = false;
bool hooksInstalled = false;
bool reportedFrame = false;
bool reportedMovement = false;
bool reportedBodyAlignment = false;
RE::BSTSmartPointer<RE::ThirdPersonState> appliedTo;
RE::TESObjectCELL* lastCell = nullptr;
Clock::time_point lastTick{};
RE::Setting* shoulderSettings[2]{};
using UpdateFn = void (*)(RE::ThirdPersonState*, RE::BSTSmartPointer<RE::TESCameraState>&);
using BeginFn = void (*)(RE::ThirdPersonState*);
REL::Relocation<UpdateFn> originalUpdate;
REL::Relocation<BeginFn> originalBegin;
REL::Relocation<BeginFn> originalEnd;
using FirstUpdateFn = void (*)(RE::FirstPersonState*, RE::BSTSmartPointer<RE::TESCameraState>&);
using FirstBeginFn = void (*)(RE::FirstPersonState*);
REL::Relocation<FirstUpdateFn> originalFirstUpdate;
REL::Relocation<FirstBeginFn> originalFirstBegin, originalFirstEnd;
bool firstHooksInstalled = false;
bool firstConflict = false;
bool menuAvailable = false;
CameraCoordinator coordinator{};
CameraDecision decision{};
first_person::Renderer bodyRenderer;
settings::Menu settingsMenu;
RE::NiPointer<RE::NiCamera> fovCamera;
camera::OwnedValue<float> ownedWorldFov;
std::array<camera::OwnedValue<float>, 4> ownedFrustum;
RE::NiPointer<RE::NiNode> positionRoot;
RE::NiPointer<RE::NiCamera> positionCamera;
camera::OwnedValue<RE::NiPoint3> ownedLocal, ownedWorld, ownedRendered;

void RestorePosition() {
    if (positionRoot) { ownedLocal.Restore(positionRoot->local.translate); ownedWorld.Restore(positionRoot->world.translate); }
    if (positionCamera) ownedRendered.Restore(positionCamera->world.translate);
    positionRoot.reset(); positionCamera.reset();
}
void RestoreNative(RE::ThirdPersonState* self);

void RestoreFov() {
    if (auto* camera = RE::PlayerCamera::GetSingleton()) ownedWorldFov.Restore(camera->GetRuntimeData2().worldFOV);
    if (fovCamera) {
        auto& f = fovCamera->GetRuntimeData2().viewFrustum;
        ownedFrustum[0].Restore(f.fLeft); ownedFrustum[1].Restore(f.fRight);
        ownedFrustum[2].Restore(f.fTop); ownedFrustum[3].Restore(f.fBottom);
        fovCamera.reset();
    }
}
void RestoreCamera(bool refresh) {
    auto rendered = fovCamera ? fovCamera : positionCamera;
    RestorePosition(); RestoreFov();
    if (refresh && rendered) {
        static REL::Relocation<void (*)(RE::NiCamera*)> updateMatrix{RELOCATION_ID(69271, 70641)};
        updateMatrix(rendered.get());
    }
}
void ApplyFov(RE::PlayerCamera* camera, RE::NiCamera* rendered, float native, float requested) {
    if (native == requested || !std::isfinite(requested) || native <= 0 || native >= 179) return;
    auto& f = rendered->GetRuntimeData2().viewFrustum;
    const float ratio = std::tan(requested * 0.00872664626f) / std::tan(native * 0.00872664626f);
    if (f.bOrtho || !std::isfinite(ratio) || ratio <= 0) return;
    for (float value : {f.fLeft, f.fRight, f.fTop, f.fBottom}) if (!std::isfinite(value * ratio)) return;
    ownedWorldFov.Write(camera->GetRuntimeData2().worldFOV, requested);
    ownedFrustum[0].Write(f.fLeft, f.fLeft * ratio); ownedFrustum[1].Write(f.fRight, f.fRight * ratio);
    ownedFrustum[2].Write(f.fTop, f.fTop * ratio); ownedFrustum[3].Write(f.fBottom, f.fBottom * ratio);
    fovCamera.reset(rendered);
}
void ApplyConfig(const CameraConfig& replacement) {
    if (cc_validate_config(&replacement)) return;
    bodyRenderer.Reset();
    reportedBodyAlignment = false;
    if (appliedTo) RestoreNative(appliedTo.get());
    RestoreCamera(true);
    config = replacement;
    for (int i = 0; i < 3; ++i) bindings[i].store(config.keys[i]);
    menuBinding = config.menu_key;
    resetRequested = true;
}

timing::Totals timings[4];
void ReportTiming(unsigned mode) {
    auto& t = timings[mode];
    if (!t.samples) return;
    spdlog::info("PERF perspective={} enabled={} samples={} applied={} mean_us: own={:.2f} previous_chain={:.2f} collision={:.2f} rust={:.2f} scene={:.2f} peak_own_us={:.2f} sampled_window={} sampled_own_p50_us={:.2f} p95_us={:.2f} p99_us={:.2f}",
        mode >= 2 ? "first" : "third", mode % 2, t.samples, t.applied, t.own/t.samples, t.native/t.samples,
        t.collision/t.samples, t.math/t.samples, t.scene/t.samples, t.peakOwn,
        t.SampledCount(), t.SampledPercentile(0.50), t.SampledPercentile(0.95), t.SampledPercentile(0.99));
    t = {};
}
struct Probe {
    bool sampled;
    unsigned mode;
    Clock::time_point start;
    timing::Sample result;
    Probe(bool activeCamera, bool enabled, bool first = false) : mode(static_cast<unsigned>(enabled) + (first ? 2u : 0u)) {
        static unsigned frame[2]{};
        sampled = activeCamera && (++frame[first ? 1 : 0] % 16 == 0);
        start = Mark();
    }
    Clock::time_point Mark() const { return sampled ? Clock::now() : Clock::time_point{}; }
    double Elapsed(Clock::time_point since) const {
        return sampled ? std::chrono::duration<double, std::micro>(Clock::now()-since).count() : 0.0;
    }
    ~Probe() {
        if (!sampled) return;
        result.total = Elapsed(start);
        timings[mode].Add(result);
        if (timings[mode].samples >= 64) ReportTiming(mode);
    }
};

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
    ApplyConfig(replacement);
    spdlog::info("Configuration loaded; enabled={}", config.enabled);
}

bool NativeAim(RE::PlayerCharacter* player, RE::PlayerCamera* camera) {
    if (camera->GetRuntimeData2().bowZoomedIn || player->WhoIsCasting() != 0) return true;
    if (!player->IsWeaponDrawn()) return false;
    auto* item = player->GetEquippedObject(false);
    auto* weapon = item ? item->As<RE::TESObjectWEAP>() : nullptr;
    return weapon && (weapon->IsBow() || weapon->IsCrossbow());
}

CameraDecision Coordinate(RE::TESCameraState* current, bool inputEnabled) {
    auto* camera = RE::PlayerCamera::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ui = RE::UI::GetSingleton();
    auto* controls = RE::ControlMap::GetSingleton();
    CameraContext context{CC_NATIVE, 0, config.enabled, config.first_person_enabled,
        firstHooksInstalled && !firstConflict ? 1u : 0u, config.locomotion_profiles};
    if (current && current->id == RE::CameraState::kThirdPerson) context.camera_mode = CC_THIRD_PERSON;
    if (current && current->id == RE::CameraState::kFirstPerson) context.camera_mode = CC_FIRST_PERSON;
    if (current && camera && player && ui && current->camera == camera
        && camera->currentState.get() == current && camera->cameraTarget.get().get() == player) context.flags |= CC_AVAILABLE;
    if (controls && controls->IsMovementControlsEnabled() && inputEnabled) context.flags |= CC_CONTROLS;
    if (!ui || ui->GameIsPaused() || ui->IsMenuOpen("Dialogue Menu") || ui->IsMenuOpen("Loading Menu")
        || ui->IsMenuOpen("Console") || settingsMenu.IsOpen()) context.flags |= CC_MENU;
    if (player && context.enabled && (context.camera_mode == CC_THIRD_PERSON || context.first_person_enabled)) {
        const auto* actor = player->AsActorState();
        if (actor->GetLifeState() != RE::ACTOR_LIFE_STATE::kAlive) context.flags |= CC_DEAD;
        if (actor->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) context.flags |= CC_RAGDOLL;
        if (actor->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) context.flags |= CC_FURNITURE;
        if (player->IsOnMount()) context.flags |= CC_MOUNTED;
        if (player->IsInKillMove()) context.flags |= CC_KILLMOVE;
        if (actor->IsWeaponDrawn()) context.flags |= CC_WEAPON_DRAWN;
        if (actor->IsSprinting()) context.flags |= CC_SPRINTING;
        if (actor->IsSneaking()) context.flags |= CC_SNEAKING;
        if (actor->IsSwimming()) context.flags |= CC_SWIMMING;
        if (player->IsInMidair()) context.flags |= CC_IN_AIR;
        if (camera && NativeAim(player, camera)) context.flags |= CC_AIMING;
        // Ordinary vampire races use a playable morph race. Verify humanoid
        // capabilities instead of rejecting every change from character creation.
        // Unknown transformations retain native behavior without guessing form IDs.
        const auto* race = player->GetRace();
        const auto playable = [](const RE::TESRace* value) { return value && value->data.flags.all(RE::RACE_DATA::Flag::kPlayable); };
        if (!race || !race->data.flags.all(RE::RACE_DATA::Flag::kFaceGenHead)
            || !(playable(race) || playable(race->morphRace))) context.flags |= CC_TRANSFORMED;
        if (context.camera_mode == CC_FIRST_PERSON && (context.flags & (CC_SWIMMING | CC_IN_AIR))) context.flags |= CC_SCRIPTED;
    }
    decision = cc_coordinate(coordinator, context);
    coordinator = decision.next;
    return decision;
}

void ProcessCommands() {
    const auto pending = commands.exchange(0);
    if (pending & 4) LoadConfig();
    if (pending & 1) { for (unsigned mode = 0; mode < 4; ++mode) ReportTiming(mode); auto replacement = config; replacement.enabled ^= 1; ApplyConfig(replacement); spdlog::info("Enabled={}", config.enabled); }
    if (pending & 2) { leftShoulder = !leftShoulder; spdlog::info("Left shoulder={}", leftShoulder); }
    if ((pending & 8) && menuAvailable) {
        bodyRenderer.Reset();
        if (appliedTo) RestoreNative(appliedTo.get());
        RestoreCamera(true); resetRequested = true;
        constexpr const char* owners[] = {"Native", "Third person", "Body experiment"};
        constexpr const char* reasons[] = {"Active", "Disabled", "Camera unavailable", "Menu open", "Controls unavailable",
            "Death/bleedout", "Ragdoll", "Killmove", "Special action or unsupported race", "Native camera state",
            "Body experiment off", "Body hooks unavailable", "Invalid camera context"};
        const auto& alignment = bodyRenderer.GetAlignmentSample();
        const auto alignmentText = alignment.available
            ? std::format("\nLast body sample: scale {:.2f}, horizontal shift ({:.1f}, {:.1f}), eye/head height difference {:.1f}. {}",
                alignment.frame.scale, alignment.result.translation[0], alignment.result.translation[1],
                alignment.result.vertical_error, alignment.result.valid ? "Alignment math accepted." : "Alignment rejected; native fallback.")
            : std::string("\nNo body alignment sample yet. Enable the body experiment and enter first person.");
        settingsMenu.Open(config, ApplyConfig, std::format("Version 0.2.1 candidate\nRuntime 1.7.104.0\nLast owner: {}\nState: {}\nFirst-person hooks: {}  competing provider: {}{}",
            decision.owner < std::size(owners) ? owners[decision.owner] : "Unknown",
            decision.reason < std::size(reasons) ? reasons[decision.reason] : "Unknown", firstHooksInstalled, firstConflict, alignmentText));
    }
}

void RestoreNative(RE::ThirdPersonState* self) {
    if (appliedTo.get() == self && self->translation.x == state.position[0]
        && self->translation.y == state.position[1] && self->translation.z == state.position[2]) {
        self->translation = {state.native[0], state.native[1], state.native[2]};
    }
    appliedTo.reset();
}
void End(RE::ThirdPersonState* self) {
    RestoreNative(self);
    RestorePosition();
    RestoreFov(); bodyRenderer.Reset(); coordinator = {};
    state = {};
    lastTick = {};
    originalEnd(self);
}
void Begin(RE::ThirdPersonState* self) {
    RestoreNative(self);
    RestorePosition();
    RestoreFov(); bodyRenderer.Reset(); coordinator = {};
    state = {};
    lastCell = nullptr;
    lastTick = {};
    originalBegin(self);
}

void Update(RE::ThirdPersonState* self, RE::BSTSmartPointer<RE::TESCameraState>& next) {
    static bool entered = false;
    if (!entered) { spdlog::info("Third-person Update callback reached"); entered = true; }
    ProcessCommands();
    Probe probe(self->id == RE::CameraState::kThirdPerson, config.enabled != 0);
    const auto restoreStamp = probe.Mark();
    // Feed the native solver its own previous result, not our filtered output.
    // Restore only an output that is still ours; never overwrite an engine/load change.
    RestoreNative(self);
    RestorePosition(); RestoreFov(); bodyRenderer.Restore();
    probe.result.scene = probe.Elapsed(restoreStamp);
    if (resetRequested.load()) { state = {}; reportedFrame = false; reportedMovement = false; }
    auto* camera = RE::PlayerCamera::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    const auto before = Coordinate(self, self->IsInputEventHandlingEnabled());
    const bool eligible = before.owner == CC_THIRD_PERSON;
    const bool aim = eligible && before.profile == CC_AIM;
    // Mirror native shoulder offsets only during this native update, restoring both values.
    float saved[2]{};
    const bool mirror = eligible && !aim && leftShoulder && shoulderSettings[0] && shoulderSettings[1];
    if (mirror) {
        for (int i=0; i<2; ++i) { saved[i] = shoulderSettings[i]->data.f; shoulderSettings[i]->data.f = -saved[i]; }
    }
    auto stamp = probe.Mark();
    originalUpdate(self, next);
    probe.result.native = probe.Elapsed(stamp);
    if (mirror) for (int i=0; i<2; ++i) if (shoulderSettings[i]->data.f == -saved[i]) shoulderSettings[i]->data.f = saved[i];
    if (!eligible || camera->currentState.get() != self || (next && next.get() != self)) {
        state = {}; lastTick = {}; return;
    }
    const auto after = Coordinate(self, self->IsInputEventHandlingEnabled());
    if (after.owner != CC_THIRD_PERSON) { state = {}; lastTick = {}; return; }
    const auto now = Clock::now();
    const float dt = lastTick == Clock::time_point{} ? 0.0f : std::chrono::duration<float>(now-lastTick).count();
    lastTick = now;
    auto* cell = player->GetParentCell();
    const bool reset = resetRequested.exchange(false) || before.reset || after.reset || lastCell != cell;
    lastCell = cell;
    auto profile = config.profiles[after.profile];
    if (leftShoulder && after.profile != CC_AIM) profile.offset[0] = -profile.offset[0];
    if ((profile.half_life == 0.0f || profile.max_lag == 0.0f)
        && profile.offset[0] == 0.0f && profile.offset[1] == 0.0f && profile.offset[2] == 0.0f
        && profile.zoom == 0.0f && profile.fov_offset == 0.0f
        && profile.offset_half_life == 0.0f && profile.zoom_half_life == 0.0f && profile.fov_half_life == 0.0f) {
        // Default aiming really is native: not even an extra collision query.
        state = {}; return;
    }
    auto* root = camera->cameraRoot.get();
    RE::NiCamera* rendered = nullptr;
    if (root) {
        for (const auto& child : root->GetChildren()) {
            if (child && (rendered = netimmerse_cast<RE::NiCamera*>(child.get()))) break;
        }
    }
    if (!rendered) {
        static bool warned = false;
        if (!warned) {
            spdlog::warn("NiCamera missing: root={} parent={} child_slots={}",
                static_cast<const void*>(root), static_cast<const void*>(root ? root->parent : nullptr),
                root ? root->GetChildren().capacity() : 0);
            if (root) for (const auto& child : root->GetChildren()) {
                if (child) spdlog::warn("Camera child type={}", child->GetRTTI() ? child->GetRTTI()->GetName() : "no RTTI");
            }
            warned = true;
        }
        state = {}; return;
    }
    const auto native = self->translation;
    if (!std::isfinite(native.x) || !std::isfinite(native.y) || !std::isfinite(native.z)) {
        state = {}; return;
    }
    const auto q = self->rotation;
    const float nativeFov = camera->GetRuntimeData2().worldFOV;
    CameraFrame frame{{native.x, native.y, native.z}, {q.w, q.x, q.y, q.z}, dt, reset ? 1u : 0u, nativeFov};
    stamp = probe.Mark();
    auto candidate = cc_step(state, frame, profile);
    probe.result.math = probe.Elapsed(stamp);
    if (!candidate.initialized) { state = {}; return; }
    RE::NiPoint3 position{candidate.position[0], candidate.position[1], candidate.position[2]};
    // Native collision is deliberately last. Never interpolate away from its correction.
    stamp = probe.Mark();
    camera->CheckCameraCollision(position, true);
    probe.result.collision = probe.Elapsed(stamp);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        state = {}; return;
    }
    candidate.base[0] += position.x - candidate.position[0];
    candidate.base[1] += position.y - candidate.position[1];
    candidate.base[2] += position.z - candidate.position[2];
    candidate.position[0] = position.x; candidate.position[1] = position.y; candidate.position[2] = position.z;
    stamp = probe.Mark();
    auto nativeState = self->translation;
    auto local = root->local.translate;
    auto world = root->world.translate;
    auto renderPosition = rendered->world.translate;
    if (!scene::PublishPosition(nativeState, local, world,
            renderPosition, position, root->parent ? &root->parent->world : nullptr)) {
        static bool warned = false;
        if (!warned) { spdlog::warn("Invalid camera parent transform or position; retaining native camera"); warned = true; }
        state = {}; return;
    }
    // Commit only after every coordinate passed validation. Retain node lifetimes
    // and restore only values still owned by this plugin at the next boundary.
    ownedLocal.Write(root->local.translate, local);
    ownedWorld.Write(root->world.translate, world);
    ownedRendered.Write(rendered->world.translate, renderPosition);
    self->translation = nativeState;
    positionRoot.reset(root); positionCamera.reset(rendered);
    state = candidate;
    ApplyFov(camera, rendered, nativeFov, candidate.fov);
    // Refresh the actual render camera's projection after changing its world position.
    static REL::Relocation<void (*)(RE::NiCamera*)> updateMatrix{RELOCATION_ID(69271, 70641)};
    updateMatrix(rendered);
    probe.result.scene += probe.Elapsed(stamp);
    probe.result.applied = true;
    appliedTo.reset(self);
    const float dx = position.x-native.x, dy = position.y-native.y, dz = position.z-native.z;
    if (!reportedMovement && dx*dx+dy*dy+dz*dz > 0.01f) {
        spdlog::info("Camera displacement published: ({},{},{})", dx, dy, dz);
        reportedMovement = true;
    }
    if (!reportedFrame) {
        spdlog::info("Camera scene: parent={} local=({},{},{}) world=({},{},{})",
            static_cast<const void*>(root->parent), root->local.translate.x, root->local.translate.y,
            root->local.translate.z, root->world.translate.x, root->world.translate.y, root->world.translate.z);
        spdlog::info("Camera frame applied; native=({},{},{}) output=({},{},{}) half_life={} max_lag={}",
            native.x, native.y, native.z, position.x, position.y, position.z, profile.half_life, profile.max_lag);
        reportedFrame = true;
    }
}

void FirstBegin(RE::FirstPersonState* self) {
    if (appliedTo) RestoreNative(appliedTo.get());
    RestorePosition();
    bodyRenderer.Reset(); RestoreFov(); state = {}; coordinator = {}; lastTick = {};
    reportedBodyAlignment = false;
    originalFirstBegin(self);
}
void FirstEnd(RE::FirstPersonState* self) {
    bodyRenderer.Reset(); RestoreFov(); state = {}; coordinator = {}; lastTick = {};
    originalFirstEnd(self);
}
void FirstUpdate(RE::FirstPersonState* self, RE::BSTSmartPointer<RE::TESCameraState>& next) {
    ProcessCommands();
    Probe probe(true, config.enabled != 0, true);
    auto stamp = probe.Mark();
    bodyRenderer.Restore(); RestoreFov();
    probe.result.scene = probe.Elapsed(stamp);
    stamp = probe.Mark();
    originalFirstUpdate(self, next);
    probe.result.native = probe.Elapsed(stamp);
    stamp = probe.Mark();
    const auto current = Coordinate(self, self->IsInputEventHandlingEnabled());
    probe.result.math = probe.Elapsed(stamp);
    if (current.owner != CC_FIRST_PERSON || (next && next.get() != self)) { bodyRenderer.Reset(); return; }
    // TESCamera may publish its root after this state callback. Read the native
    // state's verified translation provider instead of a previous rendered pose.
    if (!self->firstPersonCameraObj) { bodyRenderer.Reset(); return; }
    RE::NiPoint3 nativeEye{};
    self->GetTranslation(nativeEye);
    stamp = probe.Mark();
    const auto status = bodyRenderer.Apply(RE::PlayerCharacter::GetSingleton(), nativeEye,
        config.body_alignment, self->firstPersonCameraObj, probe.sampled);
    const double bodyMath = bodyRenderer.AlignmentMathMicroseconds();
    probe.result.math += bodyMath;
    probe.result.scene += (std::max)(0.0, probe.Elapsed(stamp) - bodyMath);
    probe.result.applied = status == first_person::Status::applied;
    static auto lastStatus = first_person::Status::inactive;
    if (status != lastStatus) {
        spdlog::info("First-person body experiment status={} (scene writes do not establish visual correctness)", static_cast<unsigned>(status));
        lastStatus = status;
    }
    const auto& alignment = bodyRenderer.GetAlignmentSample();
    if (!reportedBodyAlignment && alignment.available) {
        const auto* camera = RE::PlayerCamera::GetSingleton();
        spdlog::info("Body alignment sample: math_valid={} enabled={} scale={:.3f} native_eye=({:.2f},{:.2f},{:.2f}) head=({:.2f},{:.2f},{:.2f}) shift=({:.2f},{:.2f},{:.2f}) height_difference={:.2f} backset={:.1f} sideways={:.1f} world_fov={:.1f}; framing and equipment require visual verification",
            alignment.result.valid, config.body_alignment.alignment_enabled, alignment.frame.scale,
            alignment.frame.camera[0], alignment.frame.camera[1], alignment.frame.camera[2],
            alignment.frame.head[0], alignment.frame.head[1], alignment.frame.head[2],
            alignment.result.translation[0], alignment.result.translation[1], alignment.result.translation[2],
            alignment.result.vertical_error, config.body_alignment.body_backset, config.body_alignment.body_side,
            camera ? camera->GetRuntimeData2().worldFOV : 0.0f);
        reportedBodyAlignment = true;
    }
}

class Input final : public RE::BSTEventSink<RE::InputEvent*> {
    bool ctrlLeft = false, ctrlRight = false;
public:
    void Reset() { ctrlLeft = ctrlRight = false; }
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
            unsigned action = 0;
            for (int i=0; i<3; ++i) if (key == bindings[i].load()) action |= 1u << i;
            if (key == menuBinding.load()) action |= 8;
            if (action) { commands.fetch_or(action); SKSE::GetTaskInterface()->AddTask(ProcessCommands); }
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
    const auto base = REL::Module::get().base();
    if (vtable.address() != base + runtime::thirdVtable || !hooks::Memory(vtable.address(), 4 * sizeof(std::uintptr_t), false)) {
        spdlog::error("Unexpected third-person vtable; hooks not installed"); return;
    }
    const auto* slots = reinterpret_cast<const std::uintptr_t*>(vtable.address());
    for (int i = 1; i <= 3; ++i) {
        if (!hooks::ModuleCode(slots[i])) { spdlog::error("Unknown camera slot {} target; hooks not installed", i); return; }
    }
    LogEntry("Begin", slots[1]); LogEntry("End", slots[2]); LogEntry("Update", slots[3]);
    // Save the existing chain, including other SKSE plugins. Never replace it with a vanilla address.
    originalBegin = vtable.write_vfunc(1, Begin);
    originalEnd = vtable.write_vfunc(2, End);
    originalUpdate = vtable.write_vfunc(3, Update);
    hooksInstalled = true;
    firstConflict = GetModuleHandleW(L"ImprovedCamera.dll") || GetModuleHandleW(L"ImprovedCameraSE.dll");
    auto* first = camera->GetRuntimeData().cameraStates[RE::CameraState::kFirstPerson].get();
    if (!firstConflict && first) {
        REL::Relocation<std::uintptr_t> firstTable{*reinterpret_cast<std::uintptr_t*>(first)};
        const auto* entries = reinterpret_cast<const std::uintptr_t*>(firstTable.address());
        if (firstTable.address() == base + runtime::firstVtable
            && hooks::Memory(firstTable.address(), 6 * sizeof(std::uintptr_t), false)
            && hooks::Entry(runtime::firstBegin, base) && hooks::Entry(runtime::firstEnd, base)
            && hooks::Entry(runtime::firstUpdate, base) && hooks::Entry(runtime::firstTranslation, base)
            && hooks::ModuleCode(entries[1]) && hooks::ModuleCode(entries[2]) && hooks::ModuleCode(entries[3])
            && hooks::ModuleCode(entries[5])) {
            originalFirstBegin = firstTable.write_vfunc(1, FirstBegin);
            originalFirstEnd = firstTable.write_vfunc(2, FirstEnd);
            originalFirstUpdate = firstTable.write_vfunc(3, FirstUpdate);
            firstHooksInstalled = true;
        } else spdlog::error("Unexpected first-person vtable; body experiment disabled");
    }
    if (firstConflict) spdlog::warn("Improved Camera detected; Colony Camera body experiment is disabled");
    spdlog::info("Camera hooks installed after game load; ImprovedCameraSE={}", (GetModuleHandleW(L"ImprovedCamera.dll") != nullptr || GetModuleHandleW(L"ImprovedCameraSE.dll") != nullptr));
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
        || message->type == SKSE::MessagingInterface::kNewGame) {
        if (appliedTo) RestoreNative(appliedTo.get());
        RestorePosition(); bodyRenderer.Reset(); RestoreFov(); coordinator = {}; settingsMenu.Cancel();
        input.Reset(); commands = 0; resetRequested = true;
    }
}
}

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = [] {
    SKSE::PluginVersionData info;
    info.PluginVersion({0,2,1,0}); info.PluginName("ColonyCamera"); info.AuthorName("MotherSphere");
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
        spdlog::info("Camera Colony 0.2.1 candidate; runtime {}", skse->RuntimeVersion().string());
        LoadConfig();
        const auto base = REL::Module::get().base();
        REL::Relocation<std::uintptr_t> collisionAddress{RELOCATION_ID(49899, 50832)};
        REL::Relocation<std::uintptr_t> matrixAddress{RELOCATION_ID(69271, 70641)};
        if (collisionAddress.address() != base + runtime::collision.rva
            || matrixAddress.address() != base + runtime::matrix.rva
            || !hooks::Entry(runtime::collision, base) || !hooks::Entry(runtime::matrix, base)
            || !hooks::Entry(runtime::begin, base) || !hooks::Entry(runtime::end, base) || !hooks::Entry(runtime::update, base)) {
            spdlog::error("Unexpected runtime camera addresses; initialization refused"); return false;
        }
        // Entry detours are valid: Improved Camera hooks collision through MinHook.
        // Offline verification checks the pristine executable; runtime calls retain these detours.
        LogEntry("Collision", collisionAddress.address());
        REL::Relocation<std::uintptr_t> menuAddress{REL::ID(442726)};
        menuAvailable = menuAddress.address() == base + runtime::messageBox.rva && hooks::Entry(runtime::messageBox, base);
        if (!menuAvailable) spdlog::warn("Settings menu entry validation failed; use INI and reload instead");
        if (!SKSE::GetMessagingInterface()->RegisterListener(Message)) return false;
        spdlog::info("Waiting for a saved or new game before installing camera hooks");
        return true;
    } catch (const std::exception& error) {
        spdlog::error("Initialization failed: {}", error.what()); return false;
    }
}
