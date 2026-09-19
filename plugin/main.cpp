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
#include "framework_menu.h"
#include "hook_validation.h"
#include <Windows.h>

namespace {
using Clock = std::chrono::steady_clock;
CameraConfig config = cc_defaults();
CameraState state{};
AdvancedState advancedState{};
std::atomic_uint commands{0};
std::atomic_uint bindings[3]{66, 67, 68};
std::atomic_uint menuBinding{65};
std::atomic_bool resetRequested{true};
bool leftShoulder = false;
bool hooksInstalled = false;
bool reportedFrame = false;
bool reportedMovement = false;
bool reportedBodyAlignment = false;
bool reportedViewMotion = false;
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
using BodySceneUpdateFn = void (*)(RE::NiAVObject*, RE::NiUpdateData*);
REL::Relocation<BodySceneUpdateFn> originalBodySceneUpdate;
using CameraViewUpdateFn = void (*)(RE::TESCamera*);
REL::Relocation<CameraViewUpdateFn> originalCameraViewUpdate;
RE::BSTSmartPointer<RE::TESCameraState> firstPublishedState;
RE::NiPointer<RE::NiCamera> firstPublishedCamera;
RE::TESCameraState* firstUpdatedInView = nullptr;
bool firstHooksInstalled = false;
bool firstConflict = false;
bool menuAvailable = false;
enum class BodyOutcome : unsigned { applied, native_fallback, view_not_ready, rejected };
struct BodyTrace {
    Clock::time_point reported{};
    std::array<std::uint64_t, 4> counts{};
    unsigned lastFallback = 0;
    unsigned lastRejected = 0;
};
std::array<BodyTrace, 2> bodyTraces{};
CameraCoordinator coordinator{};
CameraDecision decision{};
first_person::Renderer bodyRenderer;
settings::Menu settingsMenu;
struct BodyViewSnapshot {
    body_facing::Sample facing{};
    first_person::AlignmentSample alignment{};
    body_position::PublicationSample publication{};
    CameraDecision decision{};
    RE::NiPoint3 viewCorrection{};
    Clock::time_point captured{};
    first_person::Status status = first_person::Status::inactive;
    bool available = false;
    bool viewCorrectionAvailable = false;
};
BodyViewSnapshot lastBodyView;
std::string activeLogPath;
RE::NiPointer<RE::NiCamera> fovCamera;
camera::OwnedValue<float> ownedWorldFov;
std::array<camera::OwnedValue<float>, 4> ownedFrustum;
RE::NiPointer<RE::NiNode> positionRoot;
RE::NiPointer<RE::NiCamera> positionCamera;
camera::OwnedValue<RE::NiPoint3> ownedLocal, ownedWorld, ownedRendered;

void ResetFirstView() {
    firstPublishedState.reset(); firstPublishedCamera.reset(); firstUpdatedInView = nullptr;
}

RE::NiCamera* RenderedCamera(RE::PlayerCamera* camera) {
    auto* root = camera ? camera->cameraRoot.get() : nullptr;
    if (root) for (const auto& child : root->GetChildren())
        if (child) if (auto* rendered = netimmerse_cast<RE::NiCamera*>(child.get())) return rendered;
    return nullptr;
}

// Every publication attempt, including early native fallbacks, contributes to
// this bounded report. A quiet old status=applied line did not establish that
// subsequent attempts had reached the renderer at all.
void TraceBody(unsigned phase, BodyOutcome outcome, unsigned detail = 0,
    const RE::NiCamera* rendered = nullptr) {
    if (!config.enabled || !config.first_person_enabled || !firstHooksInstalled) return;
    auto& trace = bodyTraces[phase];
    ++trace.counts[static_cast<unsigned>(outcome)];
    if (outcome == BodyOutcome::native_fallback) trace.lastFallback = detail;
    if (outcome == BodyOutcome::rejected) trace.lastRejected = detail;
    const auto now = Clock::now();
    if (trace.reported != Clock::time_point{} && now - trace.reported < std::chrono::seconds(2)) return;
    std::array<float, 2> heading{};
    const bool viewAvailable = rendered && body_position::ViewHeading(rendered->world.rotate, heading);
    float pitch = 0.0f;
    if (viewAvailable) {
        const auto& rotation = rendered->world.rotate;
        pitch = std::atan2(rotation.entry[2][0],
            rotation.entry[0][0] * heading[0] + rotation.entry[1][0] * heading[1]) * 57.2957795f;
    }
    const auto& sample = bodyRenderer.GetAlignmentSample();
    const bool sampleAvailable = (outcome == BodyOutcome::applied || outcome == BodyOutcome::rejected)
        && sample.available;
    const auto* player = RE::PlayerCharacter::GetSingleton();
    const auto* body = player ? player->Get3D(false) : nullptr;
    const auto* arms = player ? player->Get3D(true) : nullptr;
    spdlog::info("First-person motion window: phase={} applied={} native_fallback={} view_not_ready={} rejected={} last_outcome={} last_fallback_reason={} last_rejected_status={} view_available={} pitch_deg={:.3f} heading=({:.3f},{:.3f}) body_forward=({:.3f},{:.3f},{:.3f}) alignment_available={} shift=({:.3f},{:.3f}) eye_available={}",
        phase == 0 ? "model" : "camera-view", trace.counts[0], trace.counts[1], trace.counts[2], trace.counts[3],
        static_cast<unsigned>(outcome), trace.lastFallback, trace.lastRejected, viewAvailable, pitch, heading[0], heading[1],
        body ? body->world.rotate.entry[0][1] : 0.0f, body ? body->world.rotate.entry[1][1] : 0.0f,
        body ? body->world.rotate.entry[2][1] : 0.0f, sampleAvailable,
        sampleAvailable ? sample.result.translation[0] : 0.0f,
        sampleAvailable ? sample.result.translation[1] : 0.0f,
        sampleAvailable ? sample.frame.eye_available : 0u);
    if (player) {
        const auto* right = player->GetEquippedObject(false);
        const auto* left = player->GetEquippedObject(true);
        const auto& nativeBiped = player->GetBiped(true);
        const auto& bodyBiped = player->GetBiped(false);
        spdlog::info("First-person equipment: phase={} weapon_state={} policy_available={} last_native_requested={} native_root={} hidden={} scale={:.6f} right_form={:08X} left_form={:08X} native_biped={} body_biped={}",
            phase == 0 ? "model" : "camera-view", static_cast<unsigned>(player->AsActorState()->GetWeaponState()),
            outcome == BodyOutcome::applied, bodyRenderer.UsesNativeArms(), static_cast<const void*>(arms),
            arms && arms->GetAppCulled(), arms ? arms->world.scale : 0.0f,
            right ? right->GetFormID() : 0u, left ? left->GetFormID() : 0u,
            static_cast<const void*>(nativeBiped.get()), static_cast<const void*>(bodyBiped.get()));
        if (nativeBiped) {
            for (unsigned slot = RE::BIPED_OBJECTS::kShield; slot < RE::BIPED_OBJECTS::kTotal; ++slot) {
                if (slot > RE::BIPED_OBJECTS::kShield && slot < RE::BIPED_OBJECTS::kHandToHandMelee) continue;
                const auto* node = nativeBiped->objects[slot].partClone.get();
                if (!node) continue;
                bool shared = false;
                if (bodyBiped) for (const auto& item : bodyBiped->objects) shared |= item.partClone.get() == node;
                spdlog::info("First-person equipment node: slot={} node={} parent={} hidden={} world_scale={:.6f} shared_body_clone={}",
                    slot, static_cast<const void*>(node), static_cast<const void*>(node->parent),
                    node->GetAppCulled(), node->world.scale, shared);
            }
        }
    }
    trace.counts = {};
    trace.reported = now;
}

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
    ResetFirstView();
    bodyTraces = {};
    lastBodyView = {};
    reportedBodyAlignment = false;
    reportedViewMotion = false;
    if (appliedTo) RestoreNative(appliedTo.get());
    RestoreCamera(true);
    config = replacement;
    framework_menu::Sync(config);
    for (int i = 0; i < 3; ++i) bindings[i].store(config.keys[i]);
    menuBinding = config.menu_key;
    resetRequested = true;
}

