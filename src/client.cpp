#include "asyncwc/client.hpp"

#include "asyncwc/backoff.hpp"
#include "asyncwc/logging.hpp"
#include "detail/client_impl.hpp"
#include "detail/keep_alive.hpp"
#include "detail/scope_exit.hpp"

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <exception>
#include <format>
#include <utility>
#include <variant>

namespace asyncwc {
namespace detail {
namespace {

namespace asio = boost::asio;

constexpr auto use_nothrow = asio::as_tuple(asio::use_awaitable);
constexpr std::string_view heartbeat_payload = "asyncwc-heartbeat";

Error heartbeat_error(Errc errc) {
    return Error{.code = errc, .operation = Operation::heartbeat, .detail = {}};
}

asio::awaitable<bool> is_cancelled() {
    const auto state = co_await asio::this_coro::cancellation_state;
    co_return state.cancelled() != asio::cancellation_type::none;
}

} // namespace

ClientImpl::ClientImpl(asio::any_io_executor executor, ClientConfig config)
    : executor_{std::move(executor)},
      config_{std::move(config)},
      log_{config_.connection.logger ? config_.connection.logger : asyncwc::logger()},
      reconnect_timer_{executor_},
      close_done_{executor_} {}

template <typename Handler, typename... Args>
void ClientImpl::notify(std::string_view name, const Handler& handler,
                        const Args&... args) noexcept {
    if (!handler) {
        return;
    }
    try {
        handler(args...);
    } catch (const std::exception& e) {
        log_->error("{} handler threw: {}", name, e.what());
    } catch (...) {
        log_->error("{} handler threw a non-standard exception", name);
    }
}

asio::awaitable<Result<void>> ClientImpl::run() {
    if (running_) {
        co_return make_unexpected(Errc::operation_in_progress, Operation::run,
                                  "run() is already active");
    }
    if (auto valid = validate(config_); !valid) {
        co_return valid;
    }
    auto endpoint = parse_url(config_.url);
    if (!endpoint) {
        co_return std::unexpected(std::move(endpoint).error());
    }

    running_ = true;
    stop_requested_ = false;
    const ScopeExit finish{[this] noexcept {
        running_ = false;
        connection_.reset();
        state_ = ClientState::stopped;
    }};

    Backoff backoff{config_.reconnect};
    for (;;) {
        auto [reason, was_open] = co_await run_session(*endpoint);
        if (stop_requested_ || co_await is_cancelled()) {
            log_->info("client stopped");
            co_return Result<void>{};
        }
        if (!config_.reconnect.enabled) {
            co_return std::unexpected(std::move(reason));
        }
        if (was_open) {
            backoff.reset();
        }

        const auto delay = backoff.next();
        if (!delay) {
            log_->error("giving up after {} reconnect attempts", backoff.attempts());
            co_return make_unexpected(Errc::reconnect_attempts_exhausted, Operation::reconnect,
                                      reason.message());
        }
        state_ = ClientState::reconnecting;
        log_->info("reconnecting in {} ms (attempt {})", delay->count(), backoff.attempts());
        notify("on_reconnect", handlers_.on_reconnect, backoff.attempts(), *delay);
        if (!co_await wait_before_reconnect(*delay)) {
            log_->info("client stopped");
            co_return Result<void>{};
        }
    }
}

asio::awaitable<ClientImpl::SessionResult> ClientImpl::run_session(Endpoint endpoint) {
    state_ = ClientState::connecting;
    const auto connection = std::make_shared<ConnectionImpl>(executor_, config_.connection);
    connection_ = connection;
    connection->set_pong_handler([weak = weak_from_this()](std::string_view) {
        if (const auto self = weak.lock()) {
            self->handle_pong();
        }
    });

    if (auto connected = co_await connection->connect(std::move(endpoint)); !connected) {
        if (!stop_requested_) {
            log_->warn("connection attempt failed: {}", connected.error().message());
            notify("on_error", handlers_.on_error, connected.error());
        }
        co_return SessionResult{.reason = std::move(connected).error(), .was_open = false};
    }

    state_ = ClientState::connected;
    awaiting_pong_ = false;
    notify("on_open", handlers_.on_open);

    Error reason = co_await supervise(connection);
    if (close_pending_) {
        // close() aborts the read loop as soon as it starts; let it finish the closing
        // handshake before the connection is torn down.
        close_done_.expires_at(asio::steady_timer::time_point::max());
        co_await close_done_.async_wait(use_nothrow);
    }
    connection->abort(Errc::cancelled); // no-op unless the session ended by cancellation
    log_->info("disconnected: {}", reason.message());
    notify("on_disconnect", handlers_.on_disconnect, reason);
    co_return SessionResult{.reason = std::move(reason), .was_open = true};
}

asio::awaitable<Error> ClientImpl::supervise(std::shared_ptr<ConnectionImpl> connection) {
    using namespace asio::experimental::awaitable_operators;

    if (!config_.heartbeat.enabled()) {
        co_return co_await read_loop(connection);
    }
    // Whichever finishes first ends the session; the other one is cancelled.
    auto finished = co_await (read_loop(connection) || heartbeat(connection));
    co_return std::visit([](Error& reason) { return std::move(reason); }, finished);
}

asio::awaitable<Error> ClientImpl::read_loop(std::shared_ptr<ConnectionImpl> connection) {
    // Report cancellation through the result instead of an exception.
    co_await asio::this_coro::throw_if_cancelled(false);
    for (;;) {
        auto message = co_await connection->receive();
        if (!message) {
            co_return std::move(message).error();
        }
        notify("on_message", handlers_.on_message, *message);
    }
}

asio::awaitable<Error> ClientImpl::heartbeat(std::shared_ptr<ConnectionImpl> connection) {
    co_await asio::this_coro::throw_if_cancelled(false);
    const auto& options = config_.heartbeat;
    asio::steady_timer timer{executor_};
    auto next_ping = Clock::now() + options.interval;
    for (;;) {
        timer.expires_at(next_ping);
        if (auto [ec] = co_await timer.async_wait(use_nothrow); ec) {
            co_return heartbeat_error(Errc::cancelled);
        }

        ping_sent_at_ = Clock::now();
        next_ping = ping_sent_at_ + options.interval;
        awaiting_pong_ = true;
        // The ping may have to queue behind a large message, so it runs on its own and
        // the pong deadline below starts counting right away.
        asio::co_spawn(executor_,
                       keep_alive(connection, connection->ping(std::string{heartbeat_payload})),
                       asio::detached);

        timer.expires_at(ping_sent_at_ + options.timeout);
        if (auto [ec] = co_await timer.async_wait(use_nothrow); ec) {
            co_return heartbeat_error(Errc::cancelled);
        }
        if (awaiting_pong_) {
            log_->warn("no pong within {} ms, dropping the connection", options.timeout.count());
            connection->abort(Errc::pong_timeout);
            co_return heartbeat_error(Errc::pong_timeout);
        }
    }
}

void ClientImpl::handle_pong() {
    if (!awaiting_pong_) {
        log_->trace("ignoring unsolicited pong");
        return;
    }
    awaiting_pong_ = false;
    const auto round_trip =
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - ping_sent_at_);
    log_->trace("heartbeat pong after {} us", round_trip.count());
    notify("on_pong", handlers_.on_pong, round_trip);
}

asio::awaitable<bool> ClientImpl::wait_before_reconnect(std::chrono::milliseconds delay) {
    if (stop_requested_) { // stop() was called from the on_reconnect handler
        co_return false;
    }
    reconnect_timer_.expires_after(delay);
    auto [ec] = co_await reconnect_timer_.async_wait(use_nothrow);
    co_return !ec && !stop_requested_;
}

asio::awaitable<Result<void>> ClientImpl::send(Message message) {
    if (state_ != ClientState::connected) {
        co_return make_unexpected(Errc::not_connected, Operation::write,
                                  std::format("client is {}", to_string(state_)));
    }
    const auto connection = connection_;
    co_return co_await connection->send(std::move(message));
}

asio::awaitable<Result<void>> ClientImpl::close(CloseCode code, std::string reason) {
    if (auto valid = validate_close(code, reason); !valid) {
        co_return valid;
    }
    if (state_ == ClientState::closing) {
        co_return make_unexpected(Errc::operation_in_progress, Operation::close,
                                  "close() is already pending");
    }
    stop_requested_ = true;
    reconnect_timer_.cancel();
    if (state_ != ClientState::connected) {
        if (connection_) { // a connection attempt is in flight
            connection_->abort(Errc::cancelled);
        }
        co_return Result<void>{};
    }

    state_ = ClientState::closing;
    close_pending_ = true;
    const auto connection = connection_;
    auto result = co_await connection->close(code, std::move(reason));
    close_pending_ = false;
    close_done_.cancel();
    co_return result;
}

// NOLINTNEXTLINE(bugprone-exception-escape): cancelling an Asio timer cannot fail
void ClientImpl::stop() noexcept {
    stop_requested_ = true;
    reconnect_timer_.cancel();
    if (connection_) {
        connection_->abort(Errc::cancelled);
    }
}

} // namespace detail

