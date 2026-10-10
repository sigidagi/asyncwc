#include "asyncwc/config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <string_view>

namespace asyncwc {
namespace {

std::unexpected<Error> invalid(std::string detail) {
    return make_unexpected(Errc::invalid_configuration, Operation::validate_config,
                           std::move(detail));
}

// RFC 9110, section 5.6.2: token = 1*tchar
bool is_token(std::string_view text) {
    constexpr std::string_view extra_chars = "!#$%&'*+-.^_`|~";
    return !text.empty() && std::ranges::all_of(text, [&](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || extra_chars.contains(c);
    });
}

// CR, LF or NUL inside a header value would allow request smuggling.
bool is_safe_header_value(std::string_view text) {
    return text.find_first_of(std::string_view{"\r\n\0", 3}) == std::string_view::npos;
}

bool iequals(std::string_view lhs, std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

// Headers that the WebSocket handshake itself owns.
bool is_reserved_header(std::string_view name) {
    constexpr std::array reserved = {
        std::string_view{"Host"},
        std::string_view{"Upgrade"},
        std::string_view{"Connection"},
        std::string_view{"Sec-WebSocket-Key"},
        std::string_view{"Sec-WebSocket-Version"},
    };
    return std::ranges::any_of(
        reserved, [&](std::string_view candidate) { return iequals(candidate, name); });
}

} // namespace

Result<void> validate(const ConnectionOptions& options) {
    if (options.connect_timeout <= std::chrono::milliseconds::zero()) {
        return invalid("connect_timeout must be positive");
    }
    if (options.handshake_timeout <= std::chrono::milliseconds::zero()) {
        return invalid("handshake_timeout must be positive");
    }
    if (options.max_message_size == 0) {
        return invalid("max_message_size must be positive");
    }
    if (!is_safe_header_value(options.user_agent)) {
        return invalid("user_agent contains CR, LF or NUL");
    }
    for (const auto& header : options.headers) {
        if (!is_token(header.name)) {
            return invalid(std::format("header name '{}' is not a valid HTTP token", header.name));
        }
        if (is_reserved_header(header.name)) {
            return invalid(
                std::format("header '{}' is managed by the WebSocket handshake", header.name));
        }
        if (!is_safe_header_value(header.value)) {
            return invalid(std::format("value of header '{}' contains CR, LF or NUL", header.name));
        }
    }
    return {};
}

Result<void> validate(const HeartbeatOptions& options) {
    if (options.interval < std::chrono::milliseconds::zero()) {
        return invalid("heartbeat.interval must not be negative");
    }
    if (options.enabled() && (options.timeout <= std::chrono::milliseconds::zero() ||
                              options.timeout > options.interval)) {
        return invalid("heartbeat.timeout must be positive and not exceed heartbeat.interval");
    }
    return {};
}

Result<void> validate(const ReconnectPolicy& policy) {
    if (policy.initial_delay < std::chrono::milliseconds::zero()) {
        return invalid("reconnect.initial_delay must not be negative");
    }
    if (policy.max_delay < policy.initial_delay) {
        return invalid("reconnect.max_delay must not be smaller than reconnect.initial_delay");
    }
    if (std::isnan(policy.multiplier) || policy.multiplier < 1.0) {
        return invalid("reconnect.multiplier must be at least 1");
    }
    if (std::isnan(policy.jitter) || policy.jitter < 0.0 || policy.jitter > 1.0) {
        return invalid("reconnect.jitter must be within [0, 1]");
    }
    return {};
}

Result<void> validate(const ClientConfig& config) {
    return validate(config.connection)
        .and_then([&] { return validate(config.heartbeat); })
        .and_then([&] { return validate(config.reconnect); });
}

} // namespace asyncwc
