#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Include after CameraFrame. Independent value math; no game pointers or ownership.
// Curves: linear, then in/out/in-out for quadratic, cubic, quartic, quintic,
// sine, circular, exponential (IDs 0..21).
struct Follow {
    std::uint32_t enabled, curve;
    float min_rate, max_rate; // response fraction at 60 Hz; 0 holds, 1 snaps
    float distance; // distance at which max_rate is reached
};
struct AdvancedProfile {
    float offset[3]; // absolute camera-local side/up; additive camera-local depth
    float fov; // additive degrees; native FOV changes remain immediate
    Follow world, local, vertical;
};
struct ThirdOptions {
    std::uint32_t enabled, group_mask;
    // group*4+stance: standing, walking, running, sprinting, sneaking, swimming,
    // bowAim, sitting, horseback, dragon, vampireLord, werewolf, userDefined, vanity;
    // each has neutral, melee, ranged, magic. Engine eligibility is caller-owned.
    AdvancedProfile profiles[56];
    std::uint32_t offset_curve, zoom_curve, fov_curve;
    float offset_duration, zoom_duration, fov_duration; // seconds, zero is immediate
    std::uint32_t clamp_mask; // camera-local X/Y/Z bits; limits apply to motion lag
    float clamp_min[3], clamp_max[3]; // signed bounds, each interval contains zero
    std::uint32_t pitch_enabled, pitch_after, pitch_curve;
    float pitch_max_angle, pitch_zoom; // positive downward pitch degrees, signed local Y
    std::uint32_t mirror_clamp;
};
struct AdvancedFrame {
    CameraFrame native; // unmodified engine camera, before plugin correction
    float focus[3]; // player world anchor, sampled without previous correction
    std::uint32_t group, stance;
    float pitch_degrees; // positive downward; upward pitch does not activate zoom
    float native_side, native_up; // actual interpolated native local offsets, not INI targets
    std::uint32_t shoulder_mirrored;
};
struct AdvancedState {
    float position[3], native[3];
    std::uint32_t initialized; // 0: use legacy/native fallback, never publish position
    float anchor[3], focus[3], orbit[3]; // world anchor, last native focus, relative orbit
    float offset[3], offset_from[3], offset_target[3];
    float fov, fov_delta, fov_from, fov_target;
    float elapsed[3]; // side/up, depth, FOV transition clocks
};
static_assert(sizeof(Follow) == 20 && sizeof(AdvancedProfile) == 76);
static_assert(sizeof(ThirdOptions) == 4340 && alignof(ThirdOptions) == 4);
static_assert(sizeof(AdvancedFrame) == 76 && sizeof(AdvancedState) == 128);
static_assert(offsetof(ThirdOptions, profiles) == 8 && offsetof(ThirdOptions, clamp_mask) == 4288);
static_assert(offsetof(AdvancedFrame, focus) == 40 && offsetof(AdvancedFrame, shoulder_mirrored) == 72);
static_assert(offsetof(AdvancedState, anchor) == 28 && offsetof(AdvancedState, elapsed) == 116);
static_assert(std::is_standard_layout_v<AdvancedState> && std::is_trivially_copyable_v<ThirdOptions>);
// Status 0 success, 1 pointer/alignment/overlap, 3 invalid settings/frame, 5 panic.
// The step validates global controls and the selected profile; validate the full
// configuration with cc_advanced_validate before publishing new settings.
// Failures leave output unchanged. All input/output regions must be disjoint.
// Initialize state with zeroes; native.reset forces a fresh result. Disabled or
// masked groups return success with initialized=0 and original native position.
// Run engine collision LAST; feed its correction into next.orbit and position.
extern "C" std::uint32_t cc_step_advanced(const AdvancedState*, const AdvancedFrame*, const ThirdOptions*, AdvancedState*);
extern "C" std::uint32_t cc_advanced_defaults(ThirdOptions*);
extern "C" std::uint32_t cc_advanced_validate(const ThirdOptions*);
