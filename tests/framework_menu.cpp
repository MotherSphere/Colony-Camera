#include <RE/Skyrim.h>
#include "../plugin/framework_menu.h"
#include <cassert>

int main() {
    using namespace framework_menu;
    const auto defaults = cc_defaults();
    Register(defaults, [](const CameraConfig&) { assert(false && "No GUI edits should apply in this test"); });
    assert(!ready && !Blocking()); // Optional DLL is absent: no import/load failure.
    Sync(defaults);
    assert(draft.enabled == defaults.enabled);
    dirty = true;
    draft.body_alignment.body_backset = 17;
    auto external = defaults;
    external.enabled = 0;
    Sync(external);
    assert(latest.enabled == 0 && draft.body_alignment.body_backset == 17);
    busy = true; dirty = false;
    Sync(defaults);
    assert(draft.body_alignment.body_backset == 17);
    busy = false;
    Sync(defaults);
    assert(draft.body_alignment.body_backset == defaults.body_alignment.body_backset);
    draft.keys[1] = draft.keys[0];
    Submit(true);
    assert(!busy && status.find("Invalid") != std::string::npos);
}
