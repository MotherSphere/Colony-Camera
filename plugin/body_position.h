#pragma once
#include "owned_value.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <RE/N/NiTransform.h>
#include <cmath>
#include <limits>

namespace body_position {
// NiCamera's published basis uses column 0 for depth and column 2 for screen X.
// Verified from the native world-to-camera matrix routine on the target runtime.
// For ordinary native views without roll, screen-right keeps the same horizontal
// yaw across vertical pitch. Projected forward reverses beyond that pole. Prefer
// right consistently; use forward only when right has no usable XY projection.
// This defines a screen-right policy, not equivalent yaw for arbitrary rolled
// camera rigs. On failure leave the caller's output alone.
inline bool ViewHeading(const RE::NiMatrix3& rotation, std::array<float, 2>& result) {
    for (const auto& row : rotation.entry)
        for (float value : row) if (!std::isfinite(value)) return false;
    float x = -rotation.entry[1][2], y = rotation.entry[0][2];
    float length = std::hypot(x, y);
    if (length < 0.001f) {
        x = rotation.entry[0][0]; y = rotation.entry[1][0];
        length = std::hypot(x, y);
    }
    if (!std::isfinite(length) || length < 0.001f) return false;
    result = {x / length, y / length};
    return true;
}

struct ArmSelection {
    bool hideNative;
    std::array<float, 3> boneFactors; // head, left upper arm, right upper arm
};

// Ordinary sheathed posture uses the body rig's complete shoulder/arm silhouette.
// Combat and equipped lights retain the native first-person equipment rig.
inline ArmSelection SelectArms(bool weaponDrawn, bool equippedLight) {
    constexpr float masked = 0.0001f;
    return weaponDrawn || equippedLight ? ArmSelection{false, {masked, masked, masked}}
                                      : ArmSelection{true, {masked, 1.0f, 1.0f}};
}

struct PublicationSample {
    RE::NiPoint3 expectedBody{};
    RE::NiPoint3 actualBody{};
    float maxBonePositionError = 0.0f;
    float maxBoneScaleError = 0.0f;
    bool available = false;
    bool matched = false;
};
// Local coordinates belong to a specific parent. Reparenting is an external
// ownership change even if the engine happens to retain our last numeric value.
inline bool RestoreTranslation(camera::OwnedValue<RE::NiPoint3>& lease,
    RE::NiPoint3& local, const void* capturedParent, const void* currentParent) {
    if (capturedParent != currentParent) {
        lease = {};
        return false;
    }
    return lease.Restore(local);
}

inline bool Finite(const RE::NiPoint3& point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

inline bool Finite(const RE::NiTransform& transform) {
    if (!Finite(transform.translate) || !std::isfinite(transform.scale) || transform.scale <= 0.0f)
        return false;
    for (const auto& row : transform.rotate.entry)
        for (float value : row) if (!std::isfinite(value)) return false;
    return true;
}

inline float PositionError(const RE::NiPoint3& expected, const RE::NiPoint3& actual) {
    return (std::max)({std::abs(expected.x-actual.x), std::abs(expected.y-actual.y),
        std::abs(expected.z-actual.z)});
}

inline float PositionTolerance(const RE::NiPoint3& expected, const RE::NiPoint3& actual, float rigScale) {
    const float magnitude = (std::max)({std::abs(expected.x), std::abs(expected.y), std::abs(expected.z),
        std::abs(actual.x), std::abs(actual.y), std::abs(actual.z)});
    // Hierarchy recomposition and oldWorld+shift can differ by a few float ULPs.
    // Bound that allowance; large stale corrections must never become acceptable.
    const float roundoff = (std::min)(0.25f, 2.0f * std::numeric_limits<float>::epsilon() * magnitude);
    return (std::max)({0.01f, rigScale * 0.01f, roundoff});
}

inline bool WorldMatchesLocal(const RE::NiTransform& local, const RE::NiTransform& world,
    const RE::NiTransform* parent, float rigScale) {
    if (!Finite(local) || !Finite(world) || (parent && !Finite(*parent)) ||
        !std::isfinite(rigScale) || rigScale <= 0) return false;
    const auto expected = parent ? *parent * local : local;
    if (!Finite(expected) || PositionError(expected.translate, world.translate) > PositionTolerance(expected.translate, world.translate, rigScale) ||
        std::abs(expected.scale-world.scale) > (std::max)(1e-7f, expected.scale * 0.001f)) return false;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (std::abs(expected.rotate.entry[i][j]-world.rotate.entry[i][j]) > 0.001f) return false;
    return true;
}

inline bool OwnsPublication(const RE::NiTransform& capturedBody, const RE::NiTransform& currentBody,
    const std::array<RE::NiTransform, 3>& capturedBones, const std::array<RE::NiTransform, 3>& currentBones,
    const void* capturedParent, const void* currentParent) {
    return capturedParent == currentParent && capturedBody == currentBody && capturedBones == currentBones;
}

// Read-back checks use the actual published world pose, never the local write or
// the requested Rust output as evidence that rendering transforms were updated.
inline PublicationSample CheckPublication(const RE::NiTransform& expectedBody,
    const RE::NiTransform& actualBody, const std::array<RE::NiTransform, 3>& expectedBones,
    const std::array<RE::NiTransform, 3>& actualBones, float rigScale) {
    PublicationSample result{expectedBody.translate, actualBody.translate, 0, 0, true, true};
    if (!std::isfinite(rigScale) || rigScale <= 0) { result.matched = false; return result; }
    auto check = [&](const RE::NiTransform& expected, const RE::NiTransform& actual, bool bone) {
        if (!Finite(expected) || !Finite(actual)) { result.matched = false; return; }
        const float positionError = PositionError(expected.translate, actual.translate);
        const float scaleError = std::abs(expected.scale - actual.scale);
        if (bone) {
            result.maxBonePositionError = (std::max)(result.maxBonePositionError, positionError);
            result.maxBoneScaleError = (std::max)(result.maxBoneScaleError, scaleError);
        }
        if (positionError > PositionTolerance(expected.translate, actual.translate, rigScale) ||
            scaleError > (std::max)(1e-7f, expected.scale * 0.001f))
            result.matched = false;
        for (unsigned i = 0; i < 3; ++i)
            for (unsigned j = 0; j < 3; ++j)
                if (std::abs(expected.rotate.entry[i][j] - actual.rotate.entry[i][j]) > 0.001f)
                    result.matched = false;
    };
    check(expectedBody, actualBody, false);
    for (unsigned i = 0; i < expectedBones.size(); ++i) check(expectedBones[i], actualBones[i], true);
    return result;
}

// A shared world parent is expected. Neither controlled root may contain the other;
// otherwise translating the body would also move native arms or their camera node.
// Broken/cyclic or excessively deep parent chains fail closed.
template <class Node>
bool IndependentRoots(const Node* body, const Node* other) {
    if (!body || !other) return false;
    for (const auto* start : {body, other}) {
        const auto* forbidden = start == body ? other : body;
        const auto* node = start;
        unsigned depth = 0;
        for (; node && depth < 64; ++depth, node = node->parent)
            if (node == forbidden) return false;
        if (node) return false;
    }
    return true;
}

// Convert a horizontal WORLD displacement into the body's parent coordinates.
// Use the inverse linear transform, so parent translation and a previously stale
// world position cannot introduce a vertical drift. With a tilted parent local Z
// can change while world Z is preserved. Nothing is written on a validation failure.
inline bool Translate(const RE::NiPoint3& local, const RE::NiPoint3& displacement,
    const RE::NiTransform* parent, RE::NiPoint3& result) {
    if (!Finite(local) || !Finite(displacement) || displacement.z != 0.0f ||
        (parent && !Finite(*parent))) return false;
    const auto localDisplacement = parent ?
        (parent->rotate.Transpose() * displacement) / parent->scale : displacement;
    const auto translated = local + localDisplacement;
    if (!Finite(localDisplacement) || !Finite(translated)) return false;
    result = translated;
    return true;
}
}
