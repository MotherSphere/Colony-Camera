#include "../plugin/pov_transition.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

int main() {
    CameraState origin{};
    origin.initialized = 1;
    origin.native[1] = -200;
    origin.position[0] = 25;
    origin.position[1] = -580;
    origin.position[2] = 50;
    origin.fov_delta = -10;
    CameraFrame frame{};
    frame.world_fov = 80;
    frame.position[1] = -20;
    auto result = pov::Blend(origin, 1.2f, .2f, .2f, frame);
    assert(result.initialized && result.position[0] == 0 && result.position[1] == -20);
    assert(result.position[2] == 0 && result.fov == 80 && result.fov_delta == 0);
    result = pov::Blend(origin, 1.2f, 1.2f, .2f, frame);
    assert(result.position[0] == 25 && result.position[1] == -400 && result.position[2] == 50);
    result = pov::Blend(origin, 1.2f, .7f, .2f, frame);
    assert(std::abs(result.position[0]-12.5f) < .001f);
    assert(std::abs(result.position[1]+210) < .001f);
    assert(std::abs(result.fov-75) < .001f);
    for (float start : {.2f, .200001f, 1.2f, 10.0f}) {
        result = pov::Blend(origin, start, -.1f, .2f, frame);
        assert(result.position[1] == -20 && result.fov == 80);
    }
    result = pov::Blend({}, 1.2f, .7f, .2f, frame);
    assert(result.position[1] == -20 && result.fov == 80);
    assert(!pov::Blend(origin, 1.2f, std::numeric_limits<float>::quiet_NaN(), .2f, frame).initialized);
}
