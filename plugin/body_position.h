#pragma once
#include "owned_value.h"
#include <cstddef>
#include <initializer_list>
#include <RE/N/NiTransform.h>
#include <cmath>

namespace body_position {
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
