#pragma once

#include <algorithm>
#include <chrono>

namespace sb {

// Exponential reconnect delays: initial, 2x, 4x, ... capped at maximum.
// Call reset() once a connection succeeds.
class Backoff {
public:
    using Ms = std::chrono::milliseconds;

    constexpr explicit Backoff(Ms initial = Ms{1000}, Ms maximum = Ms{30000}) noexcept
        : initial_(initial), maximum_(maximum), next_(initial) {}

    // Returns the delay to wait before the next attempt, then grows it.
    constexpr Ms nextDelay() noexcept {
        const Ms delay = next_;
        next_ = std::min(next_ * 2, maximum_);
        ++attempts_;
        return delay;
    }

    constexpr void reset() noexcept {
        next_ = initial_;
        attempts_ = 0;
    }

    constexpr int attempts() const noexcept { return attempts_; }

private:
    Ms initial_;
    Ms maximum_;
    Ms next_;
    int attempts_ = 0;
};

}  // namespace sb
