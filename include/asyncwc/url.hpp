#pragma once

#include "asyncwc/error.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace asyncwc {

/// Where to connect to, as extracted from a ws:// URL.
struct Endpoint {
    static constexpr std::uint16_t default_port = 80;

    std::string host; ///< Name or IP address; IPv6 without brackets.
    std::uint16_t port = default_port;
    std::string target = "/"; ///< Request target: path plus optional "?query".

    /// Value of the HTTP `Host` header, e.g. "example.com:8080" or "[::1]".
    [[nodiscard]] std::string authority() const;

    friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

/// Parses a `ws://host[:port][/path][?query]` URL.
///
/// Fails with `Errc::unsupported_scheme` for anything but ws:// (including wss://) and
/// with `Errc::invalid_url` for malformed URLs, a missing host, credentials or a fragment.
[[nodiscard]] Result<Endpoint> parse_url(std::string_view url);

} // namespace asyncwc
