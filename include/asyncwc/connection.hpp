#pragma once

#include "asyncwc/config.hpp"
#include "asyncwc/error.hpp"
#include "asyncwc/message.hpp"
#include "asyncwc/url.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace asyncwc {

namespace detail {
class ConnectionImpl;
} // namespace detail

/// One WebSocket connection over plain TCP (ws://) with a coroutine based "pull" API.
///
/// ```
/// asyncwc::Connection connection{co_await asio::this_coro::executor};
/// if (auto opened = co_await connection.connect("ws://127.0.0.1:8080"); !opened) { ... }
/// co_await connection.send_text("hello");
/// auto reply = co_await connection.receive();
/// co_await connection.close();
/// ```
///
/// * A Connection is single-use: once closed or failed, create a new one (or use
///   `asyncwc::Client`, which reconnects automatically).
/// * Sends and pings may be issued concurrently from several coroutines; they are queued
///   and written in order. At most one `receive()` may be pending at a time.
/// * Pings from the peer are answered automatically, but only while a `receive()` (or a
///   `close()`) is pending, because control frames are processed by reads.
/// * Not thread-safe: use the object from the thread (or strand) running its executor.
///   Destroying it cancels pending operations; awaiting them afterwards is still safe.
class Connection {
public:
    using executor_type = boost::asio::any_io_executor;
    using PongHandler = std::function<void(std::string_view payload)>;

    explicit Connection(executor_type executor, ConnectionOptions options = {});
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    [[nodiscard]] executor_type get_executor() const noexcept;

    /// Resolves the host, connects and performs the WebSocket handshake.
    [[nodiscard]] boost::asio::awaitable<Result<void>> connect(std::string_view url);
    [[nodiscard]] boost::asio::awaitable<Result<void>> connect(Endpoint endpoint);

    [[nodiscard]] boost::asio::awaitable<Result<void>> send(Message message);
    [[nodiscard]] boost::asio::awaitable<Result<void>> send_text(std::string_view text);
    [[nodiscard]] boost::asio::awaitable<Result<void>> send_binary(std::span<const std::byte> data);

    /// Waits for the next data message. Fails with `Errc::connection_closed` once the peer
    /// closed the connection; `close_info()` then holds its close frame.
    [[nodiscard]] boost::asio::awaitable<Result<Message>> receive();

    /// Sends a ping (payload of at most 125 bytes). The pong arrives through `on_pong`.
    [[nodiscard]] boost::asio::awaitable<Result<void>> ping(std::string_view payload = {});

    /// Performs the closing handshake (reason of at most 123 bytes).
    [[nodiscard]] boost::asio::awaitable<Result<void>> close(CloseCode code = CloseCode::normal,
                                                             std::string_view reason = {});

    /// Drops the connection without a closing handshake; pending operations fail with
    /// `Errc::cancelled`.
    void cancel() noexcept;

    [[nodiscard]] bool is_open() const noexcept;

    /// The close frame sent by the peer, once it has been received.
    [[nodiscard]] std::optional<CloseInfo> close_info() const;

    /// Called for every pong received (during `receive()`).
    void on_pong(PongHandler handler);

private:
    std::shared_ptr<detail::ConnectionImpl> impl_;
};

} // namespace asyncwc
