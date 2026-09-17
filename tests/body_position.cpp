#include "../plugin/body_position.h"
#include "../plugin/owned_value.h"
#include <cassert>
#include <limits>

namespace {
bool Close(const RE::NiPoint3& a, const RE::NiPoint3& b) {
    return std::abs(a.x-b.x) < 0.0001f && std::abs(a.y-b.y) < 0.0001f &&
        std::abs(a.z-b.z) < 0.0001f;
}
struct Node { Node* parent = nullptr; };
}

int main() {
    RE::NiPoint3 local{4,5,6}, result{-1,-2,-3};
    const RE::NiPoint3 delta{8,-12,0};
    assert(body_position::Translate(local, delta, nullptr, result));
    assert(Close(result, {12,-7,6}));

    // Translation and scale of a parent must not turn a horizontal displacement
    // into a world-height change, even when its local axes are tilted.
    RE::NiTransform parent;
    parent.translate = {123,456,789};
    parent.scale = 2;
    parent.rotate.entry[0][0] = 0; parent.rotate.entry[0][2] = 1;
    parent.rotate.entry[2][0] = -1; parent.rotate.entry[2][2] = 0;
    const auto originalWorld = parent * local;
    assert(body_position::Translate(local, delta, &parent, result));
    assert(Close(parent * result, originalWorld + delta));
    assert(result.z != local.z);
    assert(Close(parent * result, {originalWorld.x+8, originalWorld.y-12, originalWorld.z}));

    camera::OwnedValue<RE::NiPoint3> lease;
    auto current = local;
    assert(lease.Write(current, result));
    assert(lease.Restore(current));
    assert(current == local);
    assert(lease.Write(current, result));
    current.x += 1;
    const auto otherOwner = current;
    assert(!lease.Restore(current) && current == otherOwner);

    // A retained local value under a NEW parent is no longer the lease's field.
    // Do not restore a coordinate captured in another parent's coordinate system.
    Node oldParent, newParent;
    current = local;
    assert(lease.Write(current, result));
    assert(!body_position::RestoreTranslation(lease, current, &oldParent, &newParent));
    assert(current == result && !lease.Active());
    assert(!body_position::RestoreTranslation(lease, current, &oldParent, &oldParent));
    assert(current == result);
    current = local;
    assert(lease.Write(current, result));
    assert(body_position::RestoreTranslation(lease, current, &newParent, &newParent));
    assert(current == local);
    assert(lease.Write(current, result));
    assert(body_position::RestoreTranslation(lease, current, nullptr, nullptr));
    assert(current == local);

    // All invalid inputs preserve the output, including late arithmetic overflow.
    const auto before = result;
    const auto inf = std::numeric_limits<float>::infinity();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    assert(!body_position::Translate(local, {1,2,3}, &parent, result) && result == before);
    assert(!body_position::Translate(local, {inf,2,0}, &parent, result) && result == before);
    assert(!body_position::Translate({nan,2,3}, delta, &parent, result) && result == before);
    parent.scale = 0;
    assert(!body_position::Translate(local, delta, &parent, result) && result == before);
    parent.scale = std::numeric_limits<float>::denorm_min();
    assert(!body_position::Translate(local, delta, &parent, result) && result == before);
    parent.scale = 2;
    parent.rotate.entry[1][1] = inf;
    assert(!body_position::Translate(local, delta, &parent, result) && result == before);
    parent.rotate.entry[1][1] = 1;
    parent.translate.x = nan;
    assert(!body_position::Translate(local, delta, &parent, result) && result == before);

    // Native arms may share a world parent, but must not move with the body.
    Node world, body{&world}, arms{&world}, eye{&arms};
    assert(body_position::IndependentRoots(&body, &arms));
    assert(body_position::IndependentRoots(&body, &eye));
    assert(!body_position::IndependentRoots(&body, &body));
    assert(!body_position::IndependentRoots(&arms, &eye));
    assert(!body_position::IndependentRoots(&eye, &arms));
    assert(!body_position::IndependentRoots<Node>(&body, nullptr));
    body.parent = &body;
    assert(!body_position::IndependentRoots(&body, &arms));
}
