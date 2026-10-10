#pragma once

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <utility>

namespace asyncwc::detail {

/// Awaits `operation` while co-owning `owner`.
///
/// Public handles (Client, Connection) forward to a shared implementation object. Their
/// awaitables are lazy, so the implementation has to stay alive until each started
/// coroutine finishes, even if the handle is destroyed in the meantime. Coroutine
/// parameters are copied into the frame when the coroutine is created, which is what
/// makes this work.
template <typename T, typename Owner>
boost::asio::awaitable<T> keep_alive(std::shared_ptr<Owner> owner,
                                     boost::asio::awaitable<T> operation) {
    const auto guard = std::move(owner);
    co_return co_await std::move(operation);
}

} // namespace asyncwc::detail
