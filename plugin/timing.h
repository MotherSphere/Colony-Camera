#pragma once
#include <algorithm>
#include <cstdint>

namespace timing {
// Microseconds of wall time in our callback. Native includes previously chained mods.
struct Sample {
    double total{}, native{}, collision{}, math{}, scene{};
    bool applied{};
};
struct Totals {
    std::uint32_t samples{}, applied{};
    double own{}, native{}, collision{}, math{}, scene{}, peakOwn{};
    void Add(const Sample& s) {
        ++samples;
        applied += s.applied;
        const auto overhead = (std::max)(0.0, s.total-s.native);
        own += overhead; native += s.native; collision += s.collision;
        math += s.math; scene += s.scene; peakOwn = (std::max)(peakOwn, overhead);
    }
};
}
