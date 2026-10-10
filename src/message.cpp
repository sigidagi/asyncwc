#include "asyncwc/message.hpp"

#include <algorithm>
#include <utility>

namespace asyncwc {

Message Message::text(std::string payload) {
    return Message{.type = MessageType::text, .payload = std::move(payload)};
}

Message Message::binary(std::span<const std::byte> data) {
    std::string payload(data.size(), '\0');
    std::ranges::transform(data, payload.begin(),
                           [](std::byte value) { return static_cast<char>(value); });
    return Message{.type = MessageType::binary, .payload = std::move(payload)};
}

bool is_sendable(CloseCode code) noexcept {
    const auto value = std::to_underlying(code);
    const bool registered = value >= 1000 && value <= 1014 && code != CloseCode::no_status &&
                            code != CloseCode::abnormal && value != 1004;
    const bool application = value >= 3000 && value <= 4999;
    return registered || application;
}

} // namespace asyncwc
