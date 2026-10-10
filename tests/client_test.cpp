#include "asyncwc/client.hpp"
#include "support/run.hpp"
#include "support/test_server.hpp"

#include <gtest/gtest.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace asyncwc {
namespace {

using namespace std::chrono_literals;
using test::ServerSession;
using test::TestServer;
namespace asio = boost::asio;
namespace websocket = boost::beast::websocket;

/// Fast and deterministic settings for tests: no heartbeat, quick reconnects.
ClientConfig fast_config(std::string url) {
    ClientConfig config;
    config.url = std::move(url);
    config.heartbeat.interval = 0ms;
    config.reconnect.initial_delay = 10ms;
    config.reconnect.max_delay = 40ms;
    config.reconnect.jitter = 0.0;
    return config;
}

/// Accepts the first connection and closes it right away, echoes on later ones.
/// (The server copies handlers per connection, hence the shared counter.)
test::SessionHandler close_first_then_echo() {
    return [count = std::make_shared<int>(0)](
               std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        const bool first = (*count)++ == 0;
        if (!co_await session->accept()) {
            co_return;
        }
        if (first) {
            co_await session->close(websocket::close_code::service_restart, "restart");
        } else {
            co_await session->echo();
        }
    };
}

class ClientTest : public ::testing::Test {
protected:
    template <typename T>
    T run(asio::awaitable<T> operation) {
        return test::run(io, std::move(operation));
    }

    /// Spawns `client.close()` from inside a handler.
    void close_soon(Client& client) {
        asio::co_spawn(io, client.close(), [](const std::exception_ptr&, Result<void>) {});
    }

    std::string refused_url() { return std::format("ws://127.0.0.1:{}/", test::unused_port(io)); }

    asio::io_context io;
    TestServer server{io};
};

TEST_F(ClientTest, ConnectsExchangesMessagesAndCloses) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (co_await session->accept() && co_await session->write_text("welcome")) {
            co_await session->echo();
        }
    });
    Client client{io.get_executor(), fast_config(server.url())};
    EXPECT_EQ(client.state(), ClientState::idle);

    std::vector<std::string> received;
    std::vector<Error> disconnects;
    int opened = 0;
    client.on_open([&] {
        ++opened;
        EXPECT_EQ(client.state(), ClientState::connected);
        asio::co_spawn(io, client.send_text("hi"),
                       [](const std::exception_ptr&, Result<void> sent) { EXPECT_TRUE(sent); });
    });
    client.on_message([&](const Message& message) {
        received.push_back(message.payload);
        if (received.size() == 2) {
            close_soon(client);
        }
    });
    client.on_disconnect([&](const Error& reason) { disconnects.push_back(reason); });

    const auto result = run(client.run());
    EXPECT_TRUE(result) << result.error().message();
    EXPECT_EQ(opened, 1);
    EXPECT_EQ(received, (std::vector<std::string>{"welcome", "hi"}));
    ASSERT_EQ(disconnects.size(), 1U);
    EXPECT_EQ(disconnects.front().code, Errc::connection_closed);
    EXPECT_EQ(client.state(), ClientState::stopped);
}

TEST_F(ClientTest, SendsBinaryAndMessageObjects) {
    Client client{io.get_executor(), fast_config(server.url())};
    std::vector<Message> received;
    const std::array bytes{std::byte{1}, std::byte{2}};
    client.on_open([&] {
        asio::co_spawn(io, client.send_binary(bytes), asio::detached);
        asio::co_spawn(io, client.send(Message::text("text")), asio::detached);
    });
    client.on_message([&](const Message& message) {
        received.push_back(message);
        if (received.size() == 2) {
            close_soon(client);
        }
    });
    ASSERT_TRUE(run(client.run()));
    ASSERT_EQ(received.size(), 2U);
    EXPECT_EQ(received[0], Message::binary(bytes));
    EXPECT_EQ(received[1], Message::text("text"));
}

TEST_F(ClientTest, SendFailsWhenNotConnected) {
    Client client{io.get_executor(), fast_config(server.url())};
    const auto result = run(client.send_text("nobody listens"));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::not_connected);
    EXPECT_EQ(result.error().detail, "client is idle");
}

TEST_F(ClientTest, RejectsAConcurrentRun) {
    Client client{io.get_executor(), fast_config(server.url())};
    const auto first = test::spawn(io, client.run());
    test::run_until(io, [&] { return client.state() == ClientState::connected; });

    const auto second = run(client.run());
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, Errc::operation_in_progress);

    client.stop();
    test::run_until(io, [&] { return first->has_value(); });
    EXPECT_TRUE(first->value());
}

