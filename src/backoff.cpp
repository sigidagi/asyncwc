#include "asyncwc/backoff.hpp"

#include <algorithm>
#include <cmath>

namespace asyncwc {

Backoff::Backoff(ReconnectPolicy policy, std::uint64_t seed) : policy_{policy}, random_{seed} {}

std::optional<std::chrono::milliseconds> Backoff::next() {
    if (policy_.max_attempts != 0 && attempts_ >= policy_.max_attempts) {
        return std::nullopt;
    }

    const auto initial = static_cast<double>(policy_.initial_delay.count());
    const auto cap = static_cast<double>(policy_.max_delay.count());
    double delay =
        std::min(initial * std::pow(policy_.multiplier, static_cast<double>(attempts_)), cap);
    if (policy_.jitter > 0.0) {
        std::uniform_real_distribution<double> spread{1.0 - policy_.jitter, 1.0 + policy_.jitter};
        delay = std::min(delay * spread(random_), cap);
    }

    ++attempts_;
    return std::chrono::milliseconds{std::llround(delay)};
}

} // namespace asyncwc
