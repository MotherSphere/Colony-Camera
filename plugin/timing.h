#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <limits>

namespace timing {
// Restore/prepare are inclusive; lookup and node-update timers are nested
// diagnostics, not additional callback time. The final three fields are counts.
enum BodyField { BodyRestore, BodyPrepare, BodyLookup, BodyUpdate, ArmsUpdate,
    BodyUpdates, ArmsUpdates, ArmLookups, BodyFieldCount };
using Body = std::array<double, BodyFieldCount>;

// Disabled scopes never read the clock. Stop is idempotent for section boundaries
// and the destructor closes early-return paths automatically.
class Scope {
    using Clock = std::chrono::steady_clock;
    double* total_;
    Clock::time_point start_;
public:
    Scope(Body* body, BodyField field) : total_(body ? &(*body)[field] : nullptr),
        start_(total_ ? Clock::now() : Clock::time_point{}) {}
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { Stop(); }
    void Stop() {
        if (!total_) return;
        *total_ += std::chrono::duration<double, std::micro>(Clock::now() - start_).count();
        total_ = nullptr;
    }
};

// Microseconds of wall time in our callback. Native includes previously chained mods.
struct Sample {
    double total{}, native{}, collision{}, math{}, scene{};
    bool applied{};
    Body body{};
};
struct Totals {
    std::uint32_t samples{}, applied{};
    double own{}, native{}, collision{}, math{}, scene{}, peakOwn{};
    Body body{};

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
        Body bodySums{};
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (!std::isfinite(s.body[i]) || s.body[i] < 0) return;
            bodySums[i] = body[i] + s.body[i];
            if (!std::isfinite(bodySums[i])) return;
        }
        body = bodySums;
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
