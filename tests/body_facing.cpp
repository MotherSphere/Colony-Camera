#include "../plugin/body_facing.h"
#include <cassert>
#include <limits>

namespace {
constexpr float radians = std::numbers::pi_v<float> / 180.0f;
bool Close(float a, float b) { return std::abs(a-b) < 0.001f; }
bool Close(const RE::NiPoint3& a, const RE::NiPoint3& b) {
    return Close(a.x, b.x) && Close(a.y, b.y) && Close(a.z, b.z);
}
std::array<float, 2> Heading(float yaw) {
    return {std::sin(yaw*radians), std::cos(yaw*radians)};
}
RE::NiMatrix3 Rotation(float yaw, float pitch = 0.0f) {
    const float s = std::sin(yaw*radians), c = std::cos(yaw*radians);
    RE::NiMatrix3 turn;
    turn.entry[0][0] = c; turn.entry[0][1] = s;
    turn.entry[1][0] = -s; turn.entry[1][1] = c;
    RE::NiMatrix3 tilt;
    tilt.entry[1][1] = std::cos(pitch*radians); tilt.entry[1][2] = std::sin(pitch*radians);
    tilt.entry[2][1] = -std::sin(pitch*radians); tilt.entry[2][2] = std::cos(pitch*radians);
    return turn * tilt;
}
void Unavailable(const body_facing::Sample& sample) {
    assert(!sample.available && sample.viewYawDegrees == 0 && sample.bodyYawDegrees == 0 &&
        sample.yawGapDegrees == 0 && Close(sample.anchorLocal, {}));
}
}

int main() {
    RE::NiTransform body;
    const RE::NiPoint3 localAnchor{3, -9, 55};
    // The convention is atan2(X,Y): +Y=0, +X=90, -X=-90. Turning
    // body and view together preserves both facing agreement and anchor position.
    for (float yaw : {0.0f, 90.0f, -90.0f, 45.0f, 179.0f, -179.0f}) {
        body.rotate = Rotation(yaw);
        const auto sample = body_facing::Compare(Heading(yaw), body, body * localAnchor);
        assert(sample.available && Close(sample.viewYawDegrees, yaw) &&
            Close(sample.bodyYawDegrees, yaw) && Close(sample.yawGapDegrees, 0));
        assert(Close(sample.anchorLocal, localAnchor));
    }
    // Cross the yaw wrap without reporting a nearly full-circle discrepancy.
    body.rotate = Rotation(-179);
    auto sample = body_facing::Compare(Heading(179), body, body * localAnchor);
    assert(sample.available && Close(sample.yawGapDegrees, 2));
    body.rotate = Rotation(179);
    sample = body_facing::Compare(Heading(-179), body, body * localAnchor);
    assert(sample.available && Close(sample.yawGapDegrees, -2));
    body.rotate = Rotation(90);
    sample = body_facing::Compare(Heading(0), body, body * localAnchor);
    assert(sample.available && Close(sample.yawGapDegrees, 90));
    body.rotate = Rotation(-90);
    sample = body_facing::Compare(Heading(0), body, body * localAnchor);
    assert(sample.available && Close(sample.yawGapDegrees, -90));
    body.rotate = RE::NiMatrix3{};
    sample = body_facing::Compare({0, -1}, body, body * localAnchor);
    assert(sample.available && Close(sample.yawGapDegrees, -180));

    // The anchor's local coordinates remove native tilt and all cumulative
    // world scale/translation. No parent transform or previous sample is needed.
    for (float yaw : {-170.0f, 0.0f, 91.0f})
        for (float scale : {0.1f, 1.0f, 4.0f})
            for (const RE::NiPoint3 position : {RE::NiPoint3{}, RE::NiPoint3{-13, 42, 170}}) {
                body.rotate = Rotation(yaw, 30);
                body.scale = scale;
                body.translate = position;
                const auto normalized = Heading(yaw);
                sample = body_facing::Compare({7*normalized[0], 7*normalized[1]}, body, body * localAnchor);
                assert(sample.available && Close(sample.yawGapDegrees, 0) && Close(sample.anchorLocal, localAnchor));
            }

    body = RE::NiTransform{};
    Unavailable(body_facing::Compare({0, 0}, body, localAnchor));
    Unavailable(body_facing::Compare({0.0005f, 0}, body, localAnchor));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Unavailable(body_facing::Compare({nan, 1}, body, localAnchor));
    Unavailable(body_facing::Compare({1, inf}, body, localAnchor));
    Unavailable(body_facing::Compare({0, 1}, body, {0, nan, 0}));
    for (float invalidScale : {0.0f, -1.0f, nan, inf}) {
        body.scale = invalidScale;
        Unavailable(body_facing::Compare({0, 1}, body, localAnchor));
    }
    body = RE::NiTransform{};
    body.translate.x = inf;
    Unavailable(body_facing::Compare({0, 1}, body, localAnchor));
    body = RE::NiTransform{};
    body.rotate = Rotation(0, 90);
    Unavailable(body_facing::Compare({0, 1}, body, localAnchor));
    body.rotate = RE::NiMatrix3{};
    body.rotate.entry[0][1] = 0.5f;
    Unavailable(body_facing::Compare({0, 1}, body, localAnchor));
    body.rotate = RE::NiMatrix3{};
    body.rotate.entry[0][0] = -1;
    Unavailable(body_facing::Compare({0, 1}, body, localAnchor));
    body.rotate = RE::NiMatrix3{};
    body.rotate.entry[2][2] = nan;
    Unavailable(body_facing::Compare({0, 1}, body, localAnchor));
    body = RE::NiTransform{};
    body.scale = 1e-30f;
    Unavailable(body_facing::Compare({0, 1}, body, {1e30f, 0, 0}));
}