timing::Totals timings[6];
void ReportTiming(unsigned mode) {
    auto& t = timings[mode];
    if (!t.samples) return;
    constexpr const char* stages[] = {"camera-state", "model", "camera-view"};
    spdlog::info("PERF perspective={} stage={} enabled={} samples={} applied={} mean_us: own={:.2f} previous_chain={:.2f} collision={:.2f} rust={:.2f} scene={:.2f} peak_own_us={:.2f} sampled_window={} sampled_own_p50_us={:.2f} p95_us={:.2f} p99_us={:.2f}",
        mode >= 2 ? "first" : "third", stages[mode / 2], mode % 2, t.samples, t.applied, t.own/t.samples, t.native/t.samples,
        t.collision/t.samples, t.math/t.samples, t.scene/t.samples, t.peakOwn,
        t.SampledCount(), t.SampledPercentile(0.50), t.SampledPercentile(0.95), t.SampledPercentile(0.99));
    t = {};
}
struct Probe {
    bool sampled;
    unsigned mode;
    Clock::time_point start;
    timing::Sample result;
    Probe(bool activeCamera, bool enabled, unsigned stage = 0) : mode(static_cast<unsigned>(enabled) + stage * 2u) {
        static unsigned frame[3]{};
        sampled = activeCamera && (++frame[stage] % 16 == 0);
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
    CameraConfig replacement{};
    if (!settings::Read("Data/SKSE/Plugins/ColonyCamera.ini", replacement)) {
        spdlog::error("Configuration unavailable or invalid; retaining current settings");
        return;
    }
    ApplyConfig(replacement);
    spdlog::info("Configuration loaded; enabled={}", config.enabled);
}

bool NativeAim(RE::PlayerCharacter* player, RE::PlayerCamera* camera) {
    if (camera->GetRuntimeData2().bowZoomedIn || player->WhoIsCasting() != 0) return true;
    if (!player->AsActorState()->IsWeaponDrawn()) return false;
    auto* item = player->GetEquippedObject(false);
    auto* weapon = item ? item->As<RE::TESObjectWEAP>() : nullptr;
    return weapon && (weapon->IsBow() || weapon->IsCrossbow());
}

// Group/stance selection is limited to the ordinary third-person state. The
// coordinator keeps mounts, furniture, transformations and scripted cameras native.
std::uint32_t ThirdStance(RE::PlayerCharacter* player) {
    if (!player->AsActorState()->IsWeaponDrawn()) return 0;
    bool ranged = false;
    for (const bool left : {false, true}) {
        auto* item = player->GetEquippedObject(left);
        if (!item) continue;
        if (item->Is(RE::FormType::Spell) || item->Is(RE::FormType::Scroll)) return 3;
        auto* weapon = item->As<RE::TESObjectWEAP>();
        ranged |= weapon && (weapon->IsBow() || weapon->IsCrossbow());
    }
    return ranged ? 2u : 1u;
}
std::uint32_t ThirdGroup(RE::PlayerCharacter* player, RE::PlayerCamera* camera, std::uint32_t stance) {
    const auto* actor = player->AsActorState();
    const auto attack = actor->GetAttackState();
    const bool drawing = attack == RE::ATTACK_STATE_ENUM::kBowDraw || attack == RE::ATTACK_STATE_ENUM::kBowDrawn
        || attack == RE::ATTACK_STATE_ENUM::kBowAttached || attack == RE::ATTACK_STATE_ENUM::kBowReleasing;
    if (stance == 2 && (drawing || camera->GetRuntimeData2().bowZoomedIn)) return 6;
    if (actor->IsSwimming()) return 5;
    if (actor->IsSneaking()) return 4;
    if (actor->IsSprinting()) return 3;
    const auto& movement = actor->actorState1;
    if (movement.movingForward || movement.movingBack || movement.movingLeft || movement.movingRight)
        return actor->IsWalking() ? 1u : 2u;
    return 0;
}

constexpr std::array previewMenus{
    RE::InventoryMenu::MENU_NAME, RE::MagicMenu::MENU_NAME,
    RE::TweenMenu::MENU_NAME, RE::MapMenu::MENU_NAME};

bool PreviewMenuOpen(RE::UI* ui) {
    if (ui) for (const auto name : previewMenus) if (ui->IsMenuOpen(name)) return true;
    return false;
}

bool InventoryBodyVisible() {
    auto* ui = RE::UI::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!ui || !camera || !camera->currentState) return false;
    const auto inventory = ui->GetMenu(RE::InventoryMenu::MENU_NAME);
    const bool blocked = framework_menu::Blocking() || settingsMenu.IsOpen() || ui->IsMenuOpen("Dialogue Menu") ||
        ui->IsMenuOpen("Loading Menu") || ui->IsMenuOpen("Console") ||
        ui->IsMenuOpen("Journal Menu") || ui->IsMenuOpen(RE::MagicMenu::MENU_NAME) ||
        ui->IsMenuOpen(RE::TweenMenu::MENU_NAME) || ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
    return body_position::RenderInventoryBody(
        camera->currentState->id == RE::CameraState::kFirstPerson,
        inventory && ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME), blocked,
        ui->numPausesGame, inventory && inventory->PausesGame());
}

