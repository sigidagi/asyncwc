// Minimal WebSocket echo server (plain Boost.Beast) to try the client examples locally.
//
//   ./echo_server [port]   (default 8080)

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using asio::ip::tcp;

namespace {

constexpr auto use_nothrow = asio::as_tuple(asio::use_awaitable);

asio::awaitable<void> echo(tcp::socket socket) {
    websocket::stream<beast::tcp_stream> ws{std::move(socket)};
    if (auto [ec] = co_await ws.async_accept(use_nothrow); ec) {
        co_return;
    }
    beast::flat_buffer buffer;
    for (;;) {
        auto [read_ec, size] = co_await ws.async_read(buffer, use_nothrow);
        if (read_ec) {
            co_return; // closed by the client or connection lost
        }
        ws.text(ws.got_text());
        auto [write_ec, written] = co_await ws.async_write(buffer.data(), use_nothrow);
        if (write_ec) {
            co_return;
        }
        buffer.consume(size);
    }
}

asio::awaitable<void> listen(tcp::endpoint endpoint) {
    tcp::acceptor acceptor{co_await asio::this_coro::executor, endpoint};
    std::cout << "echo server listening on ws://" << endpoint << '\n';
    for (;;) {
        auto socket = co_await acceptor.async_accept(asio::use_awaitable);
        asio::co_spawn(acceptor.get_executor(), echo(std::move(socket)), asio::detached);
    }
}

} // namespace

int main(int argc, char** argv) { // NOLINT(misc-const-correctness): fixed signature
    std::uint16_t port = 8080;
    if (argc > 1) {
        const std::string_view text{argv[1]};
        if (std::from_chars(text.data(), text.data() + text.size(), port).ec != std::errc{}) {
            std::cerr << "usage: echo_server [port]\n";
            return 1;
        }
    }
    asio::io_context io;
    asio::co_spawn(io, listen({asio::ip::address_v4::loopback(), port}),
                   [](const std::exception_ptr& e) {
                       if (e) {
                           std::rethrow_exception(e);
                       }
                   });
    io.run();
}
