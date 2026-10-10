// Connects, sends one message, prints the reply and closes.
//
//   ./echo_server 8080 &
//   ./simple_client ws://127.0.0.1:8080

#include <asyncwc/asyncwc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <spdlog/spdlog.h>

#include <string>

namespace asio = boost::asio;

namespace {

asio::awaitable<void> talk(std::string url) {
    asyncwc::Connection connection{co_await asio::this_coro::executor};

    if (auto connected = co_await connection.connect(url); !connected) {
        spdlog::error("{}", connected.error().message());
        co_return;
    }
    if (auto sent = co_await connection.send_text("Hello, WebSocket!"); !sent) {
        spdlog::error("{}", sent.error().message());
        co_return;
    }
    if (auto reply = co_await connection.receive()) {
        spdlog::info("received: {}", reply->payload);
    } else {
        spdlog::error("{}", reply.error().message());
    }
    if (auto closed = co_await connection.close(); !closed) {
        spdlog::warn("{}", closed.error().message());
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string url = argc > 1 ? argv[1] : "ws://127.0.0.1:8080";

    asio::io_context io;
    asio::co_spawn(io, talk(url), asio::detached);
    io.run();
}
