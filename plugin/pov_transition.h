#pragma once
#include "core.h"
#include <algorithm>
#include <cmath>

namespace pov {
inline CameraState Blend(const CameraState& origin, float startZoom, float zoom,
    float minimumZoom, const CameraFrame& frame) {
    CameraState result{};
    if (!std::isfinite(startZoom) || !std::isfinite(zoom)
        || !std::isfinite(minimumZoom) || !std::isfinite(frame.world_fov)) return result;
    // Follow native zoom progress, not a second timer or the preset's minimum
    // distance. The correction reaches exactly zero at the native endpoint.
    const float span = startZoom-minimumZoom;
    const float weight = span > .00001f ? std::clamp((zoom-minimumZoom)/span, 0.0f, 1.0f) : 0.0f;
    for (unsigned i=0; i<3; ++i) {
        const float correction = origin.initialized ? origin.position[i]-origin.native[i] : 0.0f;
        result.native[i] = frame.position[i];
        result.position[i] = frame.position[i] + correction*weight;
        if (!std::isfinite(result.native[i]) || !std::isfinite(result.position[i])) return {};
    }
    const float delta = origin.initialized ? origin.fov_delta*weight : 0.0f;
    if (!std::isfinite(delta)) return {};
    result.fov = std::clamp(frame.world_fov+delta, 30.0f, 150.0f);
    result.fov_delta = result.fov-frame.world_fov;
    result.initialized = 1;
    return result;
}
}
