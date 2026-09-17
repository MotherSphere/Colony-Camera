#pragma once

namespace camera {
// A lease on one value, not on its address. The caller owns the object's lifetime.
// Restore only if nobody has replaced our last output in the meantime.
template <class T>
class OwnedValue {
    T before_{};
    T output_{};
    bool active_ = false;

public:
    bool Write(T& value, const T& replacement) {
        if (active_ && value != output_) active_ = false;
        if (value == replacement) return false;
        if (!active_) before_ = value;
        value = output_ = replacement;
        active_ = true;
        return true;
    }

    bool Restore(T& value) {
        const bool restore = active_ && value == output_;
        if (restore) value = before_;
        active_ = false;
        return restore;
    }

    [[nodiscard]] bool Active() const { return active_; }
};
}