std::string_view to_string(ClientState state) noexcept {
    switch (state) {
    case ClientState::idle:
        return "idle";
    case ClientState::connecting:
        return "connecting";
    case ClientState::connected:
        return "connected";
    case ClientState::reconnecting:
        return "reconnecting";
    case ClientState::closing:
        return "closing";
    case ClientState::stopped:
        return "stopped";
    }
    return "unknown";
}

Client::Client(executor_type executor, ClientConfig config)
    : impl_{std::make_shared<detail::ClientImpl>(std::move(executor), std::move(config))} {}

Client::~Client() {
    // Pending coroutines keep the implementation alive; make sure they no longer call
    // into handlers that may capture objects that are being destroyed with the client.
    impl_->handlers() = {};
    impl_->stop();
}

void Client::on_open(OpenHandler handler) {
    impl_->handlers().on_open = std::move(handler);
}

void Client::on_message(MessageHandler handler) {
    impl_->handlers().on_message = std::move(handler);
}

void Client::on_disconnect(DisconnectHandler handler) {
    impl_->handlers().on_disconnect = std::move(handler);
}

void Client::on_error(ErrorHandler handler) {
    impl_->handlers().on_error = std::move(handler);
}

void Client::on_reconnect(ReconnectHandler handler) {
    impl_->handlers().on_reconnect = std::move(handler);
}

void Client::on_pong(PongHandler handler) {
    impl_->handlers().on_pong = std::move(handler);
}

boost::asio::awaitable<Result<void>> Client::run() {
    return detail::keep_alive(impl_, impl_->run());
}

boost::asio::awaitable<Result<void>> Client::send(Message message) {
    return detail::keep_alive(impl_, impl_->send(std::move(message)));
}

boost::asio::awaitable<Result<void>> Client::send_text(std::string_view text) {
    return send(Message::text(std::string{text}));
}

boost::asio::awaitable<Result<void>> Client::send_binary(std::span<const std::byte> data) {
    return send(Message::binary(data));
}

boost::asio::awaitable<Result<void>> Client::close(CloseCode code, std::string_view reason) {
    return detail::keep_alive(impl_, impl_->close(code, std::string{reason}));
}

void Client::stop() noexcept {
    impl_->stop();
}

ClientState Client::state() const noexcept {
    return impl_->state();
}

const ClientConfig& Client::config() const noexcept {
    return impl_->config();
}

Client::executor_type Client::get_executor() const noexcept {
    return impl_->get_executor();
}

} // namespace asyncwc
