#pragma once

#include "asyncwc/error.hpp"
#include "asyncwc/version.hpp"

#include <spdlog/fwd.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace asyncwc {

/// An extra HTTP header sent with the WebSocket upgrade request.
struct Header {
    std::string name;
    std::string value;
};

/// Settings of a single WebSocket connection.
struct ConnectionOptions {
    /// Limit for establishing the TCP connection.
    std::chrono::milliseconds connect_timeout{std::chrono::seconds{10}};
    /// Limit for the opening and for the closing WebSocket handshake.
    std::chrono::milliseconds handshake_timeout{std::chrono::seconds{10}};
    /// Incoming messages larger than this fail the read with `message too big`.
    std::size_t max_message_size = std::size_t{16} * 1024 * 1024;
    std::string user_agent = std::string{"asyncwc/"} + std::string{version};
    /// Added to the upgrade request, e.g. `Authorization` or `Sec-WebSocket-Protocol`.
    std::vector<Header> headers;
    /// Logger for this connection; `nullptr` selects `asyncwc::logger()`.
    std::shared_ptr<spdlog::logger> logger;
};

/// Keep-alive pings that detect dead peers and half-open TCP connections.
struct HeartbeatOptions {
    /// Time between two pings; zero disables the heartbeat.
    std::chrono::milliseconds interval{std::chrono::seconds{15}};
    /// How long to wait for the pong; must not exceed `interval`.
    std::chrono::milliseconds timeout{std::chrono::seconds{10}};

    [[nodiscard]] bool enabled() const noexcept { return interval.count() > 0; }
};

/// Exponential backoff between reconnect attempts.
///
/// The n-th delay (n = 0, 1, ...) is `initial_delay * multiplier^n`, capped at `max_delay`
/// and randomised by +/- `jitter` (a fraction of the delay) to spread out reconnect storms.
struct ReconnectPolicy {
    bool enabled = true;
    std::chrono::milliseconds initial_delay{500};
    std::chrono::milliseconds max_delay{std::chrono::seconds{30}};
    double multiplier = 2.0;
    double jitter = 0.2;
    /// Consecutive failed attempts before giving up; zero retries forever.
    std::size_t max_attempts = 0;
};

/// Everything a `Client` needs.
struct ClientConfig {
    std::string url; ///< e.g. "ws://127.0.0.1:8080/chat"
    ConnectionOptions connection;
    HeartbeatOptions heartbeat;
    ReconnectPolicy reconnect;
};

/// Checks value ranges and rejects headers that would corrupt the handshake.
[[nodiscard]] Result<void> validate(const ConnectionOptions& options);
[[nodiscard]] Result<void> validate(const HeartbeatOptions& options);
[[nodiscard]] Result<void> validate(const ReconnectPolicy& policy);
/// Validates all parts of the configuration, except the URL (see `parse_url`).
[[nodiscard]] Result<void> validate(const ClientConfig& config);

} // namespace asyncwc
