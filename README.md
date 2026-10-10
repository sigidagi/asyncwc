# asyncwc

[![CI](https://github.com/sigidagi/asyncwc/actions/workflows/ci.yml/badge.svg)](https://github.com/sigidagi/asyncwc/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/github/v/tag/sigidagi/asyncwc?sort=semver&label=version)](https://github.com/sigidagi/asyncwc/tags)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?logo=cplusplus)](https://en.cppreference.com/w/cpp/20)
[![Coverage](https://img.shields.io/badge/coverage-100%25-brightgreen.svg)](https://github.com/sigidagi/asyncwc/actions/workflows/ci.yml)

An asynchronous WebSocket client for plain `ws://` connections, built on C++20 coroutines,
`boost::asio::io_context` and Boost.Beast.

- **Coroutine API**: every operation is an `asio::awaitable` returning
  `std::expected<T, asyncwc::Error>`. No exceptions for expected failures.
- **Automatic reconnect** with exponential backoff, jitter and an optional attempt limit.
- **Ping/pong heartbeat** that detects dead peers and half-open TCP connections and
  reports round-trip times.
- **Logging** through spdlog: a library-wide logger, overridable per connection.
- Two levels: `asyncwc::Client`, which keeps itself connected, and `asyncwc::Connection`,
  a single connection with a pull-style API.

TLS (`wss://`) is intentionally not supported; such URLs are rejected with
`Errc::unsupported_scheme`.

## Requirements

| Tool / library | Version used for development | Minimum |
|---|---|---|
| C++ compiler | GCC 16, Clang 22 | C++23 (`std::expected`) |
| CMake | 4.4 | 3.25 |
| Boost (Asio, Beast, URL) | 1.92 | 1.83 |
| spdlog | 1.17 | 1.12 (fetched if not installed) |
| GoogleTest | 1.18 | 1.14 (fetched if not installed) |
| clang-format / clang-tidy | 22 | 16 / 18 |
| lcov | 2.5 | 2.0 (coverage only) |

## Building

```sh
cmake --preset debug             # Ninja, build/debug
cmake --build --preset debug
ctest --preset debug
```

| Preset | Purpose |
|---|---|
| `debug` | regular development build |
| `release` | optimized build, warnings as errors |
| `asan` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `coverage` | `cmake --build --preset coverage` runs the tests and writes `build/coverage/coverage/index.html` |
| `tidy` | runs clang-tidy on every translation unit while compiling |
| `clang` | builds with `clang++` |

`scripts/check.sh` wraps these steps and prints a short summary, keeping the full logs in
`build/logs/`:

```sh
scripts/check.sh all              # format, build, test, tidy, coverage, release
scripts/check.sh test 'Client*'   # build, then run the tests matching a gtest filter
scripts/check.sh format --fix     # reformat in place
```

CMake options: `ASYNCWC_BUILD_TESTS`, `ASYNCWC_BUILD_EXAMPLES`, `ASYNCWC_ENABLE_COVERAGE`,
`ASYNCWC_ENABLE_CLANG_TIDY`, `ASYNCWC_ENABLE_SANITIZERS`, `ASYNCWC_WARNINGS_AS_ERRORS`,
`ASYNCWC_INSTALL`.

### Formatting and static analysis

```sh
cmake --build build/debug --target format         # clang-format -i on all sources
cmake --build build/debug --target format-check   # fails on unformatted files (CI)
cmake --build build/debug --target tidy           # run-clang-tidy over src/, tests/, examples/
```

To pin specific tool versions, pass `-DCLANG_FORMAT_EXE=...`, `-DCLANG_TIDY_EXE=...` or
`-DRUN_CLANG_TIDY_EXE=...` when configuring. The check selection lives in `.clang-tidy`;
`tests/.clang-tidy` and `examples/.clang-tidy` relax a few checks, and each file explains why.

### Using the library from another project

```cmake
find_package(asyncwc 1.0 REQUIRED)        # after `cmake --install`
# or: add_subdirectory(asyncwc)
target_link_libraries(app PRIVATE asyncwc::asyncwc)
```

## Quick start

### Client: stays connected

```cpp
#include <asyncwc/asyncwc.hpp>
#include <boost/asio.hpp>

namespace asio = boost::asio;
using namespace std::chrono_literals;

int main() {
    asio::io_context io;

    asyncwc::ClientConfig config;
    config.url = "ws://127.0.0.1:8080/chat";
    config.heartbeat = {.interval = 15s, .timeout = 5s};
    config.reconnect.max_attempts = 0;            // retry forever

    asyncwc::Client client{io.get_executor(), config};
    client.on_open([&] {
        asio::co_spawn(io, client.send_text("hello"), asio::detached);
    });
    client.on_message([](const asyncwc::Message& m) { spdlog::info("got {}", m.payload); });
    client.on_disconnect([](const asyncwc::Error& why) { spdlog::warn("{}", why.message()); });

    asio::co_spawn(io, client.run(), [](std::exception_ptr, asyncwc::Result<void> result) {
        if (!result) spdlog::error("gave up: {}", result.error().message());
    });
    io.run();
}
```

### Connection: one connection, pull style

```cpp
asio::awaitable<void> talk() {
    asyncwc::Connection connection{co_await asio::this_coro::executor};
    if (auto opened = co_await connection.connect("ws://127.0.0.1:8080"); !opened) {
        spdlog::error("{}", opened.error().message());
        co_return;
    }
    if (auto sent = co_await connection.send_text("ping?"); !sent) {
        co_return;
    }
    if (auto reply = co_await connection.receive()) {
        spdlog::info("reply: {}", reply->payload);
    }
    (void)co_await connection.close();
}
```

## API overview

### `asyncwc::Client`

| Member | Meaning |
|---|---|
| `run()` | Connects and supervises the connection until `close()`/`stop()` or Asio cancellation (success), or until the reconnect policy gives up (`Errc::reconnect_attempts_exhausted`). With reconnect disabled, the first failure ends `run()` with that error. |
| `send(Message)`, `send_text(sv)`, `send_binary(span)` | Fail with `Errc::not_connected` unless `state() == connected`. Concurrent sends are queued and keep their order. |
| `close(code, reason)` | Closing handshake (when connected), then `run()` ends without reconnecting. |
| `stop()` | Drops the connection immediately; `run()` ends. |
| `on_open`, `on_message`, `on_disconnect(Error)`, `on_error(Error)`, `on_reconnect(attempt, delay)`, `on_pong(rtt)` | Event handlers. They run on the client's executor; exceptions thrown by handlers are logged and swallowed. |
| `state()` | `idle`, `connecting`, `connected`, `reconnecting`, `closing`, `stopped` |

`on_error` reports failed connection attempts (DNS, TCP, handshake). `on_disconnect`
reports the end of a connection that was open, with the reason: `Errc::connection_closed`
(details in `Error::detail`, e.g. `close code 1001: maintenance`), `Errc::pong_timeout`,
or a network error such as end of file.

### `asyncwc::Connection`

`connect(url | Endpoint)`, `send*`, `receive()`, `ping(payload)`, `close(code, reason)`,
`cancel()`, `is_open()`, `close_info()`, `on_pong(handler)`.

A `Connection` is single-use: after it closes, create a new one. Pings from the peer are
answered automatically while a `receive()` (or `close()`) is pending.

### Errors

```cpp
template <typename T = void> using Result = std::expected<T, asyncwc::Error>;

struct Error {
    std::error_code code;   // asyncwc::Errc, or the original system/Beast error
    Operation operation;    // resolve, connect, handshake, read, write, ping, close, ...
    std::string detail;     // e.g. "HTTP 403 Forbidden"
    std::string message() const;  // "handshake: WebSocket handshake rejected by the server (HTTP 403 Forbidden)"
};
```

`asyncwc::Errc` values map to portable `std::errc` conditions where one exists, so both
`error.code == asyncwc::Errc::pong_timeout` and `error.code == std::errc::timed_out` work.
Invalid input is rejected up front instead of failing later on the wire:

- URLs: scheme, host, port, credentials, fragments.
- Configuration: value ranges; CR/LF injection in headers; handshake-owned headers.
- Ping payloads over 125 bytes, close reasons over 123 bytes, and reserved close codes.

### Configuration (`ClientConfig`)

| Field | Default | |
|---|---|---|
| `connection.connect_timeout` | 10 s | TCP connect |
| `connection.handshake_timeout` | 10 s | opening and closing handshake |
| `connection.max_message_size` | 16 MiB | larger incoming messages fail the read |
| `connection.user_agent` / `headers` | `asyncwc/<version>` / none | sent with the upgrade request |
| `connection.logger` | `asyncwc::logger()` | per-client spdlog logger |
| `heartbeat.interval` / `timeout` | 15 s / 10 s | `interval = 0` disables; `timeout <= interval` |
| `reconnect.enabled` | `true` | |
| `reconnect.initial_delay` / `max_delay` | 500 ms / 30 s | |
| `reconnect.multiplier` / `jitter` | 2.0 / 0.2 | delay = initial * multiplier^n, ±jitter |
| `reconnect.max_attempts` | 0 (unlimited) | consecutive failures before giving up |

### Logging

The library logs through `asyncwc::logger()`. That is the spdlog logger registered as
`"asyncwc"` if there is one, otherwise a colored stderr logger. Use
`asyncwc::set_logger(...)` to replace it, or set `ConnectionOptions::logger` per client.
Levels: `info` for connects and disconnects, `warning` for failures, `debug` for
handshake steps, `trace` for frames.

## Design notes

- **Threading.** Objects are not thread-safe. Use each object from the thread (or strand)
  that runs its executor, which is the usual Asio contract. One `io_context` running on a
  single thread is the intended setup.
- **Lifetime.** `Client` and `Connection` are handles to a shared implementation. Every
  started coroutine co-owns that implementation, so destroying a handle while operations
  are pending is safe: they complete with `Errc::cancelled`, and the client's handlers are
  not called any more.
- **Heartbeat.** A ping is sent every `interval`. If no pong arrives within `timeout`, the
  connection is dropped (`Errc::pong_timeout`) and the reconnect logic takes over. The
  pong deadline is measured independently of the ping write, so a write stuck behind a full
  TCP buffer is still detected.
- **Writes** are serialized with a small FIFO async mutex (`src/detail/async_mutex.hpp`),
  as Beast permits only one outstanding write per stream.
- **Cancellation.** `client.run()` honors Asio per-operation cancellation, for example
  `co_await (client.run() || timer.async_wait(use_awaitable))`.

## Examples

```sh
./build/debug/examples/echo_server 8080 &                        # tiny local echo server
./build/debug/examples/simple_client ws://127.0.0.1:8080          # Connection: send, receive, close
./build/debug/examples/reconnecting_client ws://127.0.0.1:8080    # Client: restart the server to see reconnects
```

## Layout

```
include/asyncwc/   public headers (asyncwc.hpp includes everything)
src/               implementation; src/detail holds internal helpers
tests/             GoogleTest suites plus an in-process Beast test server (tests/support)
examples/          echo_server, simple_client, reconnecting_client
cmake/             dependencies, warnings, sanitizers, coverage, clang tools, install
```

## License

MIT, see [LICENSE](LICENSE). Release notes are in [CHANGELOG.md](CHANGELOG.md).
