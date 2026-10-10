#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <utility>

namespace asyncwc::detail {

/// FIFO mutex for coroutines that share one executor.
///
/// A channel with room for exactly one element doubles as the lock: sending the element
/// acquires it (suspending while the slot is taken) and receiving it releases the lock,
/// which lets the longest waiting sender proceed.
class AsyncMutex {
    using Channel = boost::asio::experimental::channel<void(boost::system::error_code)>;

public:
    /// Owns the mutex until destroyed.
    class [[nodiscard]] Lock {
    public:
        explicit Lock(Channel& channel) noexcept : channel_{&channel} {}

        Lock(Lock&& other) noexcept : channel_{std::exchange(other.channel_, nullptr)} {}

        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        Lock& operator=(Lock&&) = delete;

        ~Lock() {
            if (channel_ != nullptr) {
                channel_->try_receive([](boost::system::error_code) {});
            }
        }

    private:
        Channel* channel_;
    };

    explicit AsyncMutex(const boost::asio::any_io_executor& executor) : channel_{executor, 1} {}

    [[nodiscard]] boost::asio::awaitable<Lock> lock() {
        co_await channel_.async_send(boost::system::error_code{},
                                     boost::asio::as_tuple(boost::asio::use_awaitable));
        co_return Lock{channel_};
    }

private:
    Channel channel_;
};

} // namespace asyncwc::detail
