#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace asyncwc {

enum class MessageType : std::uint8_t { text, binary };

/// A complete (de-fragmented) WebSocket data message.
struct Message {
    MessageType type = MessageType::text;
    std::string payload; ///< UTF-8 text or raw bytes, depending on `type`

    [[nodiscard]] static Message text(std::string payload);
    [[nodiscard]] static Message binary(std::span<const std::byte> data);

    [[nodiscard]] bool is_text() const noexcept { return type == MessageType::text; }

    [[nodiscard]] bool is_binary() const noexcept { return type == MessageType::binary; }

    /// The payload viewed as raw bytes; handy for binary messages.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::as_bytes(std::span{payload});
    }

    friend bool operator==(const Message&, const Message&) = default;
};

/// Status codes of a WebSocket close frame (RFC 6455, section 7.4.1).
///
/// The enum's underlying type holds any 16 bit value, so application specific codes
/// (3000-4999) can be expressed as `static_cast<CloseCode>(4000)`.
enum class CloseCode : std::uint16_t {
    normal = 1000,
    going_away = 1001,
    protocol_error = 1002,
    unsupported_data = 1003,
    no_status = 1005, ///< Reserved: never sent, reported when a close frame has no code.
    abnormal = 1006,  ///< Reserved: never sent, the connection dropped without a close frame.
    invalid_payload = 1007,
    policy_violation = 1008,
    message_too_big = 1009,
    mandatory_extension = 1010,
    internal_error = 1011,
    service_restart = 1012,
    try_again_later = 1013,
};

/// Whether `code` may be put on the wire in a close frame.
[[nodiscard]] bool is_sendable(CloseCode code) noexcept;

/// The close frame received from the peer.
struct CloseInfo {
    CloseCode code = CloseCode::no_status;
    std::string reason;

    friend bool operator==(const CloseInfo&, const CloseInfo&) = default;
};

} // namespace asyncwc
