#include "../plugin/owned_value.h"
#include <cassert>
#include <limits>

int main() {
    camera::OwnedValue<float> scale;
    float value = 0.75f;
    assert(scale.Write(value, 0.000075f));
    assert(scale.Write(value, 0.00005f));
    assert(scale.Restore(value) && value == 0.75f);
    assert(!scale.Restore(value));

    // A later animation/mod write wins over our saved original.
    scale.Write(value, 0.0001f);
    value = 1.25f;
    assert(!scale.Restore(value) && value == 1.25f);
    assert(!scale.Active());

    // Reacquisition after ownership was lost must restore the newer value.
    scale.Write(value, 0.0001f);
    value = 2.0f;
    scale.Write(value, 0.0002f);
    assert(scale.Restore(value) && value == 2.0f);

    camera::OwnedValue<bool> visibility;
    bool hidden = true;
    assert(!visibility.Write(hidden, true));
    assert(!visibility.Active());
    assert(visibility.Write(hidden, false));
    assert(visibility.Restore(hidden) && hidden);

    // Nonfinite external mutation is preserved rather than replaced on teardown.
    scale.Write(value, 0.0001f);
    value = std::numeric_limits<float>::quiet_NaN();
    assert(!scale.Restore(value));
    assert(value != value);
}
