#include "asyncwc/connection.hpp"

#include "asyncwc/logging.hpp"
#include "detail/connection_impl.hpp"
#include "detail/keep_alive.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/beast/websocket/rfc6455.hpp>
#include <boost/beast/websocket/stream_base.hpp>

#include <format>
#include <utility>

namespace asyncwc {
namespace detail {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;

constexpr auto use_nothrow = asio::as_tuple(asio::use_awaitable);

/// Maps the Beast errors that callers commonly branch on to `Errc`; everything else
/// (DNS, TCP, protocol violations) keeps its original category.
std::error_code translate(const boost::system::error_code& ec) {
    if (ec == websocket::error::closed) {
        return Errc::connection_closed;
    }
    if (ec == beast::error::timeout) {
        return Errc::timeout;
    }
    if (ec == websocket::error::upgrade_declined) {
        return Errc::handshake_rejected;
    }
    return ec;
}

std::string describe(const websocket::close_reason& reason) {
    if (reason.reason.empty()) {
        return std::format("close code {}", reason.code);
    }
    return std::format("close code {}: {}", reason.code,
                       std::string_view{reason.reason.data(), reason.reason.size()});
}

} // namespace

Result<void> validate_close(CloseCode code, std::string_view reason) {
    if (!is_sendable(code)) {
        return make_unexpected(
            Errc::invalid_argument, Operation::close,
            std::format("close code {} must not be sent", std::to_underlying(code)));
    }
    if (reason.size() > max_close_reason) {
        return make_unexpected(Errc::invalid_argument, Operation::close,
                               std::format("close reason exceeds {} bytes", max_close_reason));
    }
    return {};
}

ConnectionImpl::ConnectionImpl(asio::any_io_executor executor, ConnectionOptions options)
    : executor_{std::move(executor)},
      options_{std::move(options)},
      log_{options_.logger ? options_.logger : asyncwc::logger()},
      resolver_{executor_},
      stream_{executor_},
      write_mutex_{executor_} {}

asio::awaitable<Result<void>> ConnectionImpl::connect(std::string url) {
    auto endpoint = parse_url(url);
    if (!endpoint) {
        co_return std::unexpected(std::move(endpoint).error());
    }
    co_return co_await connect(std::move(*endpoint));
}

asio::awaitable<Result<void>> ConnectionImpl::connect(Endpoint endpoint) {
    if (state_ == State::closed) {
        co_return make_unexpected(Errc::invalid_state, Operation::connect,
                                  "a Connection is single-use, create a new one to reconnect");
    }
    if (state_ != State::idle) {
        co_return make_unexpected(Errc::already_connected, Operation::connect);
    }
    if (auto valid = validate(options_); !valid) {
        co_return std::unexpected(std::move(valid).error());
    }

    endpoint_ = std::move(endpoint);
    state_ = State::connecting;
    auto established = co_await establish();
    if (!established) {
        log_->debug("connecting to {} failed: {}", endpoint_.authority(),
                    established.error().message());
        state_ = State::closed;
        teardown();
        co_return established;
    }

    state_ = State::open;
    log_->info("connected to ws://{}{}", endpoint_.authority(), endpoint_.target);
    co_return established;
}

asio::awaitable<Result<void>> ConnectionImpl::establish() {
    log_->debug("resolving {}", endpoint_.host);
    auto [resolve_ec, results] = co_await resolver_.async_resolve(
        endpoint_.host, std::to_string(endpoint_.port), use_nothrow);
    if (resolve_ec || aborted()) {
        co_return failure(translate(resolve_ec), Operation::resolve, endpoint_.host);
    }

    auto& tcp = beast::get_lowest_layer(stream_);
    tcp.expires_after(options_.connect_timeout);
    auto [connect_ec, peer] = co_await tcp.async_connect(results, use_nothrow);
    if (connect_ec || aborted()) {
        co_return failure(translate(connect_ec), Operation::connect, endpoint_.authority());
    }
    log_->debug("TCP connection to {}:{} established", peer.address().to_string(), peer.port());

    // From here on the WebSocket layer enforces its own timeouts.
    tcp.expires_never();
    configure_stream();

    websocket::response_type response;
    auto [handshake_ec] = co_await stream_.async_handshake(response, endpoint_.authority(),
                                                           endpoint_.target, use_nothrow);
    if (handshake_ec || aborted()) {
        std::string detail;
        if (handshake_ec == websocket::error::upgrade_declined) {
            detail = std::format("HTTP {} {}", response.result_int(),
                                 std::string_view{response.reason()});
        }
        co_return failure(translate(handshake_ec), Operation::handshake, std::move(detail));
    }
    co_return Result<void>{};
}

void ConnectionImpl::configure_stream() {
    stream_.set_option(websocket::stream_base::timeout{
        .handshake_timeout = options_.handshake_timeout,
        .idle_timeout = websocket::stream_base::none(),
        .keep_alive_pings = false,
    });
    stream_.set_option(websocket::stream_base::decorator(
        [user_agent = options_.user_agent,
         headers = options_.headers](websocket::request_type& request) {
            request.set(beast::http::field::user_agent, user_agent);
            for (const auto& header : headers) {
                request.set(header.name, header.value);
            }
        }));
    stream_.read_message_max(options_.max_message_size);
    stream_.control_callback([this](websocket::frame_type kind, beast::string_view payload) {
        on_control_frame(kind, payload);
    });
}

void ConnectionImpl::on_control_frame(websocket::frame_type kind, std::string_view payload) {
    switch (kind) {
    case websocket::frame_type::ping:
        log_->trace("ping received ({} bytes), answering", payload.size());
        break;
    case websocket::frame_type::pong:
        log_->trace("pong received ({} bytes)", payload.size());
        if (pong_handler_) {
            try {
                pong_handler_(payload);
            } catch (const std::exception& e) {
                log_->error("pong handler threw: {}", e.what());
            } catch (...) {
                log_->error("pong handler threw a non-standard exception");
            }
        }
        break;
    case websocket::frame_type::close:
        log_->debug("close frame received");
        break;
    }
}

asio::awaitable<Result<void>> ConnectionImpl::send(Message message) {
    if (state_ != State::open) {
        co_return make_unexpected(Errc::not_connected, Operation::write);
    }
    const auto lock = co_await write_mutex_.lock();
    if (state_ != State::open) { // closed while waiting for the lock
        co_return make_unexpected(Errc::not_connected, Operation::write);
    }

    stream_.text(message.is_text());
    auto [ec, written] = co_await stream_.async_write(asio::buffer(message.payload), use_nothrow);
    if (ec || aborted()) {
        co_return failure(translate(ec), Operation::write);
    }
    log_->trace("sent {} message ({} bytes)", message.is_text() ? "text" : "binary", written);
    co_return Result<void>{};
}

asio::awaitable<Result<Message>> ConnectionImpl::receive() {
    if (state_ != State::open && state_ != State::closing) {
        co_return make_unexpected(Errc::not_connected, Operation::read);
    }
    if (reading_) {
        co_return make_unexpected(Errc::operation_in_progress, Operation::read,
                                  "another receive() is pending");
    }

    reading_ = true;
    auto [ec, size] = co_await stream_.async_read(buffer_, use_nothrow);
    reading_ = false;
    if (ec || aborted()) {
        co_return read_failed(ec);
    }

    Message message{
        .type = stream_.got_text() ? MessageType::text : MessageType::binary,
        .payload = beast::buffers_to_string(buffer_.data()),
    };
    buffer_.consume(size);
    log_->trace("received {} message ({} bytes)", message.is_text() ? "text" : "binary", size);
    co_return message;
}

std::unexpected<Error> ConnectionImpl::read_failed(const boost::system::error_code& ec) {
    std::string detail;
    if (ec == websocket::error::closed) {
        record_close_frame();
        detail = describe(stream_.reason());
        log_->info("close frame received ({})", detail);
    }

    if (close_started_) {
        // close() while a receive() is pending: Beast lets the read perform the closing
        // handshake and completes whichever operation did not with operation_aborted.
        if (ec == asio::error::operation_aborted && !aborted()) {
            return make_unexpected(Errc::connection_closed, Operation::read, "closed locally");
        }
        handshake_outcome_ = ec == websocket::error::closed ? std::error_code{} : translate(ec);
        return failure(translate(ec), Operation::read, std::move(detail)); // close() tears down
    }

    state_ = State::closed;
    teardown();
    return failure(translate(ec), Operation::read, std::move(detail));
}

void ConnectionImpl::record_close_frame() {
    const auto& frame = stream_.reason();
    close_info_.emplace();
    close_info_->code = static_cast<CloseCode>(frame.code);
    close_info_->reason.assign(frame.reason.data(), frame.reason.size());
}

asio::awaitable<Result<void>> ConnectionImpl::ping(std::string payload) {
    if (payload.size() > max_control_payload) {
        co_return make_unexpected(
            Errc::invalid_argument, Operation::ping,
            std::format("ping payload exceeds {} bytes", max_control_payload));
    }
    if (state_ != State::open) {
        co_return make_unexpected(Errc::not_connected, Operation::ping);
    }
    const auto lock = co_await write_mutex_.lock();
    if (state_ != State::open) {
        co_return make_unexpected(Errc::not_connected, Operation::ping);
    }

    websocket::ping_data data;
    data.assign(payload.data(), payload.size());
    auto [ec] = co_await stream_.async_ping(data, use_nothrow);
    if (ec || aborted()) {
        co_return failure(translate(ec), Operation::ping);
    }
    log_->trace("ping sent ({} bytes)", payload.size());
    co_return Result<void>{};
}

asio::awaitable<Result<void>> ConnectionImpl::close(CloseCode code, std::string reason) {
    if (auto valid = validate_close(code, reason); !valid) {
        co_return valid;
    }
    if (state_ == State::closing) {
        co_return make_unexpected(Errc::operation_in_progress, Operation::close,
                                  "close() is already pending");
    }
    if (state_ != State::open) {
        co_return make_unexpected(Errc::not_connected, Operation::close);
    }

    state_ = State::closing;
    close_started_ = true;
    log_->debug("closing connection ({})", std::to_underlying(code));
    websocket::close_reason frame{std::to_underlying(code)};
    frame.reason.assign(reason.data(), reason.size());
    auto [ec] = co_await stream_.async_close(frame, use_nothrow);
    state_ = State::closed;
    teardown();

    auto outcome = translate(ec);
    if (ec == asio::error::operation_aborted && handshake_outcome_) {
        outcome = *handshake_outcome_; // a pending read did the handshake, see read_failed()
    }
    if (outcome || aborted()) {
        co_return failure(outcome, Operation::close);
    }
    record_close_frame();
    log_->info("connection closed ({})", describe(stream_.reason()));
    co_return Result<void>{};
}

void ConnectionImpl::abort(std::error_code reason) noexcept {
    if (state_ == State::closed) {
        return;
    }
    abort_reason_ = reason;
    state_ = State::closed;
    resolver_.cancel();
    teardown();
}

std::unexpected<Error> ConnectionImpl::failure(std::error_code code, Operation operation,
                                               std::string detail) const {
    // After abort() every failure is a consequence of it, so report its reason instead.
    return make_unexpected(aborted() ? abort_reason_ : code, operation, std::move(detail));
}

void ConnectionImpl::teardown() noexcept {
    boost::system::error_code ignored;
    beast::get_lowest_layer(stream_).socket().close(ignored);
}

} // namespace detail

