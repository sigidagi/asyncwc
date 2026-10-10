#include "asyncwc/connection.hpp"
#include "support/run.hpp"
#include "support/test_server.hpp"

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>

#include <array>
#include <chrono>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace asyncwc {
namespace {

using namespace std::chrono_literals;
using test::ServerSession;
using test::TestServer;
namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

class ConnectionTest : public ::testing::Test {
protected:
    template <typename T>
    T run(asio::awaitable<T> operation) {
        return test::run(io, std::move(operation));
    }

    /// A connection that is already connected to the test server.
    std::unique_ptr<Connection> connected(ConnectionOptions options = {}) {
        auto connection = std::make_unique<Connection>(io.get_executor(), std::move(options));
        const auto result = run(connection->connect(server.url()));
        EXPECT_TRUE(result) << result.error().message();
        return connection;
    }

    asio::io_context io;
    TestServer server{io};
};

TEST_F(ConnectionTest, ConnectsSendsReceivesAndCloses) {
    Connection connection{io.get_executor()};
    EXPECT_FALSE(connection.is_open());

    ASSERT_TRUE(run(connection.connect(server.url())));
    EXPECT_TRUE(connection.is_open());

    ASSERT_TRUE(run(connection.send_text("hello")));
    const auto reply = run(connection.receive());
    ASSERT_TRUE(reply) << reply.error().message();
    EXPECT_EQ(*reply, Message::text("hello"));

    const auto closed = run(connection.close());
    EXPECT_TRUE(closed) << closed.error().message();
    EXPECT_FALSE(connection.is_open());
}

TEST_F(ConnectionTest, EchoesBinaryMessages) {
    const auto connection = connected();
    const std::array bytes{std::byte{0}, std::byte{1}, std::byte{254}, std::byte{255}};
    ASSERT_TRUE(run(connection->send_binary(bytes)));
    const auto reply = run(connection->receive());
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->is_binary());
    EXPECT_TRUE(std::ranges::equal(reply->bytes(), bytes));

    ASSERT_TRUE(run(connection->send(Message::text("text again"))));
    EXPECT_EQ(run(connection->receive()).value(), Message::text("text again"));
}

TEST_F(ConnectionTest, ConnectsToAnEndpoint) {
    Connection connection{io.get_executor()};
    const Endpoint endpoint{.host = "127.0.0.1", .port = server.port(), .target = "/rooms/1?x=y"};
    ASSERT_TRUE(run(connection.connect(endpoint)));
    ASSERT_EQ(server.requests().size(), 1U);
    EXPECT_EQ(server.requests().front().target(), "/rooms/1?x=y");
}

TEST_F(ConnectionTest, SendsUserAgentHostAndCustomHeaders) {
    ConnectionOptions options;
    options.user_agent = "tester/1.0";
    options.headers = {{.name = "Authorization", .value = "Bearer token"},
                       {.name = "Sec-WebSocket-Protocol", .value = "chat"}};
    // Echo the requested sub-protocol, otherwise the client rejects the handshake.
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        session->stream().set_option(
            websocket::stream_base::decorator([](websocket::response_type& response) {
                response.set(http::field::sec_websocket_protocol, "chat");
            }));
        if (co_await session->accept()) {
            co_await session->echo();
        }
    });
    const auto connection = connected(options);

    ASSERT_EQ(server.requests().size(), 1U);
    const auto& request = server.requests().front();
    EXPECT_EQ(request[http::field::user_agent], "tester/1.0");
    EXPECT_EQ(request[http::field::authorization], "Bearer token");
    EXPECT_EQ(request[http::field::sec_websocket_protocol], "chat");
    EXPECT_EQ(request[http::field::host], std::format("127.0.0.1:{}", server.port()));
}

TEST_F(ConnectionTest, RejectsInvalidUrls) {
    Connection connection{io.get_executor()};
    const auto result = run(connection.connect("wss://127.0.0.1/"));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::unsupported_scheme);
    EXPECT_EQ(result.error().operation, Operation::parse_url);
}

TEST_F(ConnectionTest, RejectsInvalidOptions) {
    ConnectionOptions options;
    options.max_message_size = 0;
    Connection connection{io.get_executor(), options};
    const auto result = run(connection.connect(server.url()));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::invalid_configuration);
    EXPECT_EQ(server.connections(), 0U);
}