CameraDecision Coordinate(RE::TESCameraState* current, bool inputEnabled) {
    auto* camera = RE::PlayerCamera::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ui = RE::UI::GetSingleton();
    auto* controls = RE::ControlMap::GetSingleton();
    CameraContext context{CC_NATIVE, 0, config.enabled, config.first_person_enabled,
        firstHooksInstalled && !firstConflict ? 1u : 0u, config.locomotion_profiles, config.third_person_enabled};
    if (current && current->id == RE::CameraState::kThirdPerson) context.camera_mode = CC_THIRD_PERSON;
    if (current && current->id == RE::CameraState::kFirstPerson) context.camera_mode = CC_FIRST_PERSON;
    if (current && camera && player && ui && current->camera == camera
        && camera->currentState.get() == current && camera->cameraTarget.get().get() == player) context.flags |= CC_AVAILABLE;
    const bool inventoryBody = InventoryBodyVisible();
    // This flag authorizes body rendering only; it does not change ControlMap.
    if (inventoryBody || (controls && controls->IsMovementControlsEnabled() && inputEnabled)) context.flags |= CC_CONTROLS;
    if (!inventoryBody && (!ui || ui->GameIsPaused() || ui->IsMenuOpen("Dialogue Menu") || ui->IsMenuOpen("Loading Menu")
        || ui->IsMenuOpen("Console") || PreviewMenuOpen(ui) || settingsMenu.IsOpen() || framework_menu::Blocking())) context.flags |= CC_MENU;
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
    if (pending & 1) { for (unsigned mode = 0; mode < std::size(timings); ++mode) ReportTiming(mode); auto replacement = config; replacement.enabled ^= 1; ApplyConfig(replacement); spdlog::info("Enabled={}", config.enabled); }
    if (pending & 2) { leftShoulder = !leftShoulder; spdlog::info("Left shoulder={}", leftShoulder); }
    if ((pending & 8) && menuAvailable) {
        // Both diagnostics pages use one camera-view record, copied before the
        // modal menu resets the renderer. A later model pass is not this sample.
        const auto snapshot = lastBodyView;
        const auto sampleAge = snapshot.available
            ? std::chrono::duration<double>(Clock::now() - snapshot.captured).count() : 0.0;
        const auto& facing = snapshot.facing;
        const auto facingText = facing.available
            ? std::format("Body facing - last camera-view sample\nView yaw: {:.1f} deg\nBody yaw: {:.1f} deg\nBody minus view: {:.1f} deg\n{} in body coordinates: ({:.2f}, {:.2f}, {:.2f})\nSample age at menu opening: {:.2f} s\nScene status: {}\nClose this menu, turn, then reopen to compare.",
                facing.viewYawDegrees, facing.bodyYawDegrees, facing.yawGapDegrees,
                snapshot.alignment.frame.eye_available ? "Eye" : "Head", facing.anchorLocal.x, facing.anchorLocal.y, facing.anchorLocal.z,
                sampleAge, snapshot.status == first_person::Status::applied ? "Applied" : "Rejected")
            : std::string("Body facing\nNo usable camera-view sample. Enable Body Experiment, enter ordinary first person and close the menu briefly before reopening it.");
        const auto logText = activeLogPath.empty()
            ? std::string("No log path was recorded.")
            : std::format("Log location reported by the game\n{}\nUnder Proton, this Windows path is inside the game's active prefix (pfx/drive_c).", activeLogPath);
        bodyRenderer.Reset();
        ResetFirstView();
        if (appliedTo) RestoreNative(appliedTo.get());
        RestoreCamera(true); resetRequested = true;
        constexpr const char* owners[] = {"Native", "Third person", "Body experiment"};
        constexpr const char* reasons[] = {"Active", "Disabled", "Camera unavailable", "Menu open", "Controls unavailable",
            "Death/bleedout", "Ragdoll", "Killmove", "Special action or unsupported race", "Native camera state",
            "Body experiment off", "Body hooks unavailable", "Invalid camera context", "Third-person camera off"};
        const auto& alignment = snapshot.alignment;
        const auto alignmentText = alignment.available
            ? std::format("\nLast body sample: scale {:.2f}, horizontal shift ({:.1f}, {:.1f}), eye/head height difference {:.1f}. {}",
                alignment.frame.scale, alignment.result.translation[0], alignment.result.translation[1],
                alignment.result.vertical_error, alignment.result.valid ? "Alignment math accepted." : "Alignment rejected; native fallback.")
            : std::string("\nNo body alignment sample yet. Enable the body experiment and enter first person.");
        const auto& publication = snapshot.publication;
        const auto publicationText = publication.available
            ? std::format("\nLast scene publication: {}. Bone position error {:.3f}, scale error {:.6f}. Visual framing still needs checking.",
                publication.matched ? "verified" : "rejected", publication.maxBonePositionError, publication.maxBoneScaleError)
            : std::string("\nNo scene publication sample yet.");
        const auto& sampleDecision = snapshot.available ? snapshot.decision : decision;
        const auto sampleText = snapshot.available
            ? std::format("\nCamera-view sample age at menu opening: {:.2f} s. Scene status: {}.",
                sampleAge, snapshot.status == first_person::Status::applied ? "Applied" : "Rejected")
            : std::string("\nNo camera-view body sample; owner/state describe the last coordinator decision.");
        const auto correctionText = snapshot.viewCorrectionAvailable
            ? std::format("\nRaw-to-rendered view correction ({:.2f}, {:.2f}, {:.2f})",
                snapshot.viewCorrection.x, snapshot.viewCorrection.y, snapshot.viewCorrection.z)
            : std::string("\nNo raw-to-rendered correction in this camera-view sample.");
        settingsMenu.Open(config, ApplyConfig, std::format("Version 0.3.2 advanced-camera candidate\nRuntime 1.7.104.0\nLast owner: {}\nState: {}\nThird-person camera: {}\nFirst-person hooks: {}  competing provider: {}{}{}{}{}",
            sampleDecision.owner < std::size(owners) ? owners[sampleDecision.owner] : "Unknown",
            sampleDecision.reason < std::size(reasons) ? reasons[sampleDecision.reason] : "Unknown", config.third_person_enabled != 0,
            firstHooksInstalled, firstConflict, sampleText, correctionText, alignmentText, publicationText), facingText, logText);
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
    RestoreFov(); coordinator = {};
    state = {}; advancedState = {};
    lastTick = {};
    originalEnd(self);
}
void Begin(RE::ThirdPersonState* self) {
    RestoreNative(self);
    RestorePosition();
    RestoreFov(); bodyRenderer.Reset(); coordinator = {};
    state = {}; advancedState = {};
    lastCell = nullptr;
    lastTick = {};
    originalBegin(self);
}

void Update(RE::ThirdPersonState* self, RE::BSTSmartPointer<RE::TESCameraState>& next) {
    auto* camera = RE::PlayerCamera::GetSingleton();
    // An inactive perspective may still be visited by a chained provider. It
    // must not restore the active body's pose or mutate the coordinator/history.
    if (!self || !camera || camera->currentState.get() != self || self->id != RE::CameraState::kThirdPerson) {
        originalUpdate(self, next);
        return;
    }
    static bool entered = false;
    if (!entered) { spdlog::info("Third-person Update callback reached"); entered = true; }
    ProcessCommands();
    Probe probe(true, config.enabled && config.third_person_enabled);
    const auto restoreStamp = probe.Mark();
    // Feed the native solver its own previous result, not our filtered output.
    // Restore only an output that is still ours; never overwrite an engine/load change.
    RestoreNative(self);
    RestorePosition(); RestoreFov();
    probe.result.scene = probe.Elapsed(restoreStamp);
    if (resetRequested.load()) { state = {}; advancedState = {}; reportedFrame = false; reportedMovement = false; }
    auto* player = RE::PlayerCharacter::GetSingleton();
    const auto before = Coordinate(self, self->IsInputEventHandlingEnabled());
    const bool eligible = before.owner == CC_THIRD_PERSON;
    const bool aim = eligible && before.profile == CC_AIM;
    const bool advancedBefore = eligible && config.third.enabled
        && (config.third.group_mask & (1u << ThirdGroup(player, camera, ThirdStance(player))));
    // Mirror native shoulder offsets only during this native update, restoring both values.
    float saved[2]{};
    const bool mirror = eligible && !advancedBefore && !aim && leftShoulder && shoulderSettings[0] && shoulderSettings[1];
    if (mirror) {
        for (int i=0; i<2; ++i) { saved[i] = shoulderSettings[i]->data.f; shoulderSettings[i]->data.f = -saved[i]; }
    }
    auto stamp = probe.Mark();
    originalUpdate(self, next);
    probe.result.native = probe.Elapsed(stamp);
    if (mirror) for (int i=0; i<2; ++i) if (shoulderSettings[i]->data.f == -saved[i]) shoulderSettings[i]->data.f = saved[i];
    if (!eligible || camera->currentState.get() != self || (next && next.get() != self)) {
        state = {}; advancedState = {}; lastTick = {}; return;
    }
    const auto after = Coordinate(self, self->IsInputEventHandlingEnabled());
    if (after.owner != CC_THIRD_PERSON) { state = {}; advancedState = {}; lastTick = {}; return; }
    const auto now = Clock::now();
    const float dt = lastTick == Clock::time_point{} ? 0.0f : std::chrono::duration<float>(now-lastTick).count();
    lastTick = now;
    auto* cell = player->GetParentCell();
    const bool reset = resetRequested.exchange(false) || before.reset || after.reset || lastCell != cell;
    lastCell = cell;
    const auto stance = config.third.enabled ? ThirdStance(player) : 0u;
    const auto group = config.third.enabled ? ThirdGroup(player, camera, stance) : 0u;
    const bool advancedSelected = config.third.enabled && (config.third.group_mask & (1u << group));
    auto profile = config.profiles[after.profile];
    if (leftShoulder && after.profile != CC_AIM) profile.offset[0] = -profile.offset[0];
    if (!advancedSelected && (profile.half_life == 0.0f || profile.max_lag == 0.0f)
        && profile.offset[0] == 0.0f && profile.offset[1] == 0.0f && profile.offset[2] == 0.0f
        && profile.zoom == 0.0f && profile.fov_offset == 0.0f
        && profile.offset_half_life == 0.0f && profile.zoom_half_life == 0.0f && profile.fov_half_life == 0.0f) {
        // Default aiming really is native: not even an extra collision query.
        state = {}; advancedState = {}; return;
    }
    auto* root = camera->cameraRoot.get();
    auto* rendered = RenderedCamera(camera);
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
        state = {}; advancedState = {}; return;
    }
    const auto native = self->translation;
    if (!std::isfinite(native.x) || !std::isfinite(native.y) || !std::isfinite(native.z)) {
        state = {}; advancedState = {}; return;
    }
    const auto q = self->rotation;
    const float nativeFov = camera->GetRuntimeData2().worldFOV;
    CameraFrame frame{{native.x, native.y, native.z}, {q.w, q.x, q.y, q.z}, dt, reset ? 1u : 0u, nativeFov};
    stamp = probe.Mark();
    CameraState candidate{};
    AdvancedState advancedCandidate{};
    bool advanced = false;
    if (advancedSelected) {
        auto focus = player->GetPosition();
        float presetZoom = 0.0f;
        if (config.third.preset_geometry) {
            // Cache only within this loaded body. Perspective/menu resets refresh
            // the landmark after any first-person body lease was restored.
            static RE::NiPointer<RE::NiAVObject> focusRoot, focusNode;
            auto* body = player->Get3D(false);
            if (reset || body != focusRoot.get() ||
                !body_position::AttachedTo<RE::NiAVObject>(focusNode.get(), body)) {
                focusNode.reset(); focusRoot.reset(body);
                if (body) for (const char* name : {"Camera3rd [Cam3]", "NPC Head [Head]", "NPC Head"}) {
                    if (auto* node = body->GetObjectByName(name)) { focusNode.reset(node); break; }
                }
            }
            // No usable landmark: keep the native result, never orbit the feet.
            if (!focusNode || !std::isfinite(focusNode->world.translate.z)) {
                state = {}; advancedState = {}; return;
            }
            focus.z = focusNode->world.translate.z;
            float minimumZoom = 0.2f;
            if (auto* settings = RE::INISettingCollection::GetSingleton()) {
                if (auto* value = settings->GetSetting("fMinCurrentZoom:Camera")) minimumZoom = value->GetFloat();
            }
            if (!std::isfinite(minimumZoom) || !std::isfinite(self->targetZoomOffset)) {
                state = {}; advancedState = {}; return;
            }
            presetZoom = (std::max)(0.0f, self->targetZoomOffset - minimumZoom);
        }
        // Camera-local forward is +Y. Negative elevation is downward pitch;
        // unlike actor pitch this includes free-look and other camera rotation.
        const float norm = q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
        const float pitch = norm > 0.0f
            ? -std::asin(std::clamp(2.0f*(q.w*q.x + q.y*q.z)/norm, -1.0f, 1.0f))*57.2957795f : 0.0f;
        AdvancedFrame input{frame, {focus.x, focus.y, focus.z}, group, stance, pitch,
            self->posOffsetActual.x, self->posOffsetActual.z, leftShoulder ? 1u : 0u, presetZoom};
        if (!reportedFrame) spdlog::info("Advanced geometry: preset={} group={} stance={} focus=({:.2f},{:.2f},{:.2f}) zoom={:.3f} base={:.2f} scale={:.2f}",
            config.third.preset_geometry, group, stance, focus.x, focus.y, focus.z,
            presetZoom, config.third.min_distance, config.third.zoom_scale);
        const auto result = cc_step_advanced(&advancedState, &input, &config.third, &advancedCandidate);
        if (result) { state = {}; advancedState = {}; return; }
        advanced = advancedCandidate.initialized != 0;
        if (advanced) {
            std::copy_n(advancedCandidate.position, 3, candidate.position);
            std::copy_n(advancedCandidate.native, 3, candidate.native);
            candidate.initialized = 1;
            candidate.fov = advancedCandidate.fov;
            candidate.fov_delta = advancedCandidate.fov_delta;
        }
    }
    if (!advanced) {
        frame.reset |= advancedState.initialized;
        advancedState = {};
        candidate = cc_step(state, frame, profile);
    }
    probe.result.math = probe.Elapsed(stamp);
    if (!candidate.initialized) { state = {}; advancedState = {}; return; }
    RE::NiPoint3 position{candidate.position[0], candidate.position[1], candidate.position[2]};
    // Native collision is deliberately last. Never interpolate away from its correction.
    stamp = probe.Mark();
    camera->CheckCameraCollision(position, true);
    probe.result.collision = probe.Elapsed(stamp);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        state = {}; advancedState = {}; return;
    }
    const float corrected[]{position.x, position.y, position.z};
    for (unsigned i = 0; i < 3; ++i) {
        const float delta = corrected[i] - candidate.position[i];
        if (advanced) {
            advancedCandidate.orbit[i] += delta;
            advancedCandidate.position[i] = corrected[i];
        } else candidate.base[i] += delta;
        candidate.position[i] = corrected[i];
    }
    stamp = probe.Mark();
    auto nativeState = self->translation;
    auto local = root->local.translate;
    auto world = root->world.translate;
    auto renderPosition = rendered->world.translate;
    if (!scene::PublishPosition(nativeState, local, world,
            renderPosition, position, root->parent ? &root->parent->world : nullptr)) {
        static bool warned = false;
        if (!warned) { spdlog::warn("Invalid camera parent transform or position; retaining native camera"); warned = true; }
        state = {}; advancedState = {}; return;
    }
    // Commit only after every coordinate passed validation. Retain node lifetimes
    // and restore only values still owned by this plugin at the next boundary.
    ownedLocal.Write(root->local.translate, local);
    ownedWorld.Write(root->world.translate, world);
    ownedRendered.Write(rendered->world.translate, renderPosition);
    self->translation = nativeState;
    positionRoot.reset(root); positionCamera.reset(rendered);
    state = candidate;
    advancedState = advancedCandidate;
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
        spdlog::info("Camera frame applied; engine={} native=({},{},{}) output=({},{},{}) half_life={} max_lag={}",
            advanced ? "advanced" : "legacy", native.x, native.y, native.z, position.x, position.y, position.z, profile.half_life, profile.max_lag);
        reportedFrame = true;
    }
}