Connection::Connection(executor_type executor, ConnectionOptions options)
    : impl_{std::make_shared<detail::ConnectionImpl>(std::move(executor), std::move(options))} {}

Connection::~Connection() {
    impl_->abort(Errc::cancelled);
}

Connection::executor_type Connection::get_executor() const noexcept {
    return impl_->get_executor();
}

boost::asio::awaitable<Result<void>> Connection::connect(std::string_view url) {
    return detail::keep_alive(impl_, impl_->connect(std::string{url}));
}

boost::asio::awaitable<Result<void>> Connection::connect(Endpoint endpoint) {
    return detail::keep_alive(impl_, impl_->connect(std::move(endpoint)));
}

boost::asio::awaitable<Result<void>> Connection::send(Message message) {
    return detail::keep_alive(impl_, impl_->send(std::move(message)));
}

boost::asio::awaitable<Result<void>> Connection::send_text(std::string_view text) {
    return send(Message::text(std::string{text}));
}

boost::asio::awaitable<Result<void>> Connection::send_binary(std::span<const std::byte> data) {
    return send(Message::binary(data));
}

boost::asio::awaitable<Result<Message>> Connection::receive() {
    return detail::keep_alive(impl_, impl_->receive());
}

boost::asio::awaitable<Result<void>> Connection::ping(std::string_view payload) {
    return detail::keep_alive(impl_, impl_->ping(std::string{payload}));
}

boost::asio::awaitable<Result<void>> Connection::close(CloseCode code, std::string_view reason) {
    return detail::keep_alive(impl_, impl_->close(code, std::string{reason}));
}

void Connection::cancel() noexcept {
    impl_->abort(Errc::cancelled);
}

bool Connection::is_open() const noexcept {
    return impl_->is_open();
}

std::optional<CloseInfo> Connection::close_info() const {
    return impl_->close_info();
}

void Connection::on_pong(PongHandler handler) {
    impl_->set_pong_handler(std::move(handler));
}

} // namespace asyncwc