TEST_F(ClientTest, RejectsInvalidConfiguration) {
    auto config = fast_config(server.url());
    config.heartbeat = {.interval = 10ms, .timeout = 20ms};
    Client client{io.get_executor(), config};
    const auto result = run(client.run());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::invalid_configuration);
    EXPECT_EQ(client.state(), ClientState::idle);
}

TEST_F(ClientTest, RejectsInvalidUrls) {
    Client client{io.get_executor(), fast_config("http://127.0.0.1/")};
    const auto result = run(client.run());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::unsupported_scheme);
}

TEST_F(ClientTest, ReconnectsAfterThePeerClosed) {
    server.on_connection(close_first_then_echo());
    Client client{io.get_executor(), fast_config(server.url())};

    std::vector<Error> disconnects;
    std::vector<std::size_t> attempts;
    int opened = 0;
    client.on_open([&] {
        if (++opened == 2) {
            close_soon(client);
        }
    });
    client.on_disconnect([&](const Error& reason) { disconnects.push_back(reason); });
    client.on_reconnect([&](std::size_t attempt, std::chrono::milliseconds delay) {
        EXPECT_EQ(client.state(), ClientState::reconnecting);
        EXPECT_EQ(delay, 10ms);
        attempts.push_back(attempt);
    });

    ASSERT_TRUE(run(client.run()));
    EXPECT_EQ(opened, 2);
    EXPECT_EQ(attempts, std::vector<std::size_t>{1});
    ASSERT_EQ(disconnects.size(), 2U);
    EXPECT_EQ(disconnects[0].code, Errc::connection_closed);
    EXPECT_EQ(disconnects[0].detail, "close code 1012: restart");
    EXPECT_EQ(server.connections(), 2U);
}

TEST_F(ClientTest, ReconnectsWhenTheServerComesBack) {
    const auto port = server.port();
    std::unique_ptr<TestServer> replacement;
    Client client{io.get_executor(), fast_config(server.url())};
    int opened = 0;
    client.on_open([&] {
        if (++opened == 1) {
            server.stop(); // the server goes down while we are connected
        } else {
            close_soon(client);
        }
    });
    client.on_reconnect([&](std::size_t attempt, std::chrono::milliseconds) {
        if (attempt == 3) { // two attempts were refused by now
            replacement = std::make_unique<TestServer>(io, port);
        }
    });
    std::vector<Error> errors;
    client.on_error([&](const Error& error) { errors.push_back(error); });

    ASSERT_TRUE(run(client.run()));
    EXPECT_EQ(opened, 2);
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors.front().code, std::errc::connection_refused);
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->connections(), 1U);
}

TEST_F(ClientTest, GivesUpAfterMaxAttempts) {
    auto config = fast_config(refused_url());
    config.reconnect.max_attempts = 2;
    Client client{io.get_executor(), config};
    int errors = 0;
    std::vector<std::chrono::milliseconds> delays;
    client.on_error([&](const Error&) { ++errors; });
    client.on_reconnect(
        [&](std::size_t, std::chrono::milliseconds delay) { delays.push_back(delay); });

    const auto result = run(client.run());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::reconnect_attempts_exhausted);
    EXPECT_EQ(result.error().operation, Operation::reconnect);
    EXPECT_NE(result.error().detail.find("connect:"), std::string::npos);
    EXPECT_EQ(errors, 3);
    EXPECT_EQ(delays, (std::vector<std::chrono::milliseconds>{10ms, 20ms}));
    EXPECT_EQ(client.state(), ClientState::stopped);
}

TEST_F(ClientTest, WithoutReconnectTheFirstConnectErrorEndsRun) {
    auto config = fast_config(refused_url());
    config.reconnect.enabled = false;
    Client client{io.get_executor(), config};
    const auto result = run(client.run());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, Operation::connect);
    EXPECT_EQ(result.error().code, std::errc::connection_refused);
}

TEST_F(ClientTest, WithoutReconnectADisconnectEndsRun) {
    server.on_connection(close_first_then_echo());
    auto config = fast_config(server.url());
    config.reconnect.enabled = false;
    Client client{io.get_executor(), config};
    const auto result = run(client.run());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::connection_closed);
    EXPECT_EQ(server.connections(), 1U);
}

TEST_F(ClientTest, HeartbeatPingsAreAnswered) {
    auto config = fast_config(server.url());
    // Generous timeout so slow CI runners never miss a pong.
    config.heartbeat = {.interval = 50ms, .timeout = 50ms};
    Client client{io.get_executor(), config};
    std::vector<std::chrono::microseconds> round_trips;
    client.on_pong([&](std::chrono::microseconds round_trip) {
        round_trips.push_back(round_trip);
        if (round_trips.size() == 3) {
            close_soon(client);
        }
    });
    int disconnects = 0;
    client.on_disconnect([&](const Error& reason) {
        ++disconnects;
        EXPECT_EQ(reason.code, Errc::connection_closed);
    });

    ASSERT_TRUE(run(client.run()));
    ASSERT_EQ(round_trips.size(), 3U);
    for (const auto round_trip : round_trips) {
        EXPECT_GE(round_trip.count(), 0);
        EXPECT_LE(round_trip, 50ms);
    }
    EXPECT_EQ(disconnects, 1);
}

