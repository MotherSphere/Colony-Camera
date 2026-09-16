#include "../plugin/core.h"
#include "../plugin/runtime.h"
#include <cassert>
#include <cmath>
#include <cstring>
int main() {
    for (auto entry : {runtime::begin, runtime::end, runtime::update, runtime::collision}) {
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
