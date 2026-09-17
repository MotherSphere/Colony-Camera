#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace timing {
// Microseconds of wall time in our callback. Native includes previously chained mods.
struct Sample {
    double total{}, native{}, collision{}, math{}, scene{};
    bool applied{};
};
struct Totals {
    std::uint32_t samples{}, applied{};
    double own{}, native{}, collision{}, math{}, scene{}, peakOwn{};

private:
    static constexpr std::size_t capacity = 64;
    std::array<double, capacity> recentOwn{};
    std::size_t recentCount{}, next{};

public:
    void Add(const Sample& s) {
        // Invalid measurements must not poison the entire report. A rejected
        // sample changes neither cumulative totals nor the bounded window.
        for (const auto value : {s.total, s.native, s.collision, s.math, s.scene}) {
            if (!std::isfinite(value) || value < 0.0) return;
        }
        if (samples == (std::numeric_limits<std::uint32_t>::max)()) return;
        const auto overhead = (std::max)(0.0, s.total-s.native);
        const std::array sums{own + overhead, native + s.native,
            collision + s.collision, math + s.math, scene + s.scene};
        for (const auto value : sums) if (!std::isfinite(value)) return;
        ++samples;
        applied += s.applied;
        own = sums[0]; native = sums[1]; collision = sums[2];
        math = sums[3]; scene = sums[4]; peakOwn = (std::max)(peakOwn, overhead);
        recentOwn[next] = overhead;
        next = (next + 1) % capacity;
        recentCount = (std::min)(recentCount + 1, capacity);
    }
    [[nodiscard]] std::size_t SampledCount() const { return recentCount; }

    // Nearest-rank quantile of the last <=64 accepted callback own times in
    // microseconds: ceil(q*N), with q=0 selecting the minimum. Finite q is
    // clamped to [0,1]; empty samples or nonfinite q return zero. Sorting a fixed
    // stack copy leaves the chronological ring unchanged and allocates nothing.
    // These sampled callback tails are not full-frame/FPS or GPU percentiles.
    [[nodiscard]] double SampledPercentile(double quantile) const {
        if (recentCount == 0 || !std::isfinite(quantile)) return 0.0;
        auto sorted = recentOwn;
        std::sort(sorted.begin(), sorted.begin() + recentCount);
        const auto rank = static_cast<std::size_t>(std::ceil(std::clamp(quantile, 0.0, 1.0) * recentCount));
        return sorted[rank == 0 ? 0 : rank - 1];
    }
};
}
