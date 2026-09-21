#include "../plugin/camera_space.h"
#include <cassert>
int main() {
    int cellA{}, cellB{}, worldA{}, worldB{};
    using camera::SameSpace;
    assert(SameSpace(&cellA, &worldA, &cellB, &worldA));
    assert(SameSpace(&cellB, &worldA, &cellA, &worldA));
    assert(SameSpace(&cellA, nullptr, &cellA, nullptr));
    assert(!SameSpace(&cellA, nullptr, &cellB, nullptr));
    assert(!SameSpace(&cellA, &worldA, &cellB, &worldB));
    assert(!SameSpace(&cellA, &worldA, &cellA, &worldB));
    assert(!SameSpace(&cellA, &worldA, &cellB, nullptr));
    assert(!SameSpace(&cellA, nullptr, &cellB, &worldA));
    assert(!SameSpace(nullptr, nullptr, &cellA, &worldA));
    assert(!SameSpace(&cellA, &worldA, nullptr, &worldA));
    assert(!SameSpace(nullptr, nullptr, nullptr, nullptr));
}
