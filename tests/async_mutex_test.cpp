#include "detail/async_mutex.hpp"
#include "detail/keep_alive.hpp"
#include "detail/scope_exit.hpp"
#include "support/run.hpp"

#include <gtest/gtest.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <vector>

namespace asyncwc::detail {
namespace {

namespace asio = boost::asio;
using namespace std::chrono_literals;

asio::awaitable<void> critical_section(AsyncMutex& mutex, std::vector<int>& trace, int id) {
    const auto lock = co_await mutex.lock();
    trace.push_back(id);
    co_await test::sleep_for(5ms); // other coroutines queue up meanwhile
    trace.push_back(-id);
}

TEST(AsyncMutexTest, SerializesCoroutinesInArrivalOrder) {
    asio::io_context io;
    AsyncMutex mutex{io.get_executor()};
    std::vector<int> trace;
    for (int id = 1; id <= 3; ++id) {
        asio::co_spawn(io, critical_section(mutex, trace, id), asio::detached);
    }
    test::run_until(io, [&] { return trace.size() == 6; });
    EXPECT_EQ(trace, (std::vector{1, -1, 2, -2, 3, -3}));
}

TEST(AsyncMutexTest, MovedLockReleasesOnlyOnce) {
    asio::io_context io;
    AsyncMutex mutex{io.get_executor()};
    test::run(io, [&]() -> asio::awaitable<void> {
        {
            auto first = co_await mutex.lock();
            const auto moved = std::move(first);
        }
        const auto again = co_await mutex.lock(); // would hang if never released
    }());
}

TEST(KeepAliveTest, HoldsTheOwnerUntilTheOperationFinishes) {
    asio::io_context io;
    auto owner = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = owner;
    auto operation = keep_alive(std::move(owner), []() -> asio::awaitable<int> { co_return 7; }());
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(test::run(io, std::move(operation)), 7);
    EXPECT_TRUE(weak.expired());
}

TEST(ScopeExitTest, RunsOnScopeExit) {
    int calls = 0;
    {
        const ScopeExit guard{[&]() noexcept { ++calls; }};
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1);
}

} // namespace
} // namespace asyncwc::detail
