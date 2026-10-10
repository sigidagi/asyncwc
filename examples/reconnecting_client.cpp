// Stays connected: heartbeats detect dead connections, lost connections are re-established
// with exponential backoff. Sends a counter every second; Ctrl+C closes gracefully.
//
//   ./echo_server 8080 &
//   ./reconnecting_client ws://127.0.0.1:8080
//   (stop and restart echo_server to watch the client reconnect)

#include <asyncwc/asyncwc.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <csignal>
#include <format>
#include <string>

namespace asio = boost::asio;
using namespace std::chrono_literals;

namespace {

// Sends "tick N" once per second while connected; gaps while reconnecting are fine.
asio::awaitable<void> ticker(asyncwc::Client& client) {
    asio::steady_timer timer{client.get_executor()};
    for (int tick = 1; client.state() != asyncwc::ClientState::stopped; ++tick) {
        timer.expires_after(1s);
        co_await timer.async_wait(asio::use_awaitable);
        if (client.state() != asyncwc::ClientState::connected) {
            continue;
        }
        if (auto sent = co_await client.send_text(std::format("tick {}", tick)); !sent) {
            spdlog::warn("send failed: {}", sent.error().message());
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    asyncwc::logger()->set_level(spdlog::level::debug);

    asyncwc::ClientConfig config;
    config.url = argc > 1 ? argv[1] : "ws://127.0.0.1:8080";
    config.heartbeat = {.interval = 5s, .timeout = 3s};
    config.reconnect.initial_delay = 500ms;
    config.reconnect.max_delay = 10s;

    asio::io_context io;
    asyncwc::Client client{io.get_executor(), config};

    client.on_open([] { spdlog::info("connected"); });
    client.on_message(
        [](const asyncwc::Message& message) { spdlog::info("received: {}", message.payload); });
    client.on_disconnect(
        [](const asyncwc::Error& reason) { spdlog::warn("disconnected: {}", reason.message()); });
    client.on_reconnect([](std::size_t attempt, std::chrono::milliseconds delay) {
        spdlog::info("reconnect attempt {} in {} ms", attempt, delay.count());
    });
    client.on_pong([](std::chrono::microseconds round_trip) {
        spdlog::debug("pong after {} us", round_trip.count());
    });

    asio::signal_set signals{io, SIGINT, SIGTERM};
    signals.async_wait([&](const boost::system::error_code&, int) {
        spdlog::info("closing");
        asio::co_spawn(io, client.close(asyncwc::CloseCode::going_away, "bye"), asio::detached);
    });

    asio::co_spawn(io, client.run(), [&](const std::exception_ptr&, asyncwc::Result<void> result) {
        if (!result) {
            spdlog::error("client gave up: {}", result.error().message());
        }
        signals.cancel();
    });
    asio::co_spawn(io, ticker(client), asio::detached);
    io.run();
}
