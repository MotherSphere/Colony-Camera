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
RE::NiMatrix3 NoRollView(float yaw, float pitch) {
    const float s = std::sin(yaw), c = std::cos(yaw);
    const float p = std::sin(pitch), q = std::cos(pitch);
    RE::NiMatrix3 rotation;
    rotation.entry[0][0] = s*q; rotation.entry[0][1] = -s*p; rotation.entry[0][2] = c;
    rotation.entry[1][0] = c*q; rotation.entry[1][1] = -c*p; rotation.entry[1][2] = -s;
    rotation.entry[2][0] = p; rotation.entry[2][1] = q; rotation.entry[2][2] = 0;
    return rotation;
}
}

int main() {
    // Published view heading must remain stable through yaw and look-up/down,
    // including a vertical view where projected forward has no usable length.
    std::array<float, 2> heading{};
    RE::NiMatrix3 viewRotation;
    for (float yaw : {0.0f, 0.7f, 1.57079633f, 3.14159265f, -1.2f}) {
        for (float pitch : {0.0f, 0.8f, -1.1f, 1.57079633f, -1.57079633f}) {
            const float s = std::sin(yaw), c = std::cos(yaw);
            viewRotation = NoRollView(yaw, pitch);
            assert(body_position::ViewHeading(viewRotation, heading));
            assert(std::abs(heading[0]-s) < 0.0001f && std::abs(heading[1]-c) < 0.0001f);
        }
        // Cross both vertical poles and the previous projected-forward fallback
        // threshold at about 90.0573 degrees. A default 12-unit backset must not
        // jump by 24 units as the camera moves another hundredth of a degree.
        for (float sign : {-1.0f, 1.0f}) {
            RE::NiPoint3 previousOffset{};
            for (int step = -20; step <= 20; ++step) {
                const float pitch = sign * (1.57079633f + step * 0.000174532925f);
                viewRotation = NoRollView(yaw, pitch);
                assert(body_position::ViewHeading(viewRotation, heading));
                const RE::NiPoint3 offset{-12*heading[0], -12*heading[1], 0};
                assert(Close(offset, {-12*std::sin(yaw), -12*std::cos(yaw), 0}));
                if (step > -20) assert(Close(offset, previousOffset));
                previousOffset = offset;
            }
        }
    }

    // A rolled basis can put screen-right vertically. The explicit fallback is
    // then the horizontal forward direction; this is not a general roll-removal
    // guarantee. Identity has forward +X and screen-right +Z in NiCamera's basis.
    viewRotation = RE::NiMatrix3{};
    assert(body_position::ViewHeading(viewRotation, heading));
    assert(heading[0] == 1 && heading[1] == 0);
    const auto previousHeading = heading;
    viewRotation.entry[2][2] = std::numeric_limits<float>::quiet_NaN();
    assert(!body_position::ViewHeading(viewRotation, heading) && heading == previousHeading);
    viewRotation = NoRollView(0.7f, 0.8f);
    viewRotation.entry[0][2] = std::numeric_limits<float>::infinity();
    assert(!body_position::ViewHeading(viewRotation, heading) && heading == previousHeading);
    for (auto& row : viewRotation.entry) for (auto& value : row) value = 0;
    assert(!body_position::ViewHeading(viewRotation, heading) && heading == previousHeading);
    viewRotation.entry[0][0] = 0.0005f;
    assert(!body_position::ViewHeading(viewRotation, heading) && heading == previousHeading);

    // Sheathed idle preserves full body arms; fists/weapons readied and an
    // equipped torch need the native equipment rig without duplicate body arms.
    const auto idle = body_position::SelectArms(false, false);
    assert(idle.hideNative && idle.boneFactors[0] < 0.001f &&
        idle.boneFactors[1] == 1 && idle.boneFactors[2] == 1);
    for (const auto policy : {body_position::SelectArms(true, false),
            body_position::SelectArms(false, true), body_position::SelectArms(true, true)}) {
        assert(!policy.hideNative && policy.boneFactors[0] == idle.boneFactors[0] &&
            policy.boneFactors[1] < 0.001f && policy.boneFactors[2] < 0.001f);
    }
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

    // Immediately restored locals require a fresh native world pose before the
    // next alignment. Reject stale rendered root/bone worlds rather than feeding
    // our previous displacement or scale back into the next frame.
    RE::NiTransform nativeLocal;
    nativeLocal.translate = local;
    const auto nativeWorld = parent * nativeLocal;
    assert(body_position::WorldMatchesLocal(nativeLocal, nativeWorld, &parent, 2));
    auto staleWorld = nativeWorld;
    staleWorld.translate.y += 12;
    assert(!body_position::WorldMatchesLocal(nativeLocal, staleWorld, &parent, 2));
    staleWorld = nativeWorld;
    staleWorld.scale *= 0.0001f;
    assert(!body_position::WorldMatchesLocal(nativeLocal, staleWorld, &parent, 2));

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

    // Requested local values are not proof of world publication. A selective pass
    // leaving either the root position or bone scale unchanged must be rejected.
    RE::NiTransform expectedRoot;
    expectedRoot.translate = {100,200,300};
    std::array<RE::NiTransform, 3> expectedBones{};
    for (auto& bone : expectedBones) { bone.translate = {100,200,420}; bone.scale = 0.0001f; }
    auto actualBones = expectedBones;
    auto actualRoot = expectedRoot;
    assert(body_position::OwnsPublication(expectedRoot, actualRoot, expectedBones, actualBones, &world, &world));
    assert(!body_position::OwnsPublication(expectedRoot, actualRoot, expectedBones, actualBones, &oldParent, &newParent));
    auto publication = body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1);
    assert(publication.available && publication.matched);
    actualRoot.translate.y += 12;
    assert(!body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1).matched);
    actualRoot = expectedRoot;
    actualBones[1].scale = 1;
    assert(!body_position::OwnsPublication(expectedRoot, actualRoot, expectedBones, actualBones, &world, &world));
    publication = body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1);
    assert(!publication.matched && publication.maxBoneScaleError > 0.9f);
    actualBones = expectedBones;
    actualBones[2].translate.x += 2;
    publication = body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1);
    assert(!publication.matched && publication.maxBonePositionError == 2);
    actualBones = expectedBones;
    actualBones[0].translate.z += 0.005f;
    // Publication tolerance is not ownership tolerance: even a small later
    // write belongs to another producer and must not authorize a rebuild.
    assert(!body_position::OwnsPublication(expectedRoot, actualRoot, expectedBones, actualBones, &world, &world));
    assert(body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1).matched);
    actualBones[0].scale = nan;
    assert(!body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1).matched);

    // Far-from-origin float precision can exceed the near-origin0.01 tolerance.
    // Accept a single representable step, while rejecting a real stale shift.
    expectedRoot.translate = {250000,250000,100};
    actualRoot = expectedRoot;
    actualRoot.translate.x = std::nextafter(actualRoot.translate.x, inf);
    actualBones = expectedBones;
    assert(actualRoot.translate.x-expectedRoot.translate.x > 0.01f);
    assert(body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1).matched);
    actualRoot.translate.x = expectedRoot.translate.x + 1;
    assert(!body_position::CheckPublication(expectedRoot, actualRoot, expectedBones, actualBones, 1).matched);
    assert(body_position::PositionTolerance({10000000,0,0}, {10000000,0,0}, 1) == 0.25f);
}