void FirstBegin(RE::FirstPersonState* self) {
    lastBodyView = {};
    if (appliedTo) RestoreNative(appliedTo.get());
    RestorePosition();
    bodyRenderer.Reset(); RestoreFov(); state = {}; advancedState = {}; coordinator = {}; lastTick = {};
    reportedBodyAlignment = false;
    reportedViewMotion = false;
    ResetFirstView();
    bodyTraces = {};
    originalFirstBegin(self);
}
void FirstEnd(RE::FirstPersonState* self) {
    lastBodyView = {};
    bodyRenderer.Reset(); coordinator = {};
    ResetFirstView();
    originalFirstEnd(self);
}
void FirstUpdate(RE::FirstPersonState* self, RE::BSTSmartPointer<RE::TESCameraState>& next) {
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!self || !camera || camera->currentState.get() != self || self->id != RE::CameraState::kFirstPerson) {
        originalFirstUpdate(self, next);
        return;
    }
    ProcessCommands();
    originalFirstUpdate(self, next);
    if (camera->currentState.get() == self && (!next || next.get() == self)) firstUpdatedInView = self;
    const auto current = Coordinate(self, self->IsInputEventHandlingEnabled());
    if (current.owner != CC_FIRST_PERSON || (next && next.get() != self)) bodyRenderer.Reset();
    // The final view is not ready until the containing TESCamera update returns.
    // Both view and model publication use the displayed camera as their anchor.
}