TEST_F(ConnectionTest, ConnectWhileConnectedFails) {
    const auto connection = connected();
    const auto result = run(connection->connect(server.url()));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::already_connected);
}

TEST_F(ConnectionTest, ConnectWhileConnectingFails) {
    server.on_connection(TestServer::unresponsive());
    Connection connection{io.get_executor()};
    const auto first = test::spawn(io, connection.connect(server.url()));
    test::run_until(io, [&] { return server.connections() == 1; });

    const auto second = run(connection.connect(server.url()));
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, Errc::already_connected);

    connection.cancel();
    test::run_until(io, [&] { return first->has_value(); });
    EXPECT_EQ(first->value().error().code, Errc::cancelled);
}

TEST_F(ConnectionTest, ConnectionsAreSingleUse) {
    const auto connection = connected();
    ASSERT_TRUE(run(connection->close()));
    const auto result = run(connection->connect(server.url()));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::invalid_state);
}

TEST_F(ConnectionTest, OperationsRequireAnOpenConnection) {
    Connection connection{io.get_executor()};

    const auto sent = run(connection.send_text("x"));
    ASSERT_FALSE(sent);
    EXPECT_EQ(sent.error().code, Errc::not_connected);
    EXPECT_EQ(sent.error().operation, Operation::write);

    const auto received = run(connection.receive());
    ASSERT_FALSE(received);
    EXPECT_EQ(received.error().code, Errc::not_connected);
    EXPECT_EQ(received.error().operation, Operation::read);

    const auto pinged = run(connection.ping());
    ASSERT_FALSE(pinged);
    EXPECT_EQ(pinged.error().code, Errc::not_connected);
    EXPECT_EQ(pinged.error().operation, Operation::ping);

    const auto closed = run(connection.close());
    ASSERT_FALSE(closed);
    EXPECT_EQ(closed.error().code, Errc::not_connected);
    EXPECT_EQ(closed.error().operation, Operation::close);
}

TEST_F(ConnectionTest, OnlyOneReceiveAtATime) {
    const auto connection = connected();
    const auto first = test::spawn(io, connection->receive());
    test::run_for(io, 10ms);

    const auto second = run(connection->receive());
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, Errc::operation_in_progress);

    ASSERT_TRUE(run(connection->send_text("for the first")));
    test::run_until(io, [&] { return first->has_value(); });
    EXPECT_EQ(first->value()->payload, "for the first");
}

TEST_F(ConnectionTest, ReportsUnresolvableHosts) {
    Connection connection{io.get_executor()};
    const auto result = test::run(io, connection.connect("ws://host.invalid/"), 30s);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, Operation::resolve);
    EXPECT_EQ(result.error().detail, "host.invalid");
}

TEST_F(ConnectionTest, ReportsRefusedConnections) {
    Connection connection{io.get_executor()};
    const auto result =
        run(connection.connect(std::format("ws://127.0.0.1:{}/", test::unused_port(io))));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, Operation::connect);
    EXPECT_EQ(result.error().code, std::errc::connection_refused);
    EXPECT_FALSE(connection.is_open());
}

TEST_F(ConnectionTest, ReportsRejectedHandshakes) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        co_await session->reject(http::status::forbidden);
    });
    Connection connection{io.get_executor()};
    const auto result = run(connection.connect(server.url()));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::handshake_rejected);
    EXPECT_EQ(result.error().operation, Operation::handshake);
    EXPECT_EQ(result.error().detail, "HTTP 403 Forbidden");
}

TEST_F(ConnectionTest, HandshakeTimesOut) {
    server.on_connection(TestServer::unresponsive());
    ConnectionOptions options;
    options.handshake_timeout = 100ms;
    Connection connection{io.get_executor(), options};
    const auto result = run(connection.connect(server.url()));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::timeout);
    EXPECT_EQ(result.error().code, std::errc::timed_out);
    EXPECT_EQ(result.error().operation, Operation::handshake);
}

TEST_F(ConnectionTest, ReportsConnectionsDroppedDuringTheHandshake) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        session->drop();
        co_return;
    });
    Connection connection{io.get_executor()};
    const auto result = run(connection.connect(server.url()));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, Operation::handshake);
    EXPECT_NE(result.error().code, Errc::timeout);
}

