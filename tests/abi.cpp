#include "../plugin/core.h"
#include "../plugin/runtime.h"
#include "../plugin/scene.h"
#include "../plugin/timing.h"
#include <cassert>
#include <RE/N/NiTransform.h>
#include <cmath>
#include <cstring>
#include <limits>
int main() {
    timing::Totals measured;
    measured.Add({1000, 800, 100, 20, 30, true});
    measured.Add({600, 400, 0, 0, 0, false});
    assert(measured.samples == 2 && measured.applied == 1);
    assert(measured.own / measured.samples == 200);
    assert(measured.native / measured.samples == 600);
    assert(measured.collision == 100 && measured.peakOwn == 200);
    measured = {};
    assert(measured.samples == 0 && measured.own == 0);

    // Regression: writing only ThirdPersonState leaves the rendered camera unmoved.
    RE::NiPoint3 statePos{10,20,30}, local{10,20,30}, world{10,20,30}, rendered{11,22,33};
    scene::PublishPosition(statePos, local, world, rendered, {40,50,60});
    assert(statePos.x == 40 && local.y == 50 && world.z == 60);
    assert(rendered.x == 41 && rendered.y == 52 && rendered.z == 63);
    scene::PublishPosition(statePos, local, world, rendered, {40,50,60});
    assert(rendered.x == 41 && rendered.y == 52 && rendered.z == 63);

    // A camera root may be attached to a translated, rotated and scaled scene parent.
    RE::NiTransform parent;
    parent.translate = {100,200,300};
    parent.scale = 2;
    parent.rotate.entry[0][0] = 0; parent.rotate.entry[0][1] = -1;
    parent.rotate.entry[1][0] = 1; parent.rotate.entry[1][1] = 0;
    assert(scene::PublishPosition(statePos, local, world, rendered, {80,240,360}, &parent));
    assert(local.x == 20 && local.y == 10 && local.z == 30);
    assert(statePos.x == 80 && world.y == 240 && rendered.z == 363);
    const auto saved = local;
    parent.scale = 0;
    assert(!scene::PublishPosition(statePos, local, world, rendered, {0,0,0}, &parent));
    assert(local.x == saved.x && world.y == 240 && statePos.x == 80 && rendered.z == 363);

    // Late validation failure must not partially move any scene position.
    const RE::NiPoint3 before[]{statePos, local, world, rendered};
    parent.scale = 2;
    parent.translate.x = std::numeric_limits<float>::quiet_NaN();
    assert(!scene::PublishPosition(statePos, local, world, rendered, {0,0,0}, &parent));
    const RE::NiPoint3 after[]{statePos, local, world, rendered};
    assert(std::memcmp(before, after, sizeof(before)) == 0);

    for (auto entry : {runtime::begin, runtime::end, runtime::update, runtime::collision, runtime::matrix}) {
        auto bytes = entry.prefix;
        assert(entry.matches(bytes.data()));
        for (std::size_t i=0; i<bytes.size(); ++i) {
            bytes[i] ^= 0x80;
            assert(!entry.matches(bytes.data()));
            bytes[i] ^= 0x80;
        }
    }
    auto c = cc_defaults();
    assert(c.enabled == 1 && c.keys[0] == 0x42);
    CameraFrame f{{100,200,300},{1,0,0,0},0.016f,1};
    auto s = cc_step({}, f, c.profiles[0]);
    assert(s.initialized == 1 && s.position[0] == 100 && s.position[2] == 300);
    const char* good = "[combat]\nx=42\n";
    assert(cc_parse_config(reinterpret_cast<const unsigned char*>(good), std::strlen(good), &c) == 0);
    assert(c.profiles[1].offset[0] == 42);
    auto previous = c;
    const char* bad = "[combat]\nx=NaN";
    assert(cc_parse_config(reinterpret_cast<const unsigned char*>(bad), std::strlen(bad), &c) != 0);
    assert(std::memcmp(&previous, &c, sizeof(c)) == 0);
    assert(cc_parse_config(nullptr, 0, &c) != 0);
}
