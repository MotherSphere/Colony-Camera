#include "../plugin/timing.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        timing::Totals totals;
        Require(totals.SampledCount() == 0 && totals.SampledPercentile(0.95) == 0,
            "Empty timing window must return zero");
        totals.Add({1000, 800, 100, 20, 30, true});
        totals.Add({650, 600, 0, 0, 0, false});
        totals.Add({400, 500, 0, 0, 0, false});
        Require(totals.samples == 3 && totals.applied == 1 && totals.SampledCount() == 3,
            "Applied and sampled counts disagree");
        Require(totals.own == 250 && totals.native == 1900 && totals.peakOwn == 200,
            "Own callback time must exclude previously chained work and clamp negative differences");
        Require(totals.collision == 100 && totals.math == 20 && totals.scene == 30,
            "Per-stage totals changed");
        Require(totals.SampledPercentile(0.5) == 50 && totals.SampledPercentile(0.95) == 200,
            "Mixed sample quantiles are incorrect");
        Require(totals.SampledPercentile(-1) == 0 && totals.SampledPercentile(2) == 200,
            "Finite out-of-range quantiles must select a boundary");
        Require(totals.SampledPercentile(std::numeric_limits<double>::quiet_NaN()) == 0
            && totals.SampledPercentile(std::numeric_limits<double>::infinity()) == 0,
            "Nonfinite quantiles must not index the sample buffer");

        // Querying percentiles must not reorder the chronological ring before
        // later writes replace its oldest samples.
        totals = {};
        for (unsigned index = 1; index <= 64; ++index) totals.Add({double(index), 0, 0, 0, 0, false});
        Require(totals.SampledPercentile(0.50) == 32 && totals.SampledPercentile(0.95) == 61
            && totals.SampledPercentile(0.99) == 64, "Nearest-rank p50/p95/p99 are incorrect");
        for (unsigned index = 65; index <= 80; ++index) totals.Add({double(index), 0, 0, 0, 0, false});
        Require(totals.samples == 80 && totals.SampledCount() == 64 && totals.own == 3240,
            "Cumulative totals and bounded sample window must be independent");
        Require(totals.SampledPercentile(0) == 17 && totals.SampledPercentile(0.50) == 48
            && totals.SampledPercentile(0.95) == 77 && totals.SampledPercentile(0.99) == 80,
            "Ring rollover retained old samples or changed nearest-rank ordering");
        timing::Totals descending;
        for (unsigned value = 64; value > 0; --value) descending.Add({double(value), 0, 0, 0, 0, false});
        Require(descending.SampledPercentile(0.50) == 32, "Descending sample quantile is incorrect");
        descending.Add({65, 0, 0, 0, 0, false});
        Require(descending.SampledPercentile(0) == 1 && descending.SampledPercentile(0.50) == 32
            && descending.SampledPercentile(0.99) == 65,
            "Percentile query reordered the ring and evicted the wrong sample");

        timing::Body measured{};
        double stopped = 0;
        {
            timing::Scope scope(&measured, timing::BodyPrepare);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(20);
            while (std::chrono::steady_clock::now() < deadline) {}
            scope.Stop();
            stopped = measured[timing::BodyPrepare];
            Require(stopped > 0, "Enabled scope did not measure its section");
            scope.Stop();
        }
        Require(measured[timing::BodyPrepare] == stopped,
            "Stopping or destroying a closed scope counted the section again");
        const auto beforeDisabled = measured;
        { timing::Scope disabled(nullptr, timing::BodyPrepare); disabled.Stop(); }
        Require(measured == beforeDisabled, "Disabled scope changed measurements");
        auto earlyReturn = [&]() {
            timing::Scope scope(&measured, timing::BodyRestore);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(20);
            while (std::chrono::steady_clock::now() < deadline) {}
            return;
        };
        earlyReturn();
        Require(measured[timing::BodyRestore] > 0, "Early return lost its timing section");

        timing::Sample bodySample{100, 20, 0, 5, 60, true};
        bodySample.body[timing::BodyRestore] = 15;
        bodySample.body[timing::BodyPrepare] = 10;
        bodySample.body[timing::BodyLookup] = 2;
        bodySample.body[timing::BodyUpdate] = 40;
        bodySample.body[timing::ArmsUpdate] = 8;
        bodySample.body[timing::BodyUpdates] = 2;
        bodySample.body[timing::ArmsUpdates] = 1;
        bodySample.body[timing::ArmLookups] = 2;
        timing::Totals bodyTotals;
        bodyTotals.Add(bodySample);
        bodyTotals.Add(bodySample);
        for (std::size_t i = 0; i < bodySample.body.size(); ++i)
            Require(bodyTotals.body[i] == bodySample.body[i] * 2,
                "Body timing/count fields were lost while aggregating");
        bodySample.body[timing::BodyPrepare] = -1;
        bodyTotals.Add(bodySample);
        Require(bodyTotals.samples == 2 && bodyTotals.body[timing::BodyRestore] == 30,
            "Invalid body measurements partially changed totals");
        bodyTotals = {};
        for (auto value : bodyTotals.body) Require(value == 0, "Body totals survived reset");

        const auto previous = totals;
        for (const auto invalid : {
            timing::Sample{std::numeric_limits<double>::quiet_NaN(), 0, 0, 0, 0, true},
            timing::Sample{100, std::numeric_limits<double>::infinity(), 0, 0, 0, true},
            timing::Sample{100, 10, -1, 0, 0, true},
            timing::Sample{100, 10, 0, std::numeric_limits<double>::quiet_NaN(), 0, true},
            timing::Sample{100, 10, 0, 0, -1, true}}) totals.Add(invalid);
        Require(totals.samples == previous.samples && totals.applied == previous.applied
            && totals.own == previous.own && totals.native == previous.native
            && totals.SampledPercentile(0.5) == previous.SampledPercentile(0.5),
            "Invalid measurements partially changed timing statistics");

        totals = {};
        totals.Add({(std::numeric_limits<double>::max)(), 0, 0, 0, 0, false});
        totals.Add({(std::numeric_limits<double>::max)(), 0, 0, 0, 0, true});
        Require(totals.samples == 1 && totals.applied == 0 && totals.SampledCount() == 1
            && std::isfinite(totals.own), "Overflowing totals must reject the entire sample");
        totals = {};
        Require(totals.samples == 0 && totals.applied == 0 && totals.own == 0 && totals.peakOwn == 0
            && totals.SampledCount() == 0 && totals.SampledPercentile(0.99) == 0,
            "Reset did not clear all timing history");
        std::cout << "Sampled callback timing statistics passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Timing test failed: " << error.what() << '\n';
        return 1;
    }
}
