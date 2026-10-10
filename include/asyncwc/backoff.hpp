#pragma once

#include "asyncwc/config.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

namespace asyncwc {

/// Computes reconnect delays according to a `ReconnectPolicy`.
class Backoff {
public:
    explicit Backoff(ReconnectPolicy policy, std::uint64_t seed = std::random_device{}());

    /// The delay before the next attempt, or `std::nullopt` once `max_attempts` is used up.
    [[nodiscard]] std::optional<std::chrono::milliseconds> next();

    /// Starts over with `initial_delay`, e.g. after a successful connection.
    void reset() noexcept { attempts_ = 0; }

    /// Number of delays handed out since construction or the last `reset()`.
    [[nodiscard]] std::size_t attempts() const noexcept { return attempts_; }

private:
    ReconnectPolicy policy_;
    std::size_t attempts_ = 0;
    std::mt19937_64 random_;
};

} // namespace asyncwc
