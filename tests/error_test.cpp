#include "asyncwc/error.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <system_error>

namespace asyncwc {
namespace {

TEST(ErrorTest, CategoryIsNamedAsyncwc) {
    EXPECT_STREQ(error_category().name(), "asyncwc");
    EXPECT_EQ(&make_error_code(Errc::timeout).category(), &error_category());
}

TEST(ErrorTest, EveryErrcHasADistinctMessage) {
    std::set<std::string> messages;
    for (int value = static_cast<int>(Errc::invalid_url);
         value <= static_cast<int>(Errc::cancelled); ++value) {
        const auto message = error_category().message(value);
        EXPECT_NE(message, "unknown asyncwc error") << value;
        messages.insert(message);
    }
    EXPECT_EQ(messages.size(), static_cast<std::size_t>(Errc::cancelled));
}

TEST(ErrorTest, UnknownValuesHaveAFallbackMessage) {
    EXPECT_EQ(error_category().message(0), "unknown asyncwc error");
    EXPECT_EQ(error_category().message(1000), "unknown asyncwc error");
}

TEST(ErrorTest, ErrcConvertsImplicitlyToErrorCode) {
    const std::error_code code = Errc::not_connected;
    EXPECT_EQ(code, Errc::not_connected);
    EXPECT_NE(code, Errc::cancelled);
    EXPECT_EQ(code.message(), "not connected");
}

TEST(ErrorTest, MapsToPortableErrorConditions) {
    EXPECT_EQ(make_error_code(Errc::invalid_url), std::errc::invalid_argument);
    EXPECT_EQ(make_error_code(Errc::unsupported_scheme), std::errc::invalid_argument);
    EXPECT_EQ(make_error_code(Errc::invalid_configuration), std::errc::invalid_argument);
    EXPECT_EQ(make_error_code(Errc::invalid_argument), std::errc::invalid_argument);
    EXPECT_EQ(make_error_code(Errc::not_connected), std::errc::not_connected);
    EXPECT_EQ(make_error_code(Errc::already_connected), std::errc::already_connected);
    EXPECT_EQ(make_error_code(Errc::operation_in_progress), std::errc::operation_in_progress);
    EXPECT_EQ(make_error_code(Errc::timeout), std::errc::timed_out);
    EXPECT_EQ(make_error_code(Errc::pong_timeout), std::errc::timed_out);
    EXPECT_EQ(make_error_code(Errc::cancelled), std::errc::operation_canceled);

    const auto own = make_error_code(Errc::handshake_rejected).default_error_condition();
    EXPECT_EQ(&own.category(), &error_category());
    EXPECT_EQ(own.value(), static_cast<int>(Errc::handshake_rejected));
}

TEST(ErrorTest, OperationsHaveNames) {
    EXPECT_EQ(to_string(Operation::parse_url), "parse_url");
    EXPECT_EQ(to_string(Operation::validate_config), "validate_config");
    EXPECT_EQ(to_string(Operation::resolve), "resolve");
    EXPECT_EQ(to_string(Operation::connect), "connect");
    EXPECT_EQ(to_string(Operation::handshake), "handshake");
    EXPECT_EQ(to_string(Operation::read), "read");
    EXPECT_EQ(to_string(Operation::write), "write");
    EXPECT_EQ(to_string(Operation::ping), "ping");
    EXPECT_EQ(to_string(Operation::close), "close");
    EXPECT_EQ(to_string(Operation::heartbeat), "heartbeat");
    EXPECT_EQ(to_string(Operation::reconnect), "reconnect");
    EXPECT_EQ(to_string(Operation::run), "run");
    EXPECT_EQ(to_string(static_cast<Operation>(200)), "unknown");
}

TEST(ErrorTest, MessageCombinesOperationCodeAndDetail) {
    const Error plain{.code = Errc::timeout, .operation = Operation::handshake, .detail = {}};
    EXPECT_EQ(plain.message(), "handshake: operation timed out");

    const Error detailed{
        .code = Errc::handshake_rejected, .operation = Operation::handshake, .detail = "HTTP 403"};
    EXPECT_EQ(detailed.message(),
              "handshake: WebSocket handshake rejected by the server (HTTP 403)");
}

TEST(ErrorTest, MakeUnexpectedBuildsAnError) {
    const Result<int> result = make_unexpected(Errc::cancelled, Operation::read, "why");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, Errc::cancelled);
    EXPECT_EQ(result.error().operation, Operation::read);
    EXPECT_EQ(result.error().detail, "why");
}

} // namespace
} // namespace asyncwc
