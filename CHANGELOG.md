# Changelog

All notable changes to this project are documented here. The project follows
[Semantic Versioning](https://semver.org).

## [1.0.0] - 2026-10-10

First release.

### Added

- `asyncwc::Client`: a self-healing ws:// client.
  - Reconnects with exponential backoff, jitter and an optional attempt limit.
  - A ping/pong heartbeat with a pong timeout detects dead peers and half-open
    connections.
  - Event handlers: open, message, disconnect, error, reconnect, pong round-trip time.
  - Graceful `close()`, immediate `stop()`, and support for Asio cancellation of `run()`.
- `asyncwc::Connection`: a single connection with a pull-style coroutine API: `connect`,
  `send`, `receive`, `ping`, `close`, `cancel`.
- Error handling based on `std::expected`.
  - `asyncwc::Error` carries an error code, the failed operation and a detail text.
  - `asyncwc::Errc` codes map to portable `std::errc` conditions.
- Up-front validation of URLs (Boost.URL), configuration, headers, ping payloads and close
  frames.
- spdlog logging through a replaceable library logger or a per-connection logger.
- GoogleTest suite with an in-process Beast test server; 100% line and function coverage.
- Tooling:
  - CMake presets: debug, release, asan, coverage, tidy, clang.
  - Install and `find_package(asyncwc)` support.
  - clang-format and clang-tidy configuration.
  - `scripts/check.sh`.
  - GitHub Actions CI.
- Examples: `echo_server`, `simple_client`, `reconnecting_client`.

[1.0.0]: https://github.com/sigidagi/asyncwc/releases/tag/v1.0.0
