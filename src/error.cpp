#include "asyncwc/error.hpp"

#include <utility>

namespace asyncwc {
namespace {

class ErrorCategory final : public std::error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "asyncwc"; }

    [[nodiscard]] std::string message(int value) const override {
        switch (value) {
        case static_cast<int>(Errc::invalid_url):
            return "invalid WebSocket URL";
        case static_cast<int>(Errc::unsupported_scheme):
            return "unsupported URL scheme, only ws:// is supported";
        case static_cast<int>(Errc::invalid_configuration):
            return "invalid configuration";
        case static_cast<int>(Errc::invalid_argument):
            return "invalid argument";
        case static_cast<int>(Errc::not_connected):
            return "not connected";
        case static_cast<int>(Errc::already_connected):
            return "already connected";
        case static_cast<int>(Errc::invalid_state):
            return "operation not allowed in the current state";
        case static_cast<int>(Errc::operation_in_progress):
            return "operation already in progress";
        case static_cast<int>(Errc::connection_closed):
            return "connection closed";
        case static_cast<int>(Errc::timeout):
            return "operation timed out";
        case static_cast<int>(Errc::pong_timeout):
            return "no pong received within the heartbeat timeout";
        case static_cast<int>(Errc::handshake_rejected):
            return "WebSocket handshake rejected by the server";
        case static_cast<int>(Errc::reconnect_attempts_exhausted):
            return "reconnect attempts exhausted";
        case static_cast<int>(Errc::cancelled):
            return "operation cancelled";
        default:
            return "unknown asyncwc error";
        }
    }

    [[nodiscard]] std::error_condition default_error_condition(int value) const noexcept override {
        switch (value) {
        case static_cast<int>(Errc::invalid_url):
        case static_cast<int>(Errc::unsupported_scheme):
        case static_cast<int>(Errc::invalid_configuration):
        case static_cast<int>(Errc::invalid_argument):
            return std::errc::invalid_argument;
        case static_cast<int>(Errc::not_connected):
            return std::errc::not_connected;
        case static_cast<int>(Errc::already_connected):
            return std::errc::already_connected;
        case static_cast<int>(Errc::operation_in_progress):
            return std::errc::operation_in_progress;
        case static_cast<int>(Errc::timeout):
        case static_cast<int>(Errc::pong_timeout):
            return std::errc::timed_out;
        case static_cast<int>(Errc::cancelled):
            return std::errc::operation_canceled;
        default:
            return {value, *this};
        }
    }
};

} // namespace

const std::error_category& error_category() noexcept {
    static const ErrorCategory category;
    return category;
}

std::error_code make_error_code(Errc errc) noexcept {
    return {static_cast<int>(errc), error_category()};
}

std::string_view to_string(Operation operation) noexcept {
    switch (operation) {
    case Operation::parse_url:
        return "parse_url";
    case Operation::validate_config:
        return "validate_config";
    case Operation::resolve:
        return "resolve";
    case Operation::connect:
        return "connect";
    case Operation::handshake:
        return "handshake";
    case Operation::read:
        return "read";
    case Operation::write:
        return "write";
    case Operation::ping:
        return "ping";
    case Operation::close:
        return "close";
    case Operation::heartbeat:
        return "heartbeat";
    case Operation::reconnect:
        return "reconnect";
    case Operation::run:
        return "run";
    }
    return "unknown";
}

std::string Error::message() const {
    std::string text{to_string(operation)};
    text += ": ";
    text += code.message();
    if (!detail.empty()) {
        text += " (";
        text += detail;
        text += ')';
    }
    return text;
}

std::unexpected<Error> make_unexpected(std::error_code code, Operation operation,
                                       std::string detail) {
    return std::unexpected(
        Error{.code = code, .operation = operation, .detail = std::move(detail)});
}

} // namespace asyncwc
