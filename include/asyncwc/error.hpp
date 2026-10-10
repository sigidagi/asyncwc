#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace asyncwc {

/// Error codes produced by asyncwc itself.
///
/// Failures reported by the operating system or Boost (DNS, TCP, WebSocket protocol
/// violations, ...) keep their original category. Every code maps to the closest
/// `std::errc` condition where one exists, so both of these work:
///
///     error.code == asyncwc::Errc::pong_timeout
///     error.code == std::errc::timed_out
enum class Errc : std::uint8_t {
    invalid_url = 1,              ///< The URL cannot be parsed or has no host.
    unsupported_scheme,           ///< The URL scheme is not `ws://`.
    invalid_configuration,        ///< A configuration value is out of range.
    invalid_argument,             ///< An argument violates a WebSocket protocol limit.
    not_connected,                ///< The operation requires an open connection.
    already_connected,            ///< connect() was called on an active connection.
    invalid_state,                ///< The object cannot perform the operation any more.
    operation_in_progress,        ///< An exclusive operation is already running.
    connection_closed,            ///< The WebSocket closing handshake took place.
    timeout,                      ///< A connect, handshake or close deadline expired.
    pong_timeout,                 ///< The peer did not answer a heartbeat ping in time.
    handshake_rejected,           ///< The server refused the WebSocket upgrade.
    reconnect_attempts_exhausted, ///< The reconnect policy gave up.
    cancelled,                    ///< The operation was cancelled locally.
};

/// The `std::error_category` of `Errc`; its name is "asyncwc".
[[nodiscard]] const std::error_category& error_category() noexcept;

[[nodiscard]] std::error_code make_error_code(Errc errc) noexcept;

} // namespace asyncwc

template <>
struct std::is_error_code_enum<asyncwc::Errc> : std::true_type {};

namespace asyncwc {

/// The step during which an error occurred.
enum class Operation : std::uint8_t {
    parse_url,
    validate_config,
    resolve,
    connect,
    handshake,
    read,
    write,
    ping,
    close,
    heartbeat,
    reconnect,
    run,
};

[[nodiscard]] std::string_view to_string(Operation operation) noexcept;

/// What went wrong, during which step, plus optional human readable context.
struct Error {
    std::error_code code;
    Operation operation = Operation::run;
    std::string detail; ///< e.g. the HTTP status of a rejected handshake

    /// "<operation>: <code message> (<detail>)"
    [[nodiscard]] std::string message() const;
};

/// Every fallible asyncwc operation returns a `Result`.
template <typename T = void>
using Result = std::expected<T, Error>;

/// Shorthand for `std::unexpected(Error{code, operation, detail})`.
[[nodiscard]] std::unexpected<Error> make_unexpected(std::error_code code, Operation operation,
                                                     std::string detail = {});

} // namespace asyncwc
