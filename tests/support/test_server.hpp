#pragma once

#include "asyncwc/message.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asyncwc::test {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using asio::ip::tcp;

using Request = http::request<http::string_body>;

struct ServerState;

/// The server side of one accepted TCP connection. Test handlers script its behaviour.
class ServerSession {
public:
    ServerSession(tcp::socket socket, std::weak_ptr<ServerState> server);

    /// Reads the upgrade request and accepts the WebSocket handshake.
    asio::awaitable<bool> accept();
    /// Reads the upgrade request and answers it with a plain HTTP error.
    asio::awaitable<void> reject(http::status status);

    asio::awaitable<std::optional<Message>> read();
    asio::awaitable<bool> write(Message message);
    asio::awaitable<bool> write_text(std::string text);
    asio::awaitable<bool> ping(std::string payload);
    asio::awaitable<bool> pong(std::string payload);
    asio::awaitable<void> close(websocket::close_code code = websocket::close_code::normal,
                                std::string reason = {});
    /// Echoes every message until the connection ends.
    asio::awaitable<void> echo();
    /// Parks the coroutine until the server stops, without reading or writing.
    asio::awaitable<void> hold();
    /// Drops the TCP connection without a closing handshake.
    void drop();

    websocket::stream<beast::tcp_stream>& stream() { return stream_; }

    [[nodiscard]] const Request& request() const { return request_; }

    /// Pongs the server received, e.g. in reply to ping().
    [[nodiscard]] std::size_t pongs_received() const { return pongs_received_; }

private:
    asio::awaitable<bool> read_request();

    websocket::stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    Request request_;
    asio::steady_timer hold_timer_;
    std::weak_ptr<ServerState> server_;
    std::size_t pongs_received_ = 0;
};

using SessionHandler = std::function<asio::awaitable<void>(std::shared_ptr<ServerSession>)>;

struct ServerState {
    explicit ServerState(asio::io_context& io) : acceptor{io} {}

    tcp::acceptor acceptor;
    SessionHandler handler;
    std::size_t connections = 0;
    std::vector<Request> requests;
    std::vector<std::weak_ptr<ServerSession>> sessions;
};

/// In-process WebSocket server on 127.0.0.1 that runs on the test's io_context.
class TestServer {
public:
    /// Port 0 picks a free ephemeral port.
    explicit TestServer(asio::io_context& io, std::uint16_t port = 0);
    ~TestServer();

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;
    TestServer(TestServer&&) = delete;
    TestServer& operator=(TestServer&&) = delete;

    /// Runs for every accepted connection; the default accepts and echoes.
    void on_connection(SessionHandler handler);

    [[nodiscard]] std::uint16_t port() const { return port_; }

    [[nodiscard]] std::string url(std::string_view target = "/") const;

    /// Number of TCP connections accepted so far.
    [[nodiscard]] std::size_t connections() const { return state_->connections; }

    /// Upgrade requests received so far.
    [[nodiscard]] const std::vector<Request>& requests() const { return state_->requests; }

    /// Stops accepting and drops all live sessions.
    void stop();

    static SessionHandler echo();
    /// Accepts the handshake, then never reads: pings stay unanswered.
    static SessionHandler silent();
    /// Accepts TCP, but never answers the upgrade request.
    static SessionHandler unresponsive();

private:
    std::shared_ptr<ServerState> state_;
    std::uint16_t port_ = 0;
};

/// Returns a local port with nothing listening on it.
std::uint16_t unused_port(asio::io_context& io);

} // namespace asyncwc::test