TEST_F(ClientTest, MissingPongDropsTheConnectionAndReconnects) {
    server.on_connection([count = std::make_shared<int>(0)](
                             std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        const bool first = (*count)++ == 0;
        if (co_await session->accept()) {
            co_await (first ? session->hold() : session->echo());
        }
    });
    auto config = fast_config(server.url());
    // The first session never answers; the second must answer within the timeout.
    config.heartbeat = {.interval = 50ms, .timeout = 50ms};
    Client client{io.get_executor(), config};
    std::vector<Error> disconnects;
    int opened = 0;
    int pongs = 0;
    client.on_disconnect([&](const Error& reason) { disconnects.push_back(reason); });
    client.on_pong([&](std::chrono::microseconds) {
        if (++pongs == 1) {
            close_soon(client);
        }
    });
    client.on_open([&] { ++opened; });

    ASSERT_TRUE(run(client.run()));
    EXPECT_EQ(opened, 2);
    ASSERT_EQ(disconnects.size(), 2U);
    EXPECT_EQ(disconnects[0].code, Errc::pong_timeout);
    EXPECT_EQ(disconnects[0].operation, Operation::heartbeat);
}

TEST_F(ClientTest, UnsolicitedPongsAreIgnored) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (co_await session->accept() && co_await session->pong("unsolicited") &&
            co_await session->write_text("done")) {
            co_await session->echo();
        }
    });
    auto config = fast_config(server.url());
    config.heartbeat = {.interval = 10s, .timeout = 1s};
    Client client{io.get_executor(), config};
    int pongs = 0;
    client.on_pong([&](std::chrono::microseconds) { ++pongs; });
    client.on_message([&](const Message&) { close_soon(client); });
    ASSERT_TRUE(run(client.run()));
    EXPECT_EQ(pongs, 0);
}

TEST_F(ClientTest, StopFromTheReconnectHandler) {
    Client client{io.get_executor(), fast_config(refused_url())};
    client.on_reconnect([&](std::size_t, std::chrono::milliseconds) { client.stop(); });
    EXPECT_TRUE(run(client.run()));
}

TEST_F(ClientTest, StopWhileWaitingToReconnect) {
    auto config = fast_config(refused_url());
    config.reconnect.initial_delay = 1h;
    config.reconnect.max_delay = 1h;
    Client client{io.get_executor(), config};
    client.on_reconnect([&](std::size_t, std::chrono::milliseconds) {
        asio::co_spawn(
            io,
            [&]() -> asio::awaitable<void> {
                co_await test::sleep_for(20ms);
                client.stop();
            },
            asio::detached);
    });
    EXPECT_TRUE(run(client.run()));
}

TEST_F(ClientTest, StopWhileConnecting) {
    server.on_connection(TestServer::unresponsive());
    Client client{io.get_executor(), fast_config(server.url())};
    int errors = 0;
    client.on_error([&](const Error&) { ++errors; });
    const auto pending = test::spawn(io, client.run());
    test::run_until(io, [&] { return server.connections() == 1; });
    EXPECT_EQ(client.state(), ClientState::connecting);

    client.stop();
    test::run_until(io, [&] { return pending->has_value(); });
    EXPECT_TRUE(pending->value());
    EXPECT_EQ(errors, 0); // a deliberate stop is not reported as an error
}

TEST_F(ClientTest, CloseWhileConnecting) {
    server.on_connection(TestServer::unresponsive());
    Client client{io.get_executor(), fast_config(server.url())};
    const auto pending = test::spawn(io, client.run());
    test::run_until(io, [&] { return server.connections() == 1; });

    EXPECT_TRUE(run(client.close()));
    test::run_until(io, [&] { return pending->has_value(); });
    EXPECT_TRUE(pending->value());
}

TEST_F(ClientTest, CloseBeforeRunIsANoOp) {
    Client client{io.get_executor(), fast_config(server.url())};
    EXPECT_TRUE(run(client.close()));
    client.stop();
    EXPECT_EQ(client.state(), ClientState::idle);
}

TEST_F(ClientTest, CloseValidatesItsArguments) {
    Client client{io.get_executor(), fast_config(server.url())};
    const auto result = run(client.close(CloseCode::abnormal));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::invalid_argument);
}

