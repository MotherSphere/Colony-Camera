#include "../plugin/collision_output.h"
#include "../plugin/advanced.h"
#include <cassert>
#include <cstring>
int main() {
    CameraState output{};
    output.position[1] = -200;
    AdvancedState history{};
    history.position[1] = -200;
    history.orbit[1] = -200;
    const auto before = history;
    const float wall[]{0, -30, 0};
    ApplyCollisionOutput(output, true, wall);
    assert(output.position[1] == -30);
    assert(std::memcmp(&history, &before, sizeof(history)) == 0);
    output.position[1] = -200;
    ApplyCollisionOutput(output, false, wall);
    assert(output.base[1] == 170 && output.position[1] == -30);
}