TEST_F(ConnectionTest, ReportsTheCloseFrameOfThePeer) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (co_await session->accept()) {
            co_await session->close(websocket::close_code::going_away, "maintenance");
        }
    });
    const auto connection = connected();

    const auto result = run(connection->receive());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::connection_closed);
    EXPECT_EQ(result.error().detail, "close code 1001: maintenance");
    EXPECT_EQ(connection->close_info(),
              (CloseInfo{.code = CloseCode::going_away, .reason = "maintenance"}));
    EXPECT_FALSE(connection->is_open());

    EXPECT_EQ(run(connection->receive()).error().code, Errc::not_connected);
}

TEST_F(ConnectionTest, ReportsCloseFramesWithoutReason) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (co_await session->accept()) {
            co_await session->close();
        }
    });
    const auto connection = connected();
    const auto result = run(connection->receive());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().detail, "close code 1000");
    EXPECT_EQ(connection->close_info(), (CloseInfo{.code = CloseCode::normal, .reason = ""}));
}

TEST_F(ConnectionTest, ReportsAbruptDisconnects) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (co_await session->accept()) {
            session->drop();
        }
    });
    const auto connection = connected();
    const auto result = run(connection->receive());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, Operation::read);
    EXPECT_NE(result.error().code, Errc::connection_closed);
    EXPECT_FALSE(connection->close_info());
    EXPECT_FALSE(connection->is_open());
}

TEST_F(ConnectionTest, PingIsAnsweredWithPong) {
    const auto connection = connected();
    std::vector<std::string> pongs;
    connection->on_pong([&](std::string_view payload) { pongs.emplace_back(payload); });

    ASSERT_TRUE(run(connection->ping("abc")));
    ASSERT_TRUE(run(connection->send_text("after the ping")));
    // Control frames are processed while reading, so the pong shows up before the echo.
    EXPECT_EQ(run(connection->receive()).value().payload, "after the ping");
    EXPECT_EQ(pongs, std::vector<std::string>{"abc"});
}

TEST_F(ConnectionTest, PongHandlerExceptionsAreContained) {
    const auto connection = connected();
    int calls = 0;
    connection->on_pong([&](std::string_view payload) {
        ++calls;
        if (payload == "std") {
            throw std::runtime_error{"boom"};
        }
        throw 42; // NOLINT(hicpp-exception-baseclass)
    });

    ASSERT_TRUE(run(connection->ping("std")));
    ASSERT_TRUE(run(connection->ping("other")));
    ASSERT_TRUE(run(connection->send_text("still alive")));
    EXPECT_EQ(run(connection->receive()).value().payload, "still alive");
    EXPECT_EQ(calls, 2);
}

TEST_F(ConnectionTest, AnswersPingsFromThePeer) {
    server.on_connection([](std::shared_ptr<ServerSession> session) -> asio::awaitable<void> {
        if (!co_await session->accept() || !co_await session->ping("server")) {
            co_return;
        }
        // Reading processes the client's pong, which arrives before the next message.
        if (co_await session->read()) {
            co_await session->write_text(std::format("pongs={}", session->pongs_received()));
        }
    });
    const auto connection = connected();
    const auto reply = test::spawn(io, connection->receive());
    test::run_for(io, 100ms); // the pending receive answers the server's ping

    ASSERT_TRUE(run(connection->send_text("count")));
    test::run_until(io, [&] { return reply->has_value(); });
    EXPECT_EQ(reply->value()->payload, "pongs=1");
}

TEST_F(ConnectionTest, RejectsOversizedPingPayloads) {
    const auto connection = connected();
    const auto result = run(connection->ping(std::string(126, 'x')));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::invalid_argument);
    EXPECT_TRUE(run(connection->ping(std::string(125, 'x'))));
}

TEST_F(ConnectionTest, RejectsInvalidCloseArguments) {
    const auto connection = connected();
    const auto reserved = run(connection->close(CloseCode::no_status));
    ASSERT_FALSE(reserved);
    EXPECT_EQ(reserved.error().code, Errc::invalid_argument);

    const auto long_reason = run(connection->close(CloseCode::normal, std::string(124, 'x')));
    ASSERT_FALSE(long_reason);
    EXPECT_EQ(long_reason.error().code, Errc::invalid_argument);

    EXPECT_TRUE(connection->is_open());
}

TEST_F(ConnectionTest, CloseWhileReceiving) {
    const auto connection = connected();
    const auto pending = test::spawn(io, connection->receive());
    test::run_for(io, 10ms);

    const auto closed = run(connection->close(CloseCode::going_away, "bye"));
    EXPECT_TRUE(closed) << closed.error().message();
    test::run_until(io, [&] { return pending->has_value(); });
    EXPECT_EQ(pending->value().error().code, Errc::connection_closed);
}

