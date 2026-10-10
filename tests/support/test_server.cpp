#include "support/test_server.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include <format>
#include <utility>

namespace asyncwc::test {
namespace {

constexpr auto use_nothrow = asio::as_tuple(asio::use_awaitable);

asio::awaitable<void> run_handler(SessionHandler handler, std::shared_ptr<ServerSession> session) {
    co_await handler(std::move(session));
}

asio::awaitable<void> accept_loop(std::shared_ptr<ServerState> state) {
    for (;;) {
        auto [ec, socket] = co_await state->acceptor.async_accept(use_nothrow);
        if (ec) {
            co_return;
        }
        ++state->connections;
        auto session = std::make_shared<ServerSession>(std::move(socket), state);
        state->sessions.push_back(session);
        asio::co_spawn(state->acceptor.get_executor(), run_handler(state->handler, session),
                       asio::detached);
    }
}

} // namespace

ServerSession::ServerSession(tcp::socket socket, std::weak_ptr<ServerState> server)
    : stream_{std::move(socket)}, hold_timer_{stream_.get_executor()}, server_{std::move(server)} {
    stream_.control_callback([this](websocket::frame_type kind, beast::string_view) {
        if (kind == websocket::frame_type::pong) {
            ++pongs_received_;
        }
    });
}

asio::awaitable<bool> ServerSession::read_request() {
    auto [ec, size] =
        co_await http::async_read(stream_.next_layer(), buffer_, request_, use_nothrow);
    if (ec) {
        co_return false;
    }
    if (auto server = server_.lock()) {
        server->requests.push_back(request_);
    }
    co_return true;
}

asio::awaitable<bool> ServerSession::accept() {
    if (!co_await read_request()) {
        co_return false;
    }
    auto [ec] = co_await stream_.async_accept(request_, use_nothrow);
    co_return !ec;
}

asio::awaitable<void> ServerSession::reject(http::status status) {
    if (!co_await read_request()) {
        co_return;
    }
    http::response<http::string_body> response{status, request_.version()};
    response.keep_alive(false);
    response.body() = "go away";
    response.prepare_payload();
    co_await http::async_write(stream_.next_layer(), response, use_nothrow);
    drop();
}

asio::awaitable<std::optional<Message>> ServerSession::read() {
    auto [ec, size] = co_await stream_.async_read(buffer_, use_nothrow);
    if (ec) {
        co_return std::nullopt;
    }
    Message message{
        .type = stream_.got_text() ? MessageType::text : MessageType::binary,
        .payload = beast::buffers_to_string(buffer_.data()),
    };
    buffer_.consume(size);
    co_return message;
}

asio::awaitable<bool> ServerSession::write(Message message) {
    stream_.text(message.is_text());
    auto [ec, size] = co_await stream_.async_write(asio::buffer(message.payload), use_nothrow);
    co_return !ec;
}

asio::awaitable<bool> ServerSession::write_text(std::string text) {
    co_return co_await write(Message::text(std::move(text)));
}

asio::awaitable<bool> ServerSession::ping(std::string payload) {
    auto [ec] = co_await stream_.async_ping(websocket::ping_data{payload}, use_nothrow);
    co_return !ec;
}

asio::awaitable<bool> ServerSession::pong(std::string payload) {
    auto [ec] = co_await stream_.async_pong(websocket::ping_data{payload}, use_nothrow);
    co_return !ec;
}

asio::awaitable<void> ServerSession::close(websocket::close_code code, std::string reason) {
    co_await stream_.async_close(websocket::close_reason{code, reason}, use_nothrow);
}

asio::awaitable<void> ServerSession::echo() {
    while (auto message = co_await read()) {
        if (!co_await write(std::move(*message))) {
            co_return;
        }
    }
}

asio::awaitable<void> ServerSession::hold() {
    hold_timer_.expires_at(asio::steady_timer::time_point::max());
    co_await hold_timer_.async_wait(use_nothrow);
}

void ServerSession::drop() {
    hold_timer_.cancel();
    boost::system::error_code ignored;
    beast::get_lowest_layer(stream_).socket().close(ignored);
}

TestServer::TestServer(asio::io_context& io, std::uint16_t port)
    : state_{std::make_shared<ServerState>(io)} {
    state_->handler = echo();
    const tcp::endpoint endpoint{asio::ip::address_v4::loopback(), port};
    state_->acceptor.open(endpoint.protocol());
    state_->acceptor.set_option(asio::socket_base::reuse_address(true));
    state_->acceptor.bind(endpoint);
    state_->acceptor.listen();
    port_ = state_->acceptor.local_endpoint().port();
    asio::co_spawn(io, accept_loop(state_), asio::detached);
}

TestServer::~TestServer() {
    stop();
}

void TestServer::on_connection(SessionHandler handler) {
    state_->handler = std::move(handler);
}

std::string TestServer::url(std::string_view target) const {
    return std::format("ws://127.0.0.1:{}{}", port_, target);
}

void TestServer::stop() {
    boost::system::error_code ignored;
    state_->acceptor.close(ignored);
    for (const auto& weak : state_->sessions) {
        if (const auto session = weak.lock()) {
            session->drop();
        }
    }
    state_->sessions.clear();
}

SessionHandler TestServer::echo() {
    return [](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (co_await session->accept()) {
            co_await session->echo();
        }
    };
}

SessionHandler TestServer::silent() {
    return [](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (co_await session->accept()) {
            co_await session->hold();
        }
    };
}

SessionHandler TestServer::unresponsive() {
    return [](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        co_await session->hold();
    };
}

std::uint16_t unused_port(asio::io_context& io) {
    const tcp::acceptor acceptor{io, tcp::endpoint{asio::ip::address_v4::loopback(), 0}};
    return acceptor.local_endpoint().port();
}

} // namespace asyncwc::test