void PublishFirstPersonBody(Probe& probe, unsigned phaseIndex) {
    if (phaseIndex == 1) lastBodyView = {};
    const char* phase = phaseIndex == 0 ? "model" : "camera-view";
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!player || !camera || !camera->currentState
        || camera->currentState->id != RE::CameraState::kFirstPerson) {
        bodyRenderer.Reset();
        if (camera && camera->currentState && camera->currentState->id == RE::CameraState::kFirstPerson)
            TraceBody(phaseIndex, BodyOutcome::native_fallback, CC_UNAVAILABLE);
        return;
    }
    const auto currentState = camera->currentState;
    auto* self = static_cast<RE::FirstPersonState*>(currentState.get());
    auto stamp = probe.Mark();
    const auto current = Coordinate(self, self->IsInputEventHandlingEnabled());
    probe.result.math = probe.Elapsed(stamp);
    if (current.owner != CC_FIRST_PERSON) {
        bodyRenderer.Reset(); TraceBody(phaseIndex, BodyOutcome::native_fallback, current.reason); return;
    }
    auto* rendered = RenderedCamera(camera);
    if (!rendered || firstPublishedState.get() != self || firstPublishedCamera.get() != rendered) {
        bodyRenderer.Reset(); TraceBody(phaseIndex, BodyOutcome::view_not_ready); return;
    }
    // GetTranslation is a raw model anchor. Skyrim adds dampening and collision
    // before publishing the view. Align to what is actually displayed instead.
    const auto viewEye = rendered->world.translate;
    RE::NiPoint3 viewCorrection{};
    bool viewCorrectionAvailable = false;
    if (self->firstPersonCameraObj) {
        RE::NiPoint3 rawEye{};
        self->GetTranslation(rawEye);
        viewCorrection = viewEye - rawEye;
        const auto& delta = viewCorrection;
        viewCorrectionAvailable = std::isfinite(delta.x) && std::isfinite(delta.y) && std::isfinite(delta.z);
        if (!reportedViewMotion && viewCorrectionAvailable
            && delta.x*delta.x + delta.y*delta.y + delta.z*delta.z > 0.25f) {
            spdlog::info("First-person final view differs from raw model anchor: phase={} delta=({:.3f},{:.3f},{:.3f}); body follows rendered view, third-person effect={}",
                phase, delta.x, delta.y, delta.z, config.third_person_enabled);
            reportedViewMotion = true;
        }
    }
    stamp = probe.Mark();
    const auto status = bodyRenderer.Apply(player, viewEye,
        config.body_alignment, rendered, probe.sampled);
    if (phaseIndex == 1) {
        lastBodyView = {bodyRenderer.GetFacingSample(), bodyRenderer.GetAlignmentSample(),
            bodyRenderer.GetPublicationSample(), current, viewCorrection, Clock::now(),
            status, true, viewCorrectionAvailable};
    }
    TraceBody(phaseIndex, status == first_person::Status::applied ? BodyOutcome::applied : BodyOutcome::rejected,
        static_cast<unsigned>(status), rendered);
    const double bodyMath = bodyRenderer.AlignmentMathMicroseconds();
    probe.result.math += bodyMath;
    probe.result.scene += (std::max)(0.0, probe.Elapsed(stamp) - bodyMath);
    probe.result.applied = status == first_person::Status::applied;
    static auto lastStatus = first_person::Status::inactive;
    const bool statusChanged = status != lastStatus;
    if (statusChanged) {
        spdlog::info("First-person body experiment status={} (scene writes do not establish visual correctness)", static_cast<unsigned>(status));
        lastStatus = status;
    }
    const auto& alignment = bodyRenderer.GetAlignmentSample();
    if ((!reportedBodyAlignment || statusChanged) && alignment.available) {
        spdlog::info("Body alignment sample: phase={} math_valid={} enabled={} scale={:.3f} rendered_eye=({:.2f},{:.2f},{:.2f}) placement=ic_no_headbob root=({:.2f},{:.2f},{:.2f}) head=({:.2f},{:.2f},{:.2f}) eye_available={} body_eye=({:.2f},{:.2f},{:.2f}) shift=({:.2f},{:.2f},{:.2f}) height_difference={:.2f} backset={:.1f} sideways={:.1f} world_fov={:.1f}; framing and equipment require visual verification",
            phase, alignment.result.valid, config.body_alignment.alignment_enabled, alignment.frame.scale,
            alignment.frame.camera[0], alignment.frame.camera[1], alignment.frame.camera[2],
            alignment.frame.body_root[0], alignment.frame.body_root[1], alignment.frame.body_root[2],
            alignment.frame.head[0], alignment.frame.head[1], alignment.frame.head[2],
            alignment.frame.eye_available, alignment.frame.eye[0], alignment.frame.eye[1], alignment.frame.eye[2],
            alignment.result.translation[0], alignment.result.translation[1], alignment.result.translation[2],
            alignment.result.vertical_error, config.body_alignment.body_backset, config.body_alignment.body_side,
            camera ? camera->GetRuntimeData2().worldFOV : 0.0f);
        const auto& publication = bodyRenderer.GetPublicationSample();
        spdlog::info("Body scene publication: available={} matched={} native_arms={} expected=({:.3f},{:.3f},{:.3f}) actual=({:.3f},{:.3f},{:.3f}) max_bone_position_error={:.6f} max_bone_scale_error={:.6f}; native local transforms restored after publication",
            publication.available, publication.matched, bodyRenderer.UsesNativeArms(),
            publication.expectedBody.x, publication.expectedBody.y, publication.expectedBody.z,
            publication.actualBody.x, publication.actualBody.y, publication.actualBody.z,
            publication.maxBonePositionError, publication.maxBoneScaleError);
        reportedBodyAlignment = true;
    }
}