TEST_F(ConnectionTest, CloseTwiceFails) {
    const auto connection = connected();
    const auto first = test::spawn(io, connection->close());
    const auto second = run(connection->close());
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code, Errc::operation_in_progress);
    test::run_until(io, [&] { return first->has_value(); });
    EXPECT_TRUE(first->value());
}

TEST_F(ConnectionTest, CloseTimesOutWhenThePeerDoesNotAnswer) {
    server.on_connection(TestServer::silent());
    ConnectionOptions options;
    options.handshake_timeout = 100ms;
    const auto connection = connected(options);
    const auto result = run(connection->close());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::timeout);
    EXPECT_EQ(result.error().operation, Operation::close);
    EXPECT_FALSE(connection->is_open());
}

TEST_F(ConnectionTest, CancelAbortsPendingReceive) {
    const auto connection = connected();
    const auto pending = test::spawn(io, connection->receive());
    test::run_for(io, 10ms);

    connection->cancel();
    EXPECT_FALSE(connection->is_open());
    test::run_until(io, [&] { return pending->has_value(); });
    EXPECT_EQ(pending->value().error().code, Errc::cancelled);
    EXPECT_EQ(pending->value().error().code, std::errc::operation_canceled);

    connection->cancel(); // idempotent
}

TEST_F(ConnectionTest, CancelFailsWritesInFlightAndQueued) {
    server.on_connection(TestServer::silent()); // never reads, so the socket buffers fill up
    const auto connection = connected();
    const auto huge =
        test::spawn(io, connection->send_text(std::string(std::size_t{64} * 1024 * 1024, 'x')));
    const auto queued = test::spawn(io, connection->send_text("queued"));
    const auto ping = test::spawn(io, connection->ping("queued ping"));
    test::run_for(io, 50ms);
    ASSERT_FALSE(huge->has_value());
    ASSERT_FALSE(queued->has_value());

    connection->cancel();
    test::run_until(io,
                    [&] { return huge->has_value() && queued->has_value() && ping->has_value(); });
    EXPECT_EQ(huge->value().error().code, Errc::cancelled);
    EXPECT_EQ(queued->value().error().code, Errc::not_connected);
    EXPECT_EQ(ping->value().error().code, Errc::not_connected);
}

TEST_F(ConnectionTest, ConcurrentSendsKeepTheirOrder) {
    const auto connection = connected();
    std::vector<std::shared_ptr<std::optional<Result<void>>>> sends;
    sends.reserve(20);
    for (int i = 0; i < 20; ++i) {
        sends.push_back(test::spawn(io, connection->send_text(std::to_string(i))));
    }
    for (int i = 0; i < 20; ++i) {
        const auto message = run(connection->receive());
        ASSERT_TRUE(message);
        EXPECT_EQ(message->payload, std::to_string(i));
    }
    for (const auto& send : sends) {
        ASSERT_TRUE(send->has_value());
        EXPECT_TRUE(send->value());
    }
}

TEST_F(ConnectionTest, RejectsOversizedIncomingMessages) {
    ConnectionOptions options;
    options.max_message_size = 8;
    const auto connection = connected(options);
    ASSERT_TRUE(run(connection->send_text("longer than eight bytes")));
    const auto result = run(connection->receive());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().operation, Operation::read);
    EXPECT_FALSE(connection->is_open());
}

TEST_F(ConnectionTest, DestroyingTheConnectionCancelsPendingOperations) {
    auto connection = connected();
    const auto pending = test::spawn(io, connection->receive());
    test::run_for(io, 10ms);
    connection.reset();
    test::run_until(io, [&] { return pending->has_value(); });
    EXPECT_EQ(pending->value().error().code, Errc::cancelled);
}

TEST_F(ConnectionTest, AwaitingAfterDestructionIsSafe) {
    auto connection = std::make_unique<Connection>(io.get_executor());
    auto operation = connection->connect(server.url());
    connection.reset();
    const auto result = run(std::move(operation));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::invalid_state);
}

TEST_F(ConnectionTest, ExposesItsExecutor) {
    const Connection connection{io.get_executor()};
    EXPECT_EQ(connection.get_executor(), asio::any_io_executor{io.get_executor()});
}

} // namespace
} // namespace asyncwc
