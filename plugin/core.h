#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Value-only, game-thread ABI. No engine pointer crosses this boundary.
struct CameraState {
    float position[3];
    float native[3];
    std::uint32_t initialized;
    float base[3];
    float offset[3];
    float zoom;
    float fov;
    float fov_delta; // only this additive adjustment is interpolated
};
struct CameraFrame {
    float position[3];
    float rotation[4]; // w,x,y,z
    float dt;
    std::uint32_t reset;
    float world_fov; // native world FOV, degrees
};
struct CameraProfile {
    float offset[3];
    float half_life;
    float max_lag;
    float offset_half_life;
    float zoom_half_life;
    float fov_half_life;
    float zoom; // additional camera-local Y distance, native zoom is preserved
    float fov_offset; // degrees added to native world FOV
};
enum CameraProfileIndex : std::uint32_t {
    CC_EXPLORATION, CC_COMBAT, CC_AIM, CC_SPRINT, CC_SNEAK, CC_SWIM, CC_AIRBORNE,
    CC_PROFILE_COUNT
};
struct BodyAlignmentFrame {
    float camera[3];
    float head[3]; // unsuppressed head world position, with previous body lease restored
    float heading[2]; // horizontal published-view forward XY; normalized in Rust, no pitch
    float scale; // cumulative unsuppressed skeleton world scale
    float eye[3]; // actual unsuppressed 3P eye landmark, diagnostics only
    std::uint32_t eye_available; // height diagnostic: 0=head, 1=eye; other values rejected
    float body_root[3]; // required unmodified locomotion-root world position; previous lease restored
};
struct BodyAlignmentOptions {
    std::uint32_t alignment_enabled;
    float body_backset; // 0..40, places body root behind camera at scale one
    float body_side; // -20..20, positive moves body right at scale one
};
struct BodyAlignmentResult {
    float translation[3]; // world displacement; Z is always zero
    float vertical_error; // camera Z minus selected eye/head landmark Z; diagnostics only
    std::uint32_t valid; // zero means native fallback, never apply a partial result
};
struct CameraConfig {
    CameraProfile profiles[CC_PROFILE_COUNT];
    std::uint32_t keys[3]; // original Ctrl+F8/F9/F10 bindings
    std::uint32_t enabled;
    std::uint32_t menu_key; // Ctrl+F7 by default
    std::uint32_t first_person_enabled; // request only: renderer readiness is separate
    std::uint32_t locomotion_profiles; // bit (1 << CameraProfileIndex), opt-in sections
    BodyAlignmentOptions body_alignment;
    std::uint32_t third_person_enabled; // independent gate; enabled remains the master
};
enum CameraOwner : std::uint32_t { CC_NATIVE, CC_THIRD_PERSON, CC_FIRST_PERSON };
enum CameraFlags : std::uint32_t {
    CC_AVAILABLE = 1u << 0, CC_CONTROLS = 1u << 1, CC_MENU = 1u << 2,
    CC_DEAD = 1u << 3, CC_RAGDOLL = 1u << 4, CC_KILLMOVE = 1u << 5,
    CC_MOUNTED = 1u << 6, CC_FURNITURE = 1u << 7, CC_TRANSFORMED = 1u << 8,
    CC_SCRIPTED = 1u << 9, CC_AIMING = 1u << 10, CC_WEAPON_DRAWN = 1u << 11,
    CC_SPRINTING = 1u << 12, CC_SNEAKING = 1u << 13,
    CC_SWIMMING = 1u << 14, CC_IN_AIR = 1u << 15
};
enum CameraFallback : std::uint32_t {
    CC_ACTIVE, CC_DISABLED, CC_UNAVAILABLE, CC_MENU_OPEN, CC_CONTROLS_UNAVAILABLE,
    CC_DEATH, CC_RAGDOLL_STATE, CC_KILLMOVE_STATE, CC_SPECIAL_STATE,
    CC_NATIVE_STATE, CC_FIRST_PERSON_DISABLED, CC_FIRST_PERSON_UNAVAILABLE,
    CC_INVALID_CONTEXT, CC_THIRD_PERSON_DISABLED
};
struct CameraCoordinator { std::uint32_t owner; std::uint32_t initialized; };
struct CameraContext {
    std::uint32_t camera_mode; // normalized CameraOwner, never a raw engine enum
    std::uint32_t flags;
    std::uint32_t enabled;
    std::uint32_t first_person_enabled;
    std::uint32_t first_person_ready;
    std::uint32_t locomotion_profiles;
    std::uint32_t third_person_enabled;
};
struct CameraDecision {
    CameraCoordinator next;
    std::uint32_t owner;
    std::uint32_t profile;
    std::uint32_t reset;
    std::uint32_t reason;
};
static_assert(std::is_standard_layout_v<CameraState> && std::is_trivially_copyable_v<CameraConfig>);
static_assert(sizeof(CameraState) == 64 && sizeof(CameraFrame) == 40);
static_assert(sizeof(CameraProfile) == 40 && sizeof(CameraConfig) == 324);
static_assert(sizeof(BodyAlignmentFrame) == 64 && sizeof(BodyAlignmentOptions) == 12 && sizeof(BodyAlignmentResult) == 20);
static_assert(offsetof(BodyAlignmentFrame, eye) == 36 && offsetof(BodyAlignmentFrame, eye_available) == 48);
static_assert(offsetof(BodyAlignmentFrame, body_root) == 52);
static_assert(sizeof(CameraCoordinator) == 8 && sizeof(CameraContext) == 28 && sizeof(CameraDecision) == 24);
static_assert(alignof(CameraState) == 4 && alignof(CameraConfig) == 4);
static_assert(offsetof(CameraState, base) == 28 && offsetof(CameraProfile, offset_half_life) == 20);
static_assert(offsetof(CameraConfig, keys) == 280 && offsetof(CameraConfig, menu_key) == 296);
static_assert(offsetof(CameraConfig, body_alignment) == 308 && alignof(BodyAlignmentFrame) == 4);
static_assert(offsetof(CameraConfig, third_person_enabled) == 320 && offsetof(CameraContext, third_person_enabled) == 24);

extern "C" CameraState cc_step(CameraState, CameraFrame, CameraProfile);
extern "C" CameraDecision cc_coordinate(CameraCoordinator, CameraContext);
extern "C" BodyAlignmentResult cc_align_body(BodyAlignmentFrame, BodyAlignmentOptions);
extern "C" CameraConfig cc_defaults();
// Status: 0 success, 1 invalid pointer/size/alignment/overlap, 2 UTF-8,
// 3 invalid settings, 4 output capacity (required length is returned), 5 panic.
// Pointer storage must remain alive/readable or writable for the entire call.
extern "C" std::uint32_t cc_validate_config(const CameraConfig*);
extern "C" std::uint32_t cc_parse_config(const unsigned char*, std::size_t, CameraConfig*);
// No trailing NUL. To query required bytes, pass nullptr,0 as output/capacity.
extern "C" std::uint32_t cc_serialize_config(const CameraConfig*, unsigned char*, std::size_t, std::size_t*);
