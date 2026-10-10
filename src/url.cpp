#include "asyncwc/url.hpp"

#include <boost/url/parse.hpp>
#include <boost/url/scheme.hpp>
#include <boost/url/url_view.hpp>

#include <format>

namespace asyncwc {
namespace {

std::unexpected<Error> invalid_url(std::string_view url, std::string_view reason) {
    return make_unexpected(Errc::invalid_url, Operation::parse_url,
                           std::format("'{}': {}", url, reason));
}

} // namespace

std::string Endpoint::authority() const {
    // IPv6 literals are the only hosts that contain a colon.
    std::string result = host.contains(':') ? "[" + host + "]" : host;
    if (port != default_port) {
        result += ':';
        result += std::to_string(port);
    }
    return result;
}

Result<Endpoint> parse_url(std::string_view url) {
    namespace urls = boost::urls;

    const auto parsed = urls::parse_uri(url);
    if (!parsed) {
        return invalid_url(url, parsed.error().message());
    }
    const urls::url_view& uri = *parsed;

    switch (uri.scheme_id()) {
    case urls::scheme::ws:
        break;
    case urls::scheme::wss:
        return make_unexpected(Errc::unsupported_scheme, Operation::parse_url,
                               "wss:// needs TLS, which asyncwc does not implement");
    default:
        return make_unexpected(Errc::unsupported_scheme, Operation::parse_url,
                               std::format("'{}://'", std::string_view{uri.scheme()}));
    }

    if (uri.encoded_host().empty()) {
        return invalid_url(url, "missing host");
    }
    if (uri.has_userinfo()) {
        return invalid_url(url, "credentials in the URL are not supported");
    }
    if (uri.has_fragment()) {
        return invalid_url(url, "WebSocket URLs must not contain a fragment");
    }

    Endpoint endpoint;
    endpoint.host = uri.host_address();
    if (uri.has_port() && !uri.port().empty()) {
        // port_number() is zero for "0" and for values that do not fit into 16 bits.
        if (uri.port_number() == 0) {
            return invalid_url(url, "port out of range");
        }
        endpoint.port = uri.port_number();
    }
    if (!uri.encoded_path().empty()) {
        endpoint.target = std::string_view{uri.encoded_path()};
    }
    if (uri.has_query()) {
        endpoint.target += '?';
        endpoint.target += std::string_view{uri.encoded_query()};
    }
    return endpoint;
}

} // namespace asyncwc
