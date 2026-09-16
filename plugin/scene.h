#pragma once
#include <RE/N/NiTransform.h>
#include <cmath>

namespace scene {
inline bool PublishPosition(RE::NiPoint3& state, RE::NiPoint3& local,
    RE::NiPoint3& world, RE::NiPoint3& rendered, const RE::NiPoint3& position,
    const RE::NiTransform* parent = nullptr) {
    if (parent && (!std::isfinite(parent->scale) || parent->scale == 0.0f)) return false;
    const auto localPosition = parent ? parent->Invert() * position : position;
    const RE::NiPoint3 renderedPosition{rendered.x + position.x - world.x,
        rendered.y + position.y - world.y, rendered.z + position.z - world.z};
    for (const auto& p : {position, localPosition, renderedPosition})
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    // The state and collision result are world positions; only the root's local
    // translation is expressed in its parent's coordinates. Preserve child offsets.
    local = localPosition;
    state = world = position;
    rendered = renderedPosition;
    return true;
}
}
