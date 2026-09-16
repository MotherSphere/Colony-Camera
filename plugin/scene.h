#pragma once
#include <RE/N/NiPoint3.h>

namespace scene {
// For Skyrim's unparented camera root. Preserve any child-camera translation.
inline void PublishPosition(RE::NiPoint3& state, RE::NiPoint3& local,
    RE::NiPoint3& world, RE::NiPoint3& rendered, const RE::NiPoint3& position) {
    rendered.x += position.x - world.x;
    rendered.y += position.y - world.y;
    rendered.z += position.z - world.z;
    state = local = world = position;
}
}