void BodySceneUpdate(RE::NiAVObject* object, RE::NiUpdateData* update) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || object != player->Get3D(true)) { originalBodySceneUpdate(object, update); return; }
    auto* camera = RE::PlayerCamera::GetSingleton();
    const bool first = camera && camera->currentState && camera->currentState->id == RE::CameraState::kFirstPerson;
    Probe probe(first, config.enabled && config.first_person_enabled, 1);
    auto stamp = probe.Mark();
    bodyRenderer.Restore();
    probe.result.scene = probe.Elapsed(stamp);
    stamp = probe.Mark();
    originalBodySceneUpdate(object, update);
    probe.result.native = probe.Elapsed(stamp);
    player = RE::PlayerCharacter::GetSingleton();
    if (!player || object != player->Get3D(true)) {
        bodyRenderer.Reset(); TraceBody(0, BodyOutcome::native_fallback, CC_UNAVAILABLE); return;
    }
    PublishFirstPersonBody(probe, 0);
}

void CameraViewUpdate(RE::TESCamera* self) {
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!self || !camera || self != camera) { originalCameraViewUpdate(self); return; }
    const auto beforeState = camera->currentState;
    const bool hadFirstView = firstPublishedState == beforeState &&
        firstPublishedCamera.get() == RenderedCamera(camera) && firstPublishedCamera;
    const bool first = camera->currentState && camera->currentState->id == RE::CameraState::kFirstPerson;
    Probe probe(first, config.enabled && config.first_person_enabled, 2);
    auto stamp = probe.Mark();
    // Never let our previous rendered body become input to the native view solver.
    if (first) bodyRenderer.Restore();
    ResetFirstView();
    probe.result.scene = probe.Elapsed(stamp);
    stamp = probe.Mark();
    originalCameraViewUpdate(self);
    probe.result.native = probe.Elapsed(stamp);
    camera = RE::PlayerCamera::GetSingleton();
    // TESCamera can Begin a new POV without updating its view until the next
    // call. Never align a new first-person body to the previous third-person eye.
    if (!camera || self != camera || !first || camera->currentState != beforeState
        || (firstUpdatedInView != beforeState.get() && !(hadFirstView && InventoryBodyVisible()))) {
        lastBodyView = {};
        bodyRenderer.Reset();
        if (first) TraceBody(1, BodyOutcome::view_not_ready);
        return;
    }
    // Paused inventory may skip FirstPersonState::Update. The enclosing camera
    // pass has completed and the same first-person state is still current.
    firstPublishedState = camera->currentState;
    firstPublishedCamera.reset(RenderedCamera(camera));
    // Reconcile again after the final camera pass. If model/camera order changes,
    // whichever runs last still aligns the current body to the displayed eye.
    PublishFirstPersonBody(probe, 1);
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
            if (!(ctrlLeft || ctrlRight) || !button->IsDown() || !ui || ui->GameIsPaused() || ui->IsMenuOpen("Console") || framework_menu::Blocking()) continue;
            unsigned action = 0;
            for (int i=0; i<3; ++i) if (key == bindings[i].load()) action |= 1u << i;
            if (key == menuBinding.load()) action |= 8;
            if (action) { commands.fetch_or(action); SKSE::GetTaskInterface()->AddTask(ProcessCommands); }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
