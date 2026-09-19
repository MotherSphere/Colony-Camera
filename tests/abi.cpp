#include "../plugin/core.h"
#include "../plugin/runtime.h"
#include "../plugin/scene.h"
#include "../plugin/timing.h"
#include <cassert>
#include <RE/N/NiTransform.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
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
    assert(c.first_person_enabled == 0 && c.body_alignment.alignment_enabled == 1);
    assert(c.third_person_enabled == 1);
    assert(c.body_alignment.body_backset == 8 && c.body_alignment.body_side == 0);
    BodyAlignmentFrame body{{0,0,130},{0,0,125},{0,2},1,{0,3,129},1,{0,0,0},
        {1,0,0,0,1,0,0,0,1},0};
    auto alignment = cc_align_body(body, c.body_alignment);
    assert(alignment.valid == 1 && alignment.translation[0] == 0 && alignment.translation[1] == -18);
    assert(alignment.translation[2] == 0 && alignment.vertical_error == 1);
    body.movement = 2;
    body.head[0] = 3; body.head[2] = 130;
    body.eye[0] = 3; body.eye[1] = 0; body.eye[2] = 130;
    alignment = cc_align_body(body, c.body_alignment);
    assert(alignment.valid == 1 && alignment.translation[0] == -3 && alignment.translation[1] == -11);
    body.eye_available = 0;
    assert(cc_align_body(body, c.body_alignment).valid == 0);
    CameraFrame f{{100,200,300},{1,0,0,0},0.016f,1,75};
    auto s = cc_step({}, f, c.profiles[0]);
    assert(s.initialized == 1 && s.position[0] == 100 && s.position[2] == 300);
    auto adjusted = c.profiles[0];
    adjusted.fov_offset = 12;
    s = cc_step({}, f, adjusted);
    assert(s.fov == 87 && s.fov_delta == 12);
    f.reset = 0; f.world_fov = 55;
    s = cc_step(s, f, adjusted);
    assert(s.fov == 67 && s.fov_delta == 12);
    const char* good = "[combat]\nx=42\n[third_person]\nenabled=false\n[first_person]\nenabled=true\n";
    assert(cc_parse_config(reinterpret_cast<const unsigned char*>(good), std::strlen(good), &c) == 0);
    assert(c.profiles[1].offset[0] == 42);
    assert(c.third_person_enabled == 0 && c.first_person_enabled == 1);
    auto previous = c;
    const char* bad = "[combat]\nx=NaN";
    assert(cc_parse_config(reinterpret_cast<const unsigned char*>(bad), std::strlen(bad), &c) != 0);
    assert(std::memcmp(&previous, &c, sizeof(c)) == 0);
    assert(cc_parse_config(nullptr, 0, &c) != 0);
    assert(cc_validate_config(&c) == 0);
    assert(cc_validate_config(nullptr) == 1);
    std::size_t required = 0;
    assert(cc_serialize_config(&c, nullptr, 0, &required) == 4 && required > 0);
    std::vector<unsigned char> serialized(required);
    assert(cc_serialize_config(&c, serialized.data(), serialized.size(), &required) == 0);
    CameraConfig roundtrip{};
    assert(cc_parse_config(serialized.data(), required, &roundtrip) == 0);
    assert(std::memcmp(&c, &roundtrip, sizeof(c)) == 0);
    // Rejected pointer layouts must not touch memory across the ABI.
    alignas(CameraConfig) unsigned char unaligned[sizeof(CameraConfig) + 1]{};
    assert(cc_parse_config(serialized.data(), required, reinterpret_cast<CameraConfig*>(unaligned + 1)) == 1);
    assert(cc_parse_config(reinterpret_cast<const unsigned char*>(&c), sizeof(c), &c) == 1);
    assert(cc_serialize_config(&c, reinterpret_cast<unsigned char*>(&c), sizeof(c), &required) == 1);

    // Check the pointer ABI with nonzero fields across its complete layout.
    ThirdOptions options{};
    assert(cc_advanced_defaults(&options) == 0);
    options.enabled = 1;
    options.profiles[10].offset[0] = 28;
    options.profiles[10].fov = 8;
    assert(cc_advanced_validate(&options) == 0);
    AdvancedFrame advancedFrame{{{10, -100, 15}, {1,0,0,0}, .016f, 1, 75},
        {0,0,0}, 2, 2, 0, 10, 15, 0};
    AdvancedState initial{}, advanced{};
    assert(cc_step_advanced(&initial, &advancedFrame, &options, &advanced) == 0);
    assert(advanced.initialized == 1 && advanced.position[0] == 28 && advanced.position[2] == 0);
    assert(advanced.fov == 83 && advanced.native[0] == 10);
    assert(cc_step_advanced(&advanced, &advancedFrame, &options, &advanced) == 1);
    // Exercise all newly appended fields across the C++/Rust boundary.
    options.preset_geometry = 1; options.min_distance = 250; options.zoom_scale = 20;
    options.profiles[10].offset[1] = 140; options.profiles[10].offset[2] = 50;
    advancedFrame.focus[2] = 120; advancedFrame.zoom = 2;
    assert(cc_step_advanced(&initial, &advancedFrame, &options, &advanced) == 0);
    assert(advanced.position[0] == 28 && advanced.position[1] == -150 && advanced.position[2] == 170);
    options.profiles[10].world.curve = 99;
    const auto intact = advanced;
    assert(cc_step_advanced(&initial, &advancedFrame, &options, &advanced) == 3);
    assert(std::memcmp(&intact, &advanced, sizeof(advanced)) == 0);

    CameraContext context{CC_THIRD_PERSON, CC_AVAILABLE | CC_CONTROLS | CC_WEAPON_DRAWN,
        1, 0, 0, 0, 1};
    auto decision = cc_coordinate({}, context);
    assert(decision.owner == CC_THIRD_PERSON && decision.profile == CC_COMBAT && decision.reset == 1);
    context.flags |= CC_AIMING;
    decision = cc_coordinate(decision.next, context);
    assert(decision.profile == CC_AIM && decision.reset == 0);
    context.flags |= CC_DEAD;
    decision = cc_coordinate(decision.next, context);
    assert(decision.owner == CC_NATIVE && decision.reason == CC_DEATH && decision.reset == 1);
    context = {CC_FIRST_PERSON, CC_AVAILABLE | CC_CONTROLS, 1, 1, 0, 0, 0};
    decision = cc_coordinate(decision.next, context);
    assert(decision.owner == CC_NATIVE && decision.reason == CC_FIRST_PERSON_UNAVAILABLE);
    context.first_person_ready = 1;
    context.flags |= CC_AIMING | CC_WEAPON_DRAWN;
    decision = cc_coordinate(decision.next, context);
    assert(decision.owner == CC_FIRST_PERSON && decision.profile == CC_EXPLORATION);
    context.camera_mode = CC_THIRD_PERSON;
    decision = cc_coordinate(decision.next, context);
    assert(decision.owner == CC_NATIVE && decision.reason == CC_THIRD_PERSON_DISABLED);
    context.third_person_enabled = 1;
    decision = cc_coordinate(decision.next, context);
    assert(decision.owner == CC_THIRD_PERSON && decision.profile == CC_AIM);
}