TEST_F(ClientTest, SecondCloseDoesNotAbortThePendingOne) {
    Client client{io.get_executor(), fast_config(server.url())};
    std::optional<Result<void>> first;
    std::optional<Result<void>> second;
    client.on_open([&] {
        asio::co_spawn(io, client.close(), [&](const std::exception_ptr&, Result<void> result) {
            first = std::move(result);
        });
        asio::co_spawn(io, client.close(), [&](const std::exception_ptr&, Result<void> result) {
            second = std::move(result);
        });
    });
    EXPECT_TRUE(run(client.run()));
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(*first);
    ASSERT_TRUE(second.has_value());
    ASSERT_FALSE(*second);
    EXPECT_EQ(second->error().code, Errc::operation_in_progress);
}

TEST_F(ClientTest, CloseReportsAFailedClosingHandshake) {
    server.on_connection(TestServer::silent());
    auto config = fast_config(server.url());
    config.connection.handshake_timeout = 100ms;
    Client client{io.get_executor(), config};
    std::optional<Result<void>> closed;
    client.on_open([&] {
        asio::co_spawn(io, client.close(), [&](const std::exception_ptr&, Result<void> result) {
            closed = std::move(result);
        });
    });
    EXPECT_TRUE(run(client.run()));
    ASSERT_TRUE(closed.has_value());
    ASSERT_FALSE(*closed);
    EXPECT_EQ(closed->error().code, Errc::timeout);
}

TEST_F(ClientTest, AsioCancellationEndsRun) {
    Client client{io.get_executor(), fast_config(server.url())};
    run([&]() -> asio::awaitable<void> {
        using namespace asio::experimental::awaitable_operators;
        const auto winner = co_await (client.run() || test::sleep_for(50ms));
        EXPECT_EQ(winner.index(), 1U);
    }());
    EXPECT_EQ(client.state(), ClientState::stopped);
}

TEST_F(ClientTest, AsioCancellationWhileWaitingToReconnect) {
    auto config = fast_config(refused_url());
    config.reconnect.initial_delay = 1h;
    config.reconnect.max_delay = 1h;
    Client client{io.get_executor(), config};
    run([&]() -> asio::awaitable<void> {
        using namespace asio::experimental::awaitable_operators;
        co_await (client.run() || test::sleep_for(50ms));
    }());
    EXPECT_EQ(client.state(), ClientState::stopped);
}

TEST_F(ClientTest, HandlerExceptionsAreContained) {
    Client client{io.get_executor(), fast_config(server.url())};
    int messages = 0;
    client.on_open([&] {
        asio::co_spawn(io, client.send_text("one"), asio::detached);
        asio::co_spawn(io, client.send_text("two"), asio::detached);
        throw std::runtime_error{"open failed"};
    });
    client.on_message([&](const Message&) {
        if (++messages == 2) {
            close_soon(client);
        }
        throw 7; // NOLINT(hicpp-exception-baseclass)
    });
    ASSERT_TRUE(run(client.run()));
    EXPECT_EQ(messages, 2);
}

TEST_F(ClientTest, DestroyingTheClientStopsRunWithoutCallingHandlers) {
    auto client = std::make_unique<Client>(io.get_executor(), fast_config(server.url()));
    int disconnects = 0;
    client->on_disconnect([&](const Error&) { ++disconnects; });
    const auto pending = test::spawn(io, client->run());
    test::run_until(io, [&] { return client->state() == ClientState::connected; });

    client.reset();
    test::run_until(io, [&] { return pending->has_value(); });
    EXPECT_TRUE(pending->value());
    EXPECT_EQ(disconnects, 0);
}

TEST_F(ClientTest, CanRunAgainAfterStopping) {
    Client client{io.get_executor(), fast_config(server.url())};
    int opened = 0;
    client.on_open([&] {
        ++opened;
        close_soon(client);
    });
    ASSERT_TRUE(run(client.run()));
    ASSERT_TRUE(run(client.run()));
    EXPECT_EQ(opened, 2);
}

TEST_F(ClientTest, ExposesConfigAndExecutor) {
    const Client client{io.get_executor(), fast_config(server.url())};
    EXPECT_EQ(client.config().url, server.url());
    EXPECT_EQ(client.get_executor(), asio::any_io_executor{io.get_executor()});
}

TEST(ClientStateTest, HasNames) {
    EXPECT_EQ(to_string(ClientState::idle), "idle");
    EXPECT_EQ(to_string(ClientState::connecting), "connecting");
    EXPECT_EQ(to_string(ClientState::connected), "connected");
    EXPECT_EQ(to_string(ClientState::reconnecting), "reconnecting");
    EXPECT_EQ(to_string(ClientState::closing), "closing");
    EXPECT_EQ(to_string(ClientState::stopped), "stopped");
    EXPECT_EQ(to_string(static_cast<ClientState>(99)), "unknown");
}

} // namespace
} // namespace asyncwc
