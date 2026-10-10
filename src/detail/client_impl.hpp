#pragma once

#include "asyncwc/client.hpp"
#include "asyncwc/config.hpp"
#include "asyncwc/error.hpp"
#include "asyncwc/message.hpp"
#include "asyncwc/url.hpp"
#include "detail/connection_impl.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <spdlog/logger.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace asyncwc::detail {

/// Callbacks registered through the `Client::on_*` setters.
struct ClientHandlers {
    Client::OpenHandler on_open;
    Client::MessageHandler on_message;
    Client::DisconnectHandler on_disconnect;
    Client::ErrorHandler on_error;
    Client::ReconnectHandler on_reconnect;
    Client::PongHandler on_pong;
};

/// Shared state behind `asyncwc::Client`.
class ClientImpl : public std::enable_shared_from_this<ClientImpl> {
public:
    ClientImpl(boost::asio::any_io_executor executor, ClientConfig config);

    [[nodiscard]] boost::asio::awaitable<Result<void>> run();
    [[nodiscard]] boost::asio::awaitable<Result<void>> send(Message message);
    [[nodiscard]] boost::asio::awaitable<Result<void>> close(CloseCode code, std::string reason);
    void stop() noexcept;

    [[nodiscard]] ClientState state() const noexcept { return state_; }

    [[nodiscard]] const ClientConfig& config() const noexcept { return config_; }

    [[nodiscard]] const boost::asio::any_io_executor& get_executor() const noexcept {
        return executor_;
    }

    [[nodiscard]] ClientHandlers& handlers() noexcept { return handlers_; }

private:
    using Clock = std::chrono::steady_clock;

    struct SessionResult {
        Error reason;
        bool was_open = false;
    };

    [[nodiscard]] boost::asio::awaitable<SessionResult> run_session(Endpoint endpoint);
    [[nodiscard]] boost::asio::awaitable<Error>
    supervise(std::shared_ptr<ConnectionImpl> connection);
    [[nodiscard]] boost::asio::awaitable<Error>
    read_loop(std::shared_ptr<ConnectionImpl> connection);
    [[nodiscard]] boost::asio::awaitable<Error>
    heartbeat(std::shared_ptr<ConnectionImpl> connection);
    [[nodiscard]] boost::asio::awaitable<bool>
    wait_before_reconnect(std::chrono::milliseconds delay);
    void handle_pong();

    template <typename Handler, typename... Args>
    void notify(std::string_view name, const Handler& handler, const Args&... args) noexcept;

    boost::asio::any_io_executor executor_;
    ClientConfig config_;
    ClientHandlers handlers_;
    std::shared_ptr<spdlog::logger> log_;
    boost::asio::steady_timer reconnect_timer_;
    boost::asio::steady_timer close_done_; ///< cancelled when a pending close() finished
    std::shared_ptr<ConnectionImpl> connection_;
    ClientState state_ = ClientState::idle;
    bool running_ = false;
    bool stop_requested_ = false;
    bool close_pending_ = false;
    bool awaiting_pong_ = false;
    Clock::time_point ping_sent_at_;
};

} // namespace asyncwc::detail
