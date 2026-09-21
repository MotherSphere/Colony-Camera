#pragma once
#include "core.h"
#include <algorithm>
#include <cmath>
namespace camera {
struct FollowTransition {
    AdvancedProfile current{}, from{};
    unsigned profile{};
    float elapsed{};
    bool initialized{};
    static Follow Blend(Follow a, Follow b, float t) {
        // Enum curve IDs cannot be interpolated. Preserve existing semantics
        // for curve/enable changes; blend coefficients only within one curve.
        if (t >= 1 || a.curve != b.curve || a.enabled != b.enabled) return b;
        b.min_rate = a.min_rate + (b.min_rate-a.min_rate)*t;
        b.max_rate = a.max_rate + (b.max_rate-a.max_rate)*t;
        b.distance = a.distance + (b.distance-a.distance)*t;
        return b;
    }
    AdvancedProfile Step(const AdvancedProfile& target, unsigned selected, float dt, float duration, bool reset) {
        if (!initialized || reset || !std::isfinite(dt) || dt < 0 || dt > .25f
            || !std::isfinite(duration) || duration <= 0) {
            current = from = target; profile = selected; elapsed = duration; initialized = true;
            return target;
        }
        if (profile != selected) { from = current; profile = selected; elapsed = 0; }
        elapsed = (std::min)(elapsed+dt, duration);
        const float t = elapsed/duration;
        current = target;
        current.world = Blend(from.world, target.world, t);
        current.local = Blend(from.local, target.local, t);
        current.vertical = Blend(from.vertical, target.vertical, t);
        return current;
    }
};
}
