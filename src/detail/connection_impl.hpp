#pragma once

#include "asyncwc/config.hpp"
#include "asyncwc/error.hpp"
#include "asyncwc/message.hpp"
#include "asyncwc/url.hpp"
#include "detail/async_mutex.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <spdlog/logger.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace asyncwc::detail {

/// Largest payload of a ping or pong frame (RFC 6455, section 5.5).
inline constexpr std::size_t max_control_payload = 125;
/// Largest close reason: the control payload minus the two byte close code.
inline constexpr std::size_t max_close_reason = max_control_payload - 2;

/// Checks the arguments of a close() call before anything is sent.
[[nodiscard]] Result<void> validate_close(CloseCode code, std::string_view reason);

/// Shared state behind `asyncwc::Connection`; also used directly by `ClientImpl`.
class ConnectionImpl {
public:
    using PongHandler = std::function<void(std::string_view payload)>;

    ConnectionImpl(boost::asio::any_io_executor executor, ConnectionOptions options);

    [[nodiscard]] const boost::asio::any_io_executor& get_executor() const noexcept {
        return executor_;
    }

    [[nodiscard]] boost::asio::awaitable<Result<void>> connect(std::string url);
    [[nodiscard]] boost::asio::awaitable<Result<void>> connect(Endpoint endpoint);
    [[nodiscard]] boost::asio::awaitable<Result<void>> send(Message message);
    [[nodiscard]] boost::asio::awaitable<Result<Message>> receive();
    [[nodiscard]] boost::asio::awaitable<Result<void>> ping(std::string payload);
    [[nodiscard]] boost::asio::awaitable<Result<void>> close(CloseCode code, std::string reason);

    /// Tears the connection down at once. Pending and later operations report `reason`.
    void abort(std::error_code reason) noexcept;

    [[nodiscard]] bool is_open() const noexcept { return state_ == State::open; }

    [[nodiscard]] std::optional<CloseInfo> close_info() const { return close_info_; }

    void set_pong_handler(PongHandler handler) { pong_handler_ = std::move(handler); }

private:
    using Stream = boost::beast::websocket::stream<boost::beast::tcp_stream>;

    enum class State : std::uint8_t { idle, connecting, open, closing, closed };

    [[nodiscard]] boost::asio::awaitable<Result<void>> establish();
    void configure_stream();
    void on_control_frame(boost::beast::websocket::frame_type kind, std::string_view payload);
    [[nodiscard]] std::unexpected<Error> read_failed(const boost::system::error_code& ec);
    void record_close_frame();
    [[nodiscard]] std::unexpected<Error> failure(std::error_code code, Operation operation,
                                                 std::string detail = {}) const;

    [[nodiscard]] bool aborted() const noexcept { return static_cast<bool>(abort_reason_); }

    void teardown() noexcept;

    boost::asio::any_io_executor executor_;
    ConnectionOptions options_;
    std::shared_ptr<spdlog::logger> log_;
    boost::asio::ip::tcp::resolver resolver_;
    Stream stream_;
    boost::beast::flat_buffer buffer_;
    AsyncMutex write_mutex_;
    Endpoint endpoint_;
    State state_ = State::idle;
    bool reading_ = false;
    bool close_started_ = false;
    std::optional<std::error_code> handshake_outcome_; ///< closing handshake done by a read
    std::error_code abort_reason_;
    std::optional<CloseInfo> close_info_;
    PongHandler pong_handler_;
};

} // namespace asyncwc::detail