Input input;

class PreviewMenuEvents final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        if (!event || !event->opening) return RE::BSEventNotifyControl::kContinue;
        const auto* name = event->menuName.c_str();
        bool preview = false;
        if (name) for (const auto candidate : previewMenus) preview |= candidate == name;
        if (!preview) return RE::BSEventNotifyControl::kContinue;
        // Use the SKSE main-thread queue; event delivery alone does not prove
        // ownership of scene data. Coordinate also checks fresh UI state before
        // every publication, including menus that do not pause gameplay.
        if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([] {
            // A delayed open notification must not clear a new gameplay pose
            // after the menu has already closed. There is no persistent gate.
            if (!PreviewMenuOpen(RE::UI::GetSingleton()) || InventoryBodyVisible()) return;
            bodyRenderer.Reset();
            ResetFirstView();
            lastBodyView = {};
        });
        return RE::BSEventNotifyControl::kContinue;
    }
};
PreviewMenuEvents previewMenuEvents;

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
        std::uintptr_t sceneTarget = 0, viewTarget = 0;
        REL::Relocation<std::uintptr_t> sceneAddress{REL::ID(70251)};
        REL::Relocation<std::uintptr_t> bodySceneAddress{REL::ID(40522)};
        REL::Relocation<std::uintptr_t> cameraAddress{REL::ID(33025)};
        REL::Relocation<std::uintptr_t> playerCameraAddress{REL::ID(50784)};
        REL::Relocation<std::uintptr_t> firstTable{*reinterpret_cast<std::uintptr_t*>(first)};
        const auto* entries = reinterpret_cast<const std::uintptr_t*>(firstTable.address());
        if (firstTable.address() == base + runtime::firstVtable
            && hooks::Memory(firstTable.address(), 6 * sizeof(std::uintptr_t), false)
            && hooks::Entry(runtime::firstBegin, base) && hooks::Entry(runtime::firstEnd, base)
            && hooks::Entry(runtime::firstUpdate, base) && hooks::Entry(runtime::firstTranslation, base)
            && sceneAddress.address() == base + runtime::sceneUpdate.rva
            && bodySceneAddress.address() == base + runtime::bodySceneUpdate.rva
            && hooks::Entry(runtime::sceneUpdate, base) && hooks::Entry(runtime::bodySceneUpdate, base)
            && hooks::SceneCallTarget(runtime::bodySceneCall, base, sceneTarget)
            && cameraAddress.address() == base + runtime::cameraUpdate.rva
            && playerCameraAddress.address() == base + runtime::playerCameraUpdate.rva
            && hooks::Entry(runtime::cameraUpdate, base) && hooks::Entry(runtime::playerCameraUpdate, base)
            && hooks::SceneCallTarget(runtime::cameraUpdateCall, base, viewTarget)
            && SKSE::GetTrampoline().free_size() >= 28
            && hooks::ModuleCode(entries[1]) && hooks::ModuleCode(entries[2]) && hooks::ModuleCode(entries[3])
            && hooks::ModuleCode(entries[5])) {
            LogEntry("Body scene", sceneTarget);
            LogEntry("Final camera view", viewTarget);
            originalBodySceneUpdate = sceneTarget;
            originalCameraViewUpdate = viewTarget;
            originalBodySceneUpdate = SKSE::GetTrampoline().write_call<5>(base + runtime::bodySceneCall.rva, BodySceneUpdate);
            originalCameraViewUpdate = SKSE::GetTrampoline().write_call<5>(base + runtime::cameraUpdateCall.rva, CameraViewUpdate);
            originalFirstBegin = firstTable.write_vfunc(1, FirstBegin);
            originalFirstEnd = firstTable.write_vfunc(2, FirstEnd);
            originalFirstUpdate = firstTable.write_vfunc(3, FirstUpdate);
            firstHooksInstalled = true;
            spdlog::info("First-person body publication installed after native model and final camera passes");
        } else spdlog::error("First-person vtable/model call validation or trampoline capacity failed; body experiment disabled");
    }
    if (firstConflict) spdlog::warn("Improved Camera detected; Colony Camera body experiment is disabled");
    spdlog::info("Camera hooks installed after game load; ImprovedCameraSE={}", (GetModuleHandleW(L"ImprovedCamera.dll") != nullptr || GetModuleHandleW(L"ImprovedCameraSE.dll") != nullptr));
}
void Message(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoadGame || message->type == SKSE::MessagingInterface::kNewGame) InstallHooks();
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        framework_menu::Register(config, ApplyConfig);
        auto* settings = RE::INISettingCollection::GetSingleton();
        if (settings) {
            shoulderSettings[0] = settings->GetSetting("fOverShoulderPosX:Camera");
            shoulderSettings[1] = settings->GetSetting("fOverShoulderCombatPosX:Camera");
        }
        for (auto& s : shoulderSettings) if (s && s->GetType() != RE::Setting::Type::kFloat) s = nullptr;
        if (!shoulderSettings[0] || !shoulderSettings[1]) spdlog::warn("Shoulder mirroring unavailable: native settings missing");
        if (auto* manager = RE::BSInputDeviceManager::GetSingleton()) manager->AddEventSink(&input);
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&previewMenuEvents);
        spdlog::info("Data ready; Ctrl+F8 toggle, Ctrl+F9 shoulder, Ctrl+F10 reload (default bindings)");
    }
    if (message->type == SKSE::MessagingInterface::kPreLoadGame || message->type == SKSE::MessagingInterface::kPostLoadGame
        || message->type == SKSE::MessagingInterface::kNewGame) {
        if (appliedTo) RestoreNative(appliedTo.get());
        RestorePosition(); bodyRenderer.Reset(); ResetFirstView(); RestoreFov(); coordinator = {}; settingsMenu.Cancel();
        bodyTraces = {};
        lastBodyView = {};
        input.Reset(); commands = 0; resetRequested = true;
    }
}
}

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = [] {
    SKSE::PluginVersionData info;
    info.PluginVersion({0,3,2,0}); info.PluginName("ColonyCamera"); info.AuthorName("MotherSphere");
    info.CompatibleVersions({REL::Version{1,7,104,0}});
    info.MinimumRequiredXSEVersion({2,3,1,0});
    return info;
}();
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    if (!skse || skse->RuntimeVersion() != REL::Version{1,7,104,0}) return false;
    SKSE::Init(skse, {.trampoline = true, .trampolineSize = 64});
    try {
        auto directory = SKSE::log::log_directory();
        if (!directory) return false;
        activeLogPath = (*directory / "ColonyCamera.log").string();
        auto logger = std::make_shared<spdlog::logger>("ColonyCamera",
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(activeLogPath, true));
        spdlog::set_default_logger(logger); spdlog::flush_on(spdlog::level::info);
        spdlog::info("Camera Colony 0.3.2 advanced-camera candidate; runtime {}", skse->RuntimeVersion().string());
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
