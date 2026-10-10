#include "asyncwc/message.hpp"

#include <gtest/gtest.h>

#include <array>

namespace asyncwc {
namespace {

TEST(MessageTest, DefaultsToEmptyText) {
    const Message message;
    EXPECT_TRUE(message.is_text());
    EXPECT_FALSE(message.is_binary());
    EXPECT_TRUE(message.payload.empty());
}

TEST(MessageTest, TextFactory) {
    const auto message = Message::text("hello");
    EXPECT_TRUE(message.is_text());
    EXPECT_EQ(message.payload, "hello");
}

TEST(MessageTest, BinaryFactoryCopiesBytes) {
    const std::array bytes{std::byte{0x00}, std::byte{0xff}, std::byte{0x7f}};
    const auto message = Message::binary(bytes);
    EXPECT_TRUE(message.is_binary());
    ASSERT_EQ(message.bytes().size(), bytes.size());
    EXPECT_TRUE(std::ranges::equal(message.bytes(), bytes));
}

TEST(MessageTest, Equality) {
    EXPECT_EQ(Message::text("a"), Message::text("a"));
    EXPECT_NE(Message::text("a"), Message::text("b"));
    EXPECT_NE(Message::text("a"), (Message{.type = MessageType::binary, .payload = "a"}));
}

TEST(MessageTest, SendableCloseCodes) {
    EXPECT_TRUE(is_sendable(CloseCode::normal));
    EXPECT_TRUE(is_sendable(CloseCode::going_away));
    EXPECT_TRUE(is_sendable(CloseCode::try_again_later));
    EXPECT_TRUE(is_sendable(static_cast<CloseCode>(1014)));
    EXPECT_TRUE(is_sendable(static_cast<CloseCode>(3000)));
    EXPECT_TRUE(is_sendable(static_cast<CloseCode>(4999)));

    EXPECT_FALSE(is_sendable(CloseCode::no_status));
    EXPECT_FALSE(is_sendable(CloseCode::abnormal));
    EXPECT_FALSE(is_sendable(static_cast<CloseCode>(1004)));
    EXPECT_FALSE(is_sendable(static_cast<CloseCode>(1015)));
    EXPECT_FALSE(is_sendable(static_cast<CloseCode>(999)));
    EXPECT_FALSE(is_sendable(static_cast<CloseCode>(2999)));
    EXPECT_FALSE(is_sendable(static_cast<CloseCode>(5000)));
}

TEST(MessageTest, CloseInfoEquality) {
    EXPECT_EQ((CloseInfo{.code = CloseCode::normal, .reason = "bye"}),
              (CloseInfo{.code = CloseCode::normal, .reason = "bye"}));
    EXPECT_NE((CloseInfo{.code = CloseCode::normal, .reason = "bye"}), CloseInfo{});
}

} // namespace
} // namespace asyncwc
