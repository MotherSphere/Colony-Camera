#pragma once
#include "core.h"

// Advanced following keeps its pre-collision history. Only the display/handoff
// candidate receives collision output; legacy base correction is unchanged.
inline void ApplyCollisionOutput(CameraState& candidate, bool advanced, const float* corrected) {
    for (unsigned i = 0; i < 3; ++i) {
        if (!advanced) candidate.base[i] += corrected[i] - candidate.position[i];
        candidate.position[i] = corrected[i];
    }
}
