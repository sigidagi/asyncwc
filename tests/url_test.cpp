#include "asyncwc/url.hpp"

#include <gtest/gtest.h>

namespace asyncwc {
namespace {

Endpoint parse_ok(std::string_view url) {
    auto endpoint = parse_url(url);
    EXPECT_TRUE(endpoint) << url << ": " << endpoint.error().message();
    return endpoint.value_or(Endpoint{});
}

Error parse_error(std::string_view url) {
    auto endpoint = parse_url(url);
    EXPECT_FALSE(endpoint) << url;
    return endpoint ? Error{} : endpoint.error();
}

TEST(UrlTest, ParsesHostWithDefaultPortAndTarget) {
    EXPECT_EQ(parse_ok("ws://example.com"),
              (Endpoint{.host = "example.com", .port = 80, .target = "/"}));
    EXPECT_EQ(parse_ok("ws://example.com/"),
              (Endpoint{.host = "example.com", .port = 80, .target = "/"}));
}

TEST(UrlTest, ParsesPortPathAndQuery) {
    EXPECT_EQ(parse_ok("ws://127.0.0.1:9001/chat/room?user=a%20b&x=1"),
              (Endpoint{.host = "127.0.0.1", .port = 9001, .target = "/chat/room?user=a%20b&x=1"}));
    EXPECT_EQ(parse_ok("ws://host?only=query"),
              (Endpoint{.host = "host", .port = 80, .target = "/?only=query"}));
}

TEST(UrlTest, EmptyPortMeansDefaultPort) {
    EXPECT_EQ(parse_ok("ws://host:/x").port, 80);
}

TEST(UrlTest, SchemeIsCaseInsensitive) {
    EXPECT_EQ(parse_ok("WS://Example.com:81").port, 81);
}

TEST(UrlTest, ParsesIpv6Literals) {
    const auto endpoint = parse_ok("ws://[::1]:8080/path");
    EXPECT_EQ(endpoint.host, "::1");
    EXPECT_EQ(endpoint.port, 8080);
    EXPECT_EQ(endpoint.authority(), "[::1]:8080");
}

TEST(UrlTest, AuthorityOmitsTheDefaultPort) {
    EXPECT_EQ((Endpoint{.host = "example.com", .port = 80, .target = "/"}).authority(),
              "example.com");
    EXPECT_EQ((Endpoint{.host = "example.com", .port = 8080, .target = "/"}).authority(),
              "example.com:8080");
    EXPECT_EQ((Endpoint{.host = "::1", .port = 80, .target = "/"}).authority(), "[::1]");
}

TEST(UrlTest, RejectsSecureWebSockets) {
    const auto error = parse_error("wss://example.com");
    EXPECT_EQ(error.code, Errc::unsupported_scheme);
    EXPECT_EQ(error.operation, Operation::parse_url);
    EXPECT_NE(error.detail.find("TLS"), std::string::npos);
}

TEST(UrlTest, RejectsOtherSchemes) {
    const auto error = parse_error("http://example.com");
    EXPECT_EQ(error.code, Errc::unsupported_scheme);
    EXPECT_EQ(error.detail, "'http://'");
    EXPECT_EQ(parse_error("ftp://example.com").code, Errc::unsupported_scheme);
}

TEST(UrlTest, RejectsMalformedUrls) {
    EXPECT_EQ(parse_error("").code, Errc::invalid_url);
    EXPECT_EQ(parse_error("not a url").code, Errc::invalid_url);
    EXPECT_EQ(parse_error("ws://host:port").code, Errc::invalid_url);
}

TEST(UrlTest, RejectsMissingHost) {
    const auto error = parse_error("ws:///path");
    EXPECT_EQ(error.code, Errc::invalid_url);
    EXPECT_NE(error.detail.find("missing host"), std::string::npos);
    EXPECT_EQ(parse_error("ws:path").code, Errc::invalid_url);
}

TEST(UrlTest, RejectsCredentialsAndFragments) {
    EXPECT_NE(parse_error("ws://user:secret@host/").detail.find("credentials"), std::string::npos);
    EXPECT_NE(parse_error("ws://host/#frag").detail.find("fragment"), std::string::npos);
}

TEST(UrlTest, RejectsOutOfRangePorts) {
    EXPECT_NE(parse_error("ws://host:0/").detail.find("port"), std::string::npos);
    EXPECT_NE(parse_error("ws://host:65536/").detail.find("port"), std::string::npos);
    EXPECT_EQ(parse_ok("ws://host:65535/").port, 65535);
}

} // namespace
} // namespace asyncwc
