#pragma once

#include "asyncwc/config.hpp"
#include "asyncwc/error.hpp"
#include "asyncwc/message.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace asyncwc {

namespace detail {
class ClientImpl;
} // namespace detail

enum class ClientState : std::uint8_t {
    idle,         ///< run() was not called yet.
    connecting,   ///< Establishing a connection.
    connected,    ///< The connection is open; send() works.
    reconnecting, ///< Waiting for the backoff delay before the next attempt.
    closing,      ///< close() is performing the closing handshake.
    stopped,      ///< run() has finished.
};

[[nodiscard]] std::string_view to_string(ClientState state) noexcept;

/// A self-healing WebSocket client: it connects, keeps the connection alive with
/// ping/pong heartbeats and reconnects with exponential backoff whenever the connection
/// is lost, until `close()` or `stop()` is called.
///
/// ```
/// asyncwc::Client client{io.get_executor(), {.url = "ws://127.0.0.1:8080"}};
/// client.on_message([](const asyncwc::Message& m) { spdlog::info("got {}", m.payload); });
/// asio::co_spawn(io, client.run(), asio::detached);
/// io.run();
/// ```
///
/// Handlers run on the client's executor, inside its read loop: keep them short and do not
/// block. Exceptions escaping a handler are logged and swallowed. To reply from a handler,
/// spawn the send: `asio::co_spawn(executor, client.send_text("pong"), asio::detached)`.
///
/// Not thread-safe: use the object from the thread (or strand) running its executor.
class Client {
public:
    using executor_type = boost::asio::any_io_executor;
    using OpenHandler = std::function<void()>;
    using MessageHandler = std::function<void(const Message& message)>;
    using DisconnectHandler = std::function<void(const Error& reason)>;
    using ErrorHandler = std::function<void(const Error& error)>;
    using ReconnectHandler =
        std::function<void(std::size_t attempt, std::chrono::milliseconds delay)>;
    using PongHandler = std::function<void(std::chrono::microseconds round_trip)>;

    Client(executor_type executor, ClientConfig config);
    /// Clears all handlers and stops the client; a running run() finishes on its own.
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    /// The connection is open (initially and after every reconnect).
    void on_open(OpenHandler handler);
    void on_message(MessageHandler handler);
    /// An open connection ended; `reason` tells why (e.g. `Errc::pong_timeout`).
    void on_disconnect(DisconnectHandler handler);
    /// A connection attempt failed (DNS, TCP or handshake error).
    void on_error(ErrorHandler handler);
    /// The next attempt is scheduled after `delay`; `attempt` counts from 1.
    void on_reconnect(ReconnectHandler handler);
    /// A heartbeat ping was answered.
    void on_pong(PongHandler handler);

    /// Connects and supervises the connection until it is closed for good.
    ///
    /// Completes successfully after `close()`, `stop()` or Asio cancellation of the
    /// awaiting coroutine. Fails if the configuration or URL is invalid, if run() is
    /// already active, with `Errc::reconnect_attempts_exhausted` when the reconnect policy
    /// gives up, or with the reason of the first failure when reconnecting is disabled.
    [[nodiscard]] boost::asio::awaitable<Result<void>> run();

    /// Fails with `Errc::not_connected` unless the state is `connected`.
    [[nodiscard]] boost::asio::awaitable<Result<void>> send(Message message);
    [[nodiscard]] boost::asio::awaitable<Result<void>> send_text(std::string_view text);
    [[nodiscard]] boost::asio::awaitable<Result<void>> send_binary(std::span<const std::byte> data);

    /// Performs the closing handshake (when connected) and ends run() without reconnecting.
    [[nodiscard]] boost::asio::awaitable<Result<void>> close(CloseCode code = CloseCode::normal,
                                                             std::string_view reason = {});

    /// Ends run() promptly: drops the connection without a closing handshake.
    void stop() noexcept;

    [[nodiscard]] ClientState state() const noexcept;
    [[nodiscard]] const ClientConfig& config() const noexcept;
    [[nodiscard]] executor_type get_executor() const noexcept;

private:
    std::shared_ptr<detail::ClientImpl> impl_;
};

} // namespace asyncwc
