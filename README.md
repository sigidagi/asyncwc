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

## How this project was made

asyncwc was written by [Claude Code](https://claude.com/claude-code) (model Claude Opus 5.5,
effort level "xhigh") in a single session. Roughly an hour passed between the first file and
the tagged v1.0.0 commit. The complete specification was this one prompt:

> spec:I need that you will create a c++ websocket client (only non-secure) using c++20
> coroutines and boost::asio::io_context event loop; Consider other boost libraries only if
> it provide more redability. Make CMakeLists.txt, provide clang-format and clang-tidy tools
> can be used in the project. Add reconnect posibility if websocket server disconnects,
> ping/pong. Write gtests for full code coverage. for logging use spglog lib. Do not forget
> to provide nice error handling - you can consider std::expect or choose your own. Add some
> very simple examples in separate directory. Ask me only if you need something important to
> consider, otherwise Implement everything without prompting to console questions. name lib
> asyncwc

The work went roughly like this:

1. **Setup and design.** Claude checked the installed toolchain (GCC 16, Boost 1.92, spdlog,
   GoogleTest, lcov) and designed the layering: `Connection`, `Client` built on top of it, and
   shared implementation objects that keep pending coroutines safe.
2. **First run.** It wrote the library, then the tests against an in-process Beast server.
   107 of the first 114 tests passed.
3. **Fixing the failures.** The failures exposed:
   - Beast's behaviour when `close()` overlaps a pending read, now reconciled in
     `ConnectionImpl`.
   - A test-server bug: handlers were copied per connection.
   - A test with a missing disconnect trigger.
4. **Hardening.** ASan + UBSan, 100% line and function coverage, clang-tidy with zero
   findings, clang-format, Release with `-Werror`, a Clang build, and repeated runs to rule
   out flaky tests.
5. **Human input.** The author supplied the specification, `scripts/check.sh`, and the
   requests for the LICENSE, CI, badges and the v1.0.0 release.

### Size

Physical source lines of C++, without comments and blank lines, counted with
David A. Wheeler's SLOCCount:

| Part | Files | Lines of code |
|---|---:|---:|
| Library (`include/`, `src/`) | 22 | 1,508 |
| Tests (`tests/`) | 13 | 1,752 |
| Examples (`examples/`) | 3 | 164 |
| **Total** | **38** | **3,424** |

### What it would cost to write by hand

SLOCCount's Basic COCOMO model estimates the conventional development effort for 3,424
lines:

- about **8.7 person-months** of effort over a 5.7-month schedule;
- about **$98,000** at SLOCCount's default salary of $56,286 per year (from 2004), with a
  2.4 overhead factor;
- about **$210,000** at a present-day salary of $120,000 per year.

COCOMO is a rough, decades-old model. It describes the traditional cost of code this size;
the actual cost of generating it follows.

### What it actually cost

The whole session, from the first question to this README, made 130 API calls to Claude
Opus 5.5 at standard speed. Token counts come from the session transcript and are priced
at Anthropic's API list rates:

| Tokens | Count | Price per million | Cost |
|---|---:|---:|---:|
| Cache reads | 38,295,226 | $0.20 | $7.66 |
| Output (incl. thinking) | 296,819 | $20.00 | $5.94 |
| Cache writes (1-hour) | 434,324 | $8.00 | $3.47 |
| Uncached input | 260 | $4.00 | $0.00 |
| **Total** | **39,026,629** | | **$17.07** |

Most tokens are cache reads: every call re-reads the conversation so far, and caching makes
that 20 times cheaper than uncached input. About $11 of the total went into turning the spec
into a working library; the rest went into tooling, CI and publishing. On a Claude
subscription plan these tokens are not billed individually, so the figure is the equivalent
API cost.

## License

MIT, see [LICENSE](LICENSE). Release notes are in [CHANGELOG.md](CHANGELOG.md).
