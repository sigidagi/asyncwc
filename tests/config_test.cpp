#include "asyncwc/config.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace asyncwc {
namespace {

using std::chrono::milliseconds;

template <typename Options>
std::string rejection(const Options& options) {
    const auto result = validate(options);
    EXPECT_FALSE(result);
    if (result) {
        return {};
    }
    EXPECT_EQ(result.error().code, Errc::invalid_configuration);
    EXPECT_EQ(result.error().operation, Operation::validate_config);
    return result.error().detail;
}

TEST(ConfigTest, DefaultsAreValid) {
    EXPECT_TRUE(validate(ConnectionOptions{}));
    EXPECT_TRUE(validate(HeartbeatOptions{}));
    EXPECT_TRUE(validate(ReconnectPolicy{}));
    EXPECT_TRUE(validate(ClientConfig{}));
}

TEST(ConfigTest, DefaultUserAgentContainsTheVersion) {
    EXPECT_EQ(ConnectionOptions{}.user_agent, std::string{"asyncwc/"} + std::string{version});
}

TEST(ConfigTest, RejectsNonPositiveTimeouts) {
    ConnectionOptions options;
    options.connect_timeout = milliseconds{0};
    EXPECT_NE(rejection(options).find("connect_timeout"), std::string::npos);

    options = {};
    options.handshake_timeout = milliseconds{-1};
    EXPECT_NE(rejection(options).find("handshake_timeout"), std::string::npos);
}

TEST(ConfigTest, RejectsZeroMessageSize) {
    ConnectionOptions options;
    options.max_message_size = 0;
    EXPECT_NE(rejection(options).find("max_message_size"), std::string::npos);
}

TEST(ConfigTest, RejectsHeaderInjection) {
    ConnectionOptions options;
    options.user_agent = "agent\r\nX-Evil: 1";
    EXPECT_NE(rejection(options).find("user_agent"), std::string::npos);

    options = {};
    options.headers = {{.name = "X-Token", .value = "a\nb"}};
    EXPECT_NE(rejection(options).find("X-Token"), std::string::npos);

    options.headers = {{.name = "X-Token", .value = std::string{"a\0b", 3}}};
    EXPECT_NE(rejection(options).find("X-Token"), std::string::npos);
}

TEST(ConfigTest, RejectsInvalidHeaderNames) {
    ConnectionOptions options;
    options.headers = {{.name = "", .value = "x"}};
    EXPECT_NE(rejection(options).find("not a valid HTTP token"), std::string::npos);
    options.headers = {{.name = "Bad Name", .value = "x"}};
    EXPECT_NE(rejection(options).find("Bad Name"), std::string::npos);
}

TEST(ConfigTest, RejectsHeadersOwnedByTheHandshake) {
    ConnectionOptions options;
    for (const auto* name :
         {"host", "UPGRADE", "Connection", "sec-websocket-key", "Sec-WebSocket-Version"}) {
        options.headers = {{.name = name, .value = "x"}};
        EXPECT_NE(rejection(options).find("managed by the WebSocket handshake"), std::string::npos)
            << name;
    }
}

TEST(ConfigTest, AcceptsCustomHeaders) {
    ConnectionOptions options;
    options.headers = {{.name = "Authorization", .value = "Bearer abc"},
                       {.name = "Sec-WebSocket-Protocol", .value = "chat"},
                       {.name = "X-Custom_Header.1~", .value = ""}};
    EXPECT_TRUE(validate(options));
}

TEST(ConfigTest, ValidatesHeartbeat) {
    HeartbeatOptions options{.interval = milliseconds{-1}, .timeout = milliseconds{1}};
    EXPECT_NE(rejection(options).find("interval"), std::string::npos);

    options = {.interval = milliseconds{100}, .timeout = milliseconds{0}};
    EXPECT_NE(rejection(options).find("timeout"), std::string::npos);

    options = {.interval = milliseconds{100}, .timeout = milliseconds{101}};
    EXPECT_NE(rejection(options).find("timeout"), std::string::npos);

    options = {.interval = milliseconds{100}, .timeout = milliseconds{100}};
    EXPECT_TRUE(validate(options));
}

TEST(ConfigTest, DisabledHeartbeatIgnoresTimeout) {
    const HeartbeatOptions options{.interval = milliseconds{0}, .timeout = milliseconds{-5}};
    EXPECT_FALSE(options.enabled());
    EXPECT_TRUE(validate(options));
}

TEST(ConfigTest, ValidatesReconnectPolicy) {
    ReconnectPolicy policy;
    policy.initial_delay = milliseconds{-1};
    EXPECT_NE(rejection(policy).find("initial_delay"), std::string::npos);

    policy = {};
    policy.initial_delay = milliseconds{100};
    policy.max_delay = milliseconds{99};
    EXPECT_NE(rejection(policy).find("max_delay"), std::string::npos);

    policy = {};
    policy.multiplier = 0.5;
    EXPECT_NE(rejection(policy).find("multiplier"), std::string::npos);
    policy.multiplier = std::numeric_limits<double>::quiet_NaN();
    EXPECT_NE(rejection(policy).find("multiplier"), std::string::npos);

    policy = {};
    policy.jitter = -0.1;
    EXPECT_NE(rejection(policy).find("jitter"), std::string::npos);
    policy.jitter = 1.5;
    EXPECT_NE(rejection(policy).find("jitter"), std::string::npos);
}

TEST(ConfigTest, ClientConfigValidatesAllParts) {
    ClientConfig config;
    config.connection.max_message_size = 0;
    EXPECT_NE(rejection(config).find("max_message_size"), std::string::npos);

    config = {};
    config.heartbeat.timeout = config.heartbeat.interval + milliseconds{1};
    EXPECT_NE(rejection(config).find("heartbeat"), std::string::npos);

    config = {};
    config.reconnect.jitter = 2.0;
    EXPECT_NE(rejection(config).find("jitter"), std::string::npos);
}

} // namespace
} // namespace asyncwc
