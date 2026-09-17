#pragma once
#include "body_position.h"
#include <numbers>

namespace body_facing {
struct Sample {
    bool available = false;
    float viewYawDegrees = 0.0f;
    float bodyYawDegrees = 0.0f;
    // Body minus view, wrapped to [-180, 180). Positive turns toward view-right.
    float yawGapDegrees = 0.0f;
    RE::NiPoint3 anchorLocal{};
};

// Native NiTransform rotations are orthonormal. Transpose is their inverse;
// reject malformed/sheared bases instead of presenting misleading local values.
inline bool RigidRotation(const RE::NiMatrix3& rotation) {
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = i; j < 3; ++j) {
            float dot = 0.0f;
            for (unsigned k = 0; k < 3; ++k)
                dot += rotation.entry[k][i] * rotation.entry[k][j];
            const float expected = i == j ? 1.0f : 0.0f;
            if (!std::isfinite(dot) || std::abs(dot - expected) > 0.001f) return false;
        }
    const auto& r = rotation.entry;
    const float determinant = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1])
        - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
        + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    return std::isfinite(determinant) && std::abs(determinant - 1.0f) <= 0.002f;
}

// A read-only comparison, not a body-facing policy. Body local +Y is forward.
// World inverse removes the full native tilt/scale/translation from the anchor,
// independent of the body's parent. A rejected diagnostic never moves the scene.
inline Sample Compare(const std::array<float, 2>& viewHeading,
    const RE::NiTransform& bodyWorld, const RE::NiPoint3& anchorWorld) {
    if (!std::isfinite(viewHeading[0]) || !std::isfinite(viewHeading[1]) ||
        !body_position::Finite(bodyWorld) || !body_position::Finite(anchorWorld) ||
        !RigidRotation(bodyWorld.rotate)) return {};
    const float viewLength = std::hypot(viewHeading[0], viewHeading[1]);
    const float bodyX = bodyWorld.rotate.entry[0][1];
    const float bodyY = bodyWorld.rotate.entry[1][1];
    const float bodyLength = std::hypot(bodyX, bodyY);
    if (!std::isfinite(viewLength) || viewLength < 0.001f ||
        !std::isfinite(bodyLength) || bodyLength < 0.001f) return {};
    const auto displacement = anchorWorld - bodyWorld.translate;
    if (!body_position::Finite(displacement)) return {};
    const auto local = (bodyWorld.rotate.Transpose() * displacement) / bodyWorld.scale;
    if (!body_position::Finite(local)) return {};
    constexpr float degrees = 180.0f / std::numbers::pi_v<float>;
    const float viewYaw = std::atan2(viewHeading[0], viewHeading[1]) * degrees;
    const float bodyYaw = std::atan2(bodyX, bodyY) * degrees;
    float gap = bodyYaw - viewYaw;
    if (gap >= 180.0f) gap -= 360.0f;
    else if (gap < -180.0f) gap += 360.0f;
    return {true, viewYaw, bodyYaw, gap, local};
}
}
