#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace asyncwc::test {

inline constexpr std::chrono::seconds default_timeout{10};

/// Runs `io` until `done` turns true; throws if that takes longer than `timeout`.
inline void drive(boost::asio::io_context& io, const bool& done,
                  std::chrono::steady_clock::duration timeout) {
    io.restart();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done) {
        const auto handled = io.run_one_until(deadline);
        if (done) {
            break;
        }
        if (handled == 0) {
            throw std::runtime_error(io.stopped() ? "io_context ran out of work"
                                                  : "operation timed out");
        }
    }
}

/// Runs `io` until `operation` completes and returns its result.
///
/// This lets tests read sequentially and use ASSERT_* (which cannot be used inside
/// coroutines) while servers and other coroutines keep running on the same io_context.
template <typename T>
T run(boost::asio::io_context& io, boost::asio::awaitable<T> operation,
      std::chrono::steady_clock::duration timeout = default_timeout) {
    std::optional<T> result;
    std::exception_ptr failure;
    bool done = false;
    boost::asio::co_spawn(io, std::move(operation), [&](std::exception_ptr error, T value) {
        failure = std::move(error);
        if (!failure) {
            result.emplace(std::move(value));
        }
        done = true;
    });
    drive(io, done, timeout);
    if (failure) {
        std::rethrow_exception(failure);
    }
    return std::move(*result);
}

inline void run(boost::asio::io_context& io, boost::asio::awaitable<void> operation,
                std::chrono::steady_clock::duration timeout = default_timeout) {
    std::exception_ptr failure;
    bool done = false;
    boost::asio::co_spawn(io, std::move(operation), [&](std::exception_ptr error) {
        failure = std::move(error);
        done = true;
    });
    drive(io, done, timeout);
    if (failure) {
        std::rethrow_exception(failure);
    }
}

/// Runs `io` for `duration`, e.g. to let background coroutines make progress.
inline void run_for(boost::asio::io_context& io, std::chrono::steady_clock::duration duration) {
    io.restart();
    io.run_for(duration);
}

/// Runs `io` until `predicate()` holds; throws after `timeout`.
template <typename Predicate>
void run_until(boost::asio::io_context& io, Predicate predicate,
               std::chrono::steady_clock::duration timeout = default_timeout) {
    io.restart();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error("condition not reached in time");
        }
        io.run_one_for(std::chrono::milliseconds{10});
        if (io.stopped()) {
            io.restart();
        }
    }
}

/// Starts `operation` in the background; the returned slot is filled on completion.
template <typename T>
std::shared_ptr<std::optional<T>> spawn(boost::asio::io_context& io,
                                        boost::asio::awaitable<T> operation) {
    auto slot = std::make_shared<std::optional<T>>();
    boost::asio::co_spawn(io, std::move(operation),
                          [slot](const std::exception_ptr& error, T value) {
                              if (error) {
                                  std::rethrow_exception(error);
                              }
                              slot->emplace(std::move(value));
                          });
    return slot;
}

/// Suspends the calling coroutine for `duration`.
inline boost::asio::awaitable<void> sleep_for(std::chrono::steady_clock::duration duration) {
    boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, duration};
    co_await timer.async_wait(boost::asio::use_awaitable);
}

} // namespace asyncwc::test
