#include "../plugin/follow_transition.h"
#include <cassert>
#include <cmath>
int main() {
    AdvancedProfile run{};
    run.world = run.local = run.vertical = {1, 13, .27f, .73f, 650};
    auto sprint = run; sprint.world.min_rate = .7f; sprint.world.max_rate = .98f;
    sprint.offset[0] = 40;
    camera::FollowTransition t;
    t.Step(run, 8, .016f, .3f, true);
    auto first = t.Step(sprint, 12, 0, .3f, false);
    assert(first.world.min_rate == run.world.min_rate);
    assert(first.offset[0] == 40); // Offset interpolation still belongs to Rust.
    auto halfway = t.Step(sprint, 12, .15f, .3f, false);
    assert(std::abs(halfway.world.min_rate-.485f)<.0001f);
    auto retarget = t.Step(run, 8, 0, .3f, false);
    assert(retarget.world.min_rate == halfway.world.min_rate);
    t.Step(run, 8, .15f, .3f, false);
    auto end = t.Step(run, 8, .15f, .3f, false);
    assert(end.world.min_rate == run.world.min_rate);
    assert(t.Step(sprint, 12, 0, 0, false).world.min_rate == .7f);
    assert(t.Step(run, 8, 0, .3f, true).world.min_rate == .27f);
    auto curve = sprint; curve.world.curve = 1;
    assert(t.Step(curve, 12, 0, .3f, false).world.curve == 1);
    assert(t.Step(curve, 12, .016f, .3f, false).world.min_rate == .7f);
}
